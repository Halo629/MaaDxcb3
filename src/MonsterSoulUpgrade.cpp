#include "Comm.h"
#include <algorithm>
#include <map>
#include <set>

// 魔魂列表 4×4 扫描区域
static const MaaRect kScanGridRoi = { 46, 139, 629, 824 };
static constexpr int kGridCellWidth = 157;  // 629/4
static constexpr int kGridCellHeight = 206; // 824/4
static constexpr int kGridCols = 4;

// 词条 OCR 区域
static const MaaRect kAffixRoi = { 194, 276, 345, 354 };

// 右下角 8 级检测区域
static const MaaRect kLevel8CheckRoi = { 548, 747, 129, 216 };

static int parseLevel(const std::string& text)
{
    std::string digits;
    for (char ch : text) {
        if (ch >= '0' && ch <= '9') {
            digits += ch;
        }
    }
    return digits.empty() ? -1 : std::atoi(digits.c_str());
}

static int ocrSingleLevel(MaaContext* context, const MaaImageBuffer* image, const MaaRect& roi)
{
    // ColorMatch 找白色数字区域（不加 connected，避免拆分 3/5/6/8/9 等数字）
    char colorJson[256];
    snprintf(
        colorJson,
        sizeof(colorJson),
        R"({"lv_color":{"recognition":"ColorMatch","roi":[%d,%d,%d,%d],"method":40,)"
        R"("lower":[0,0,200],"upper":[255,30,255],"count":3}})",
        roi.x,
        roi.y,
        roi.width,
        roi.height);
    MaaRect colorBox;
    if (!doRecognition(context, image, "lv_color", colorJson, &colorBox, nullptr)) {
        return -1;
    }

    // OCR 只识别白色区域
    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"lv":{"recognition":"OCR","roi":[%d,%d,%d,%d],"expected":["\\d+"],"only_rec":true}})",
        colorBox.x - 2,
        colorBox.y - 2,
        colorBox.width + 4,
        colorBox.height + 4);
    auto buffer = MaaStringBufferCreate();
    MaaRect resultBox;
    if (!doRecognition(context, image, "lv", ocrJson, &resultBox, buffer)) {
        MaaStringBufferDestroy(buffer);
        return -1;
    }
    std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
    MaaStringBufferDestroy(buffer);
    auto parsed = json::parse(detail).value_or(json::value { });
    std::string text;
    if (auto& best = parsed["best"]; best.is_object()) {
        text = best["text"].as_string();
    }
    else if (auto& all = parsed["all"]; all.is_array() && !all.empty()) {
        text = all[0]["text"].as_string();
    }
    // 等级只可能 1-8，过滤 OCR 误识别
    int level = parseLevel(text);
    return (level >= 1 && level <= 8) ? level : -1;
}

// 模板匹配，可选是否点击
static bool matchTemplate(
    MaaContext* context,
    const char* recognitionName,
    const char* templatePath,
    const MaaRect& roi,
    bool waitForResult = true,
    bool shouldClick = true)
{
    auto* controller = MaaTaskerGetController(MaaContextGetTasker(context));
    char json[512];
    snprintf(
        json,
        sizeof(json),
        R"({"%s":{"recognition":"TemplateMatch","template":"%s","roi":[%d,%d,%d,%d]}})",
        recognitionName,
        templatePath,
        roi.x,
        roi.y,
        roi.width,
        roi.height);
    MaaRect resultBox;
    if (waitForResult) {
        if (!waitUntilRecognitionSuccess(context, recognitionName, json, &resultBox, nullptr)) {
            return false;
        }
    }
    else {
        ScreenCap capture(context);
        if (!doRecognition(context, capture.img, recognitionName, json, &resultBox, nullptr)) {
            return false;
        }
    }
    if (shouldClick) {
        controller = MaaTaskerGetController(MaaContextGetTasker(context));
        auto clickId = MaaControllerPostClick(controller, resultBox.x + resultBox.width / 2, resultBox.y + resultBox.height / 2);
        MaaControllerWait(controller, clickId);
    }
    return true;
}

// 点击返回按钮
static void clickBackButton(MaaContext* context)
{
    matchTemplate(context, "back", "button/back_btn.png", { 93, 1148, 158, 119 }, false);
    Sleep(500);
}

// 分解 → 确认 → 奖励弹窗
static void decomposeAndConfirm(MaaContext* context)
{
    auto* controller = MaaTaskerGetController(MaaContextGetTasker(context));
    const char* decomposeJson =
        R"({"dec":{"recognition":"TemplateMatch","template":"monster_soul/decompose.png","roi":[262,823,364,262]}})";
    MaaRect decomposeBox;
    if (waitUntilRecognitionSuccess(context, "dec", decomposeJson, &decomposeBox, nullptr)) {
        controller = MaaTaskerGetController(MaaContextGetTasker(context));
        auto clickId =
            MaaControllerPostClick(controller, decomposeBox.x + decomposeBox.width / 2, decomposeBox.y + decomposeBox.height / 2);
        MaaControllerWait(controller, clickId);
    }
    const char* confirmJson = R"({"confirm":{"recognition":"TemplateMatch","template":"button/confirm_btn.png","roi":[379,670,195,115]}})";
    MaaRect confirmBox;
    if (waitUntilRecognitionSuccess(context, "confirm", confirmJson, &confirmBox, nullptr)) {
        controller = MaaTaskerGetController(MaaContextGetTasker(context));
        auto clickId = MaaControllerPostClick(controller, confirmBox.x + confirmBox.width / 2, confirmBox.y + confirmBox.height / 2);
        MaaControllerWait(controller, clickId);
    }
    // 奖励弹窗
    const char* rewardJson = R"({"reward":{"recognition":"OCR","expected":"获得物品","roi":[290,350,125,50]}})";
    if (waitUntilRecognitionSuccess(context, "reward", rewardJson, nullptr, nullptr)) {
        Sleep(500);
        clickRandomTarget(context, 113, 12, 38, 42);
        postWaitFreezes(context, ScreenArea_CenterCol, 500, 3000);
    }

    ScreenCap cap(context);
    if (doRecognition(context, cap.img, "reward", rewardJson, nullptr, nullptr)) {
        Sleep(500);
        clickRandomTarget(context, 113, 12, 38, 42);
        postWaitFreezes(context, ScreenArea_CenterCol, 500, 3000);
    }
}

// ──── 魔魂列表搜索 ────

// 右下角检测是否有 8 级魔魂（表示本页全满级，无需再找）
static bool isPageAllMaxLevel(MaaContext* context, const MaaImageBuffer* image)
{
    char json[256];
    snprintf(
        json,
        sizeof(json),
        R"({"soul":{"recognition":"OCR","roi":[%d,%d,%d,%d],"order_by":"Vertical"}})",
        kLevel8CheckRoi.x,
        kLevel8CheckRoi.y,
        kLevel8CheckRoi.width,
        kLevel8CheckRoi.height);
    auto buffer = MaaStringBufferCreate();
    MaaRect resultBox;
    if (!doRecognition(context, image, "soul", json, &resultBox, buffer)) {
        MaaStringBufferDestroy(buffer);
        return false;
    }
    std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
    MaaStringBufferDestroy(buffer);
    auto parsed = json::parse(detail).value_or(json::value { });
    for (auto& item : parsed["all"].as_array()) {
        std::string text = item["text"].as_string();
        if (text.find("之魂") == std::string::npos) {
            continue;
        }
        int boxX = item["box"][0].as_integer(), boxY = item["box"][1].as_integer();
        int boxW = item["box"][2].as_integer(), boxH = item["box"][3].as_integer();
        MaaRect levelRoi = { boxX + boxW - 20, boxY - 35, 50, 35 };
        if (ocrSingleLevel(context, image, levelRoi) == 8) {
            return true;
        }
    }
    return false;
}

// 可升级魔魂信息
struct SoulEntry
{
    std::string name;
    MaaRect clickBox;
    int gridPosition = 0; // 在 16 格中的位置 (0-15)
};

// 扫描当前页面 16 格区域，从 startGridPos 开始找第一个符合条件的魔魂
static SoulEntry scanPageForTargetSoul(MaaContext* context, const std::set<std::string>& targetNames, int startGridPos)
{
    SoulEntry result;
    ScreenCap capture(context);
    if (startGridPos == -1 && isPageAllMaxLevel(context, capture.img)) {
        return result;
    }

    // OCR 全区域一次
    char json[512];
    snprintf(
        json,
        sizeof(json),
        R"({"g":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})",
        kScanGridRoi.x,
        kScanGridRoi.y,
        kScanGridRoi.width,
        kScanGridRoi.height);
    auto buffer = MaaStringBufferCreate();
    MaaRect resultBox;
    if (!doRecognition(context, capture.img, "g", json, &resultBox, buffer)) {
        MaaStringBufferDestroy(buffer);
        return result;
    }
    std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
    MaaStringBufferDestroy(buffer);
    auto parsed = json::parse(detail).value_or(json::value { });

    // 收集命中目标集合的魔魂
    struct SoulCandidate
    {
        int gridPos;
        std::string name;
        MaaRect box;
        MaaRect levelRoi;
    };

    std::vector<SoulCandidate> candidates;
    for (auto& item : parsed["all"].as_array()) {
        std::string text = item["text"].as_string();
        if (!targetNames.count(text)) {
            continue;
        }
        int boxX = item["box"][0].as_integer(), boxY = item["box"][1].as_integer();
        int boxW = item["box"][2].as_integer(), boxH = item["box"][3].as_integer();
        int gridPos = (boxY - kScanGridRoi.y) / kGridCellHeight * kGridCols + (boxX - kScanGridRoi.x) / kGridCellWidth;
        candidates.push_back({ gridPos, text, { boxX, boxY - 35, boxW, boxH }, { boxX + boxW - 20, boxY - 35, 50, 35 } });
    }
    std::sort(candidates.begin(), candidates.end(), [](const SoulCandidate& a, const SoulCandidate& b) { return a.gridPos < b.gridPos; });

    // 从 startGridPos 开始扫描
    for (auto& candidate : candidates) {
        if (candidate.gridPos < startGridPos) {
            continue;
        }
        int level = ocrSingleLevel(context, capture.img, candidate.levelRoi);
        if (level > 0 && level < 8) {
            result.name = candidate.name;
            result.clickBox = candidate.box;
            result.gridPosition = candidate.gridPos;
            break;
        }
    }
    return result;
}

// 滑动翻页并查找目标魔魂（TODO: isRoiSame 依赖 OpenCV 已暂时移除，恢复后补回"到底检测"）
static SoulEntry findTargetSoul(MaaContext* context, const std::set<std::string>& targetNames, int& startGridPos)
{
    auto* controller = MaaTaskerGetController(MaaContextGetTasker(context));
    for (int safety = 0; safety < 100; ++safety) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return { };
        }
        auto entry = scanPageForTargetSoul(context, targetNames, startGridPos);
        if (!entry.name.empty()) {
            return entry;
        }
        startGridPos = 0;

        controller = MaaTaskerGetController(MaaContextGetTasker(context));
        auto swipeId = MaaControllerPostSwipe(controller, 360, 700, 360, 200, 1000);
        MaaControllerWait(controller, swipeId);
        postWaitFreezes(context);
    }
    return { };
}

// ──── 升级界面操作 ────

static bool enterUpgradeUI(MaaContext* context, SoulEntry& targetSoul)
{
    auto* controller = MaaTaskerGetController(MaaContextGetTasker(context));
    // 点击目标魔魂
    auto clickId = MaaControllerPostClick(
        controller,
        targetSoul.clickBox.x + targetSoul.clickBox.width / 2,
        targetSoul.clickBox.y + targetSoul.clickBox.height / 2);
    MaaControllerWait(controller, clickId);
    postWaitFreezes(context);
    // 点击升级入口
    if (!matchTemplate(context, "upgradeEntry", "monster_soul/upgrade.png", { 474, 828, 155, 286 })) {
        return false;
    }
    postWaitFreezes(context);
    // 等待词条界面出现（不点击）
    if (!matchTemplate(context, "affixView", "monster_soul/affixes_view_icon2.png", { 615, 132, 72, 93 }, true, false)) {
        return false;
    }
    // 检查材料（不点击）
    if (!matchTemplate(context, "warpedCheck", "material/warped_soul.png", { 75, 427, 148, 543 }, false, false)) {
        LogUtils::log("升级魔魂失败，扭动之魂不足", "#ef4444");
        return false;
    }
    return true;
}

static void clickAutoFillButton(MaaContext* context)
{
    matchTemplate(context, "autoFill", "monster_soul/auto_fill.png", { 280, 1109, 160, 161 }, false);
    Sleep(500);
}

static void clickUpgradeButton(MaaContext* context)
{
    matchTemplate(context, "upgradeBtn", "monster_soul/upgrade.png", { 465, 1143, 165, 129 }, false);
    Sleep(500);
}

static int readCurrentLevel(MaaContext* context)
{
    ScreenCap capture(context);
    const char* json = R"({"lv":{"recognition":"OCR","roi":[358,155,65,45]}})";
    return parseLevel(ocrText(context, capture.img, "lv", json, nullptr));
}

static void upgradeToTargetLevel(MaaContext* context, int targetLevel)
{
    int currentLevel = readCurrentLevel(context);
    if (currentLevel >= targetLevel) {
        return;
    }
    int needed = targetLevel - currentLevel;
    for (int i = 0; i < needed; ++i) {
        clickAutoFillButton(context);
    }
    clickUpgradeButton(context);
    postWaitFreezes(context, ScreenArea_TopCenter | ScreenArea_MidCenter, 500, 3000);
    if (readCurrentLevel(context) < targetLevel) {
        upgradeToTargetLevel(context, targetLevel);
    }
}

static double readAffixValue(MaaContext* context, const char* keyword, bool findMinusSign, std::string* outRawText = nullptr)
{
    char json[512];
    snprintf(
        json,
        sizeof(json),
        R"({"af":{"recognition":"OCR","roi":[%d,%d,%d,%d],"order_by":"Vertical"}})",
        kAffixRoi.x,
        kAffixRoi.y,
        kAffixRoi.width,
        kAffixRoi.height);
    ScreenCap capture(context);
    auto buffer = MaaStringBufferCreate();
    MaaRect resultBox;
    if (!doRecognition(context, capture.img, "af", json, &resultBox, buffer)) {
        MaaStringBufferDestroy(buffer);
        return 0;
    }
    std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
    MaaStringBufferDestroy(buffer);
    auto parsed = json::parse(detail).value_or(json::value { });
    std::string fullText;
    for (auto& item : parsed["all"].as_array()) {
        fullText += item["text"].as_string() + "\n";
    }
    if (outRawText) {
        *outRawText = fullText;
    }

    auto keywordPos = fullText.find(keyword);
    if (keywordPos == std::string::npos) {
        return 0;
    }
    std::string after = fullText.substr(keywordPos + strlen(keyword));
    char signChar = findMinusSign ? '-' : '+';
    auto signPos = after.find(signChar);
    if (signPos == std::string::npos) {
        return 0;
    }

    std::string numberStr;
    for (size_t i = signPos + 1; i < after.size(); ++i) {
        if ((after[i] >= '0' && after[i] <= '9') || after[i] == '.') {
            numberStr += after[i];
        }
        else {
            break;
        }
    }
    if (numberStr.empty()) {
        return 0;
    }
    return std::round(std::atof(numberStr.c_str()) * 10.0) / 10.0;
}

// ──── 保留 / 分解 ────
static void handleSoulKept(MaaContext* context, const std::string& soulName, int level)
{
    auto* controller = MaaTaskerGetController(MaaContextGetTasker(context));
    clickBackButton(context);

    if (!matchTemplate(context, "lock", "monster_soul/lock.png", { 78, 761, 297, 378 })) {
        return;
    }
    Sleep(500);

    // 打印最终词条
    const char* json = R"({"finalAffix":{"recognition":"OCR","roi":[239,533,241,426],"order_by":"Vertical"}})";
    ScreenCap capture(context);
    auto buffer = MaaStringBufferCreate();
    MaaRect resultBox;
    std::vector<std::string> attrLines;
    if (doRecognition(context, capture.img, "finalAffix", json, &resultBox, buffer)) {
        std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
        auto parsed = json::parse(detail).value_or(json::value { });
        for (auto& item : parsed["all"].as_array()) {
            std::string text = item["text"].as_string();
            attrLines.push_back(fmt("  %1", text));
        }
    }
    MaaStringBufferDestroy(buffer);

    LogUtils::log(
        fmt("【魔魂满足条件，已锁定】\n魔魂名称：%1\n魔魂等级：%2\n词条属性：\n%3", soulName, level, join(attrLines, "\n")),
        "#22c55e");

    controller = MaaTaskerGetController(MaaContextGetTasker(context));
    auto escId = MaaControllerPostClickKey(controller, 111);
    MaaControllerWait(controller, escId);
    Sleep(300);
}

static void handleSoulUnsatisfied(MaaContext* context, int& decomposedCount)
{
    clickBackButton(context);
    ++decomposedCount;
    decomposeAndConfirm(context);
}

static void decomposeSoulDirectly(MaaContext* context, SoulEntry& targetSoul, int& decomposedCount)
{
    auto* controller = MaaTaskerGetController(MaaContextGetTasker(context));
    auto clickId = MaaControllerPostClick(
        controller,
        targetSoul.clickBox.x + targetSoul.clickBox.width / 2,
        targetSoul.clickBox.y + targetSoul.clickBox.height / 2);
    MaaControllerWait(controller, clickId);
    postWaitFreezes(context);
    ++decomposedCount;
    decomposeAndConfirm(context);
}

// ──── 条件解析 ────

struct AffixCheckConfig
{
    std::string ocrKeyword; // OCR 识别关键词（如 "行动间隔"）
    bool readMinusSign;     // 读减号(true)还是加号(false)
    int requiredLevel;      // 出现该词条需要的等级
};

struct ConditionRule
{
    AffixCheckConfig check;
    double threshold;
    std::string displayName; // UI 名称（如 "间隔"），用于匹配魔魂类型
    bool hasPercent = false; // 配置中是否带 %，e.g. 间隔>=3.0% → true, 抗性>=3.0 → false
};

using ConditionGroup = std::map<int, ConditionRule>; // key=检查等级, value=条件规则

static AffixCheckConfig getAffixCheckConfig(const std::string& name)
{
    if (name == "间隔") {
        return { "行动间隔", true, 4 };
    }
    if (name == "暴击") {
        return { "暴击率", false, 6 };
    }
    if (name == "承伤") {
        return { "承受伤害", true, 8 };
    }
    if (name == "抗性") {
        return { "抗性", false, 8 };
    }
    if (name == "治疗效果") {
        return { "治疗效果", false, 8 };
    }
    return { };
}

// 魔魂词条分组
static const std::map<std::string, std::set<std::string>> kSoulAvailableAffixes = {
    // 组A: 间隔(Lv4) + 暴击(Lv6) — 14个
    { "巫蛊之魂", { "间隔", "暴击" } },
    { "暴虐之魂", { "间隔", "暴击" } },
    { "凶煞之魂", { "间隔", "暴击" } },
    { "邪典之魂", { "间隔", "暴击" } },
    { "劫掠之魂", { "间隔", "暴击" } },
    { "狡智之魂", { "间隔", "暴击" } },
    { "雄狮之魂", { "间隔", "暴击" } },
    { "狂怒之魂", { "间隔", "暴击" } },
    { "霜雪之魂", { "间隔", "暴击" } },
    { "林动之魂", { "间隔", "暴击" } },
    { "极寒之魂", { "间隔", "暴击" } },
    { "沉渊之魂", { "间隔", "暴击" } },
    { "狂热之魂", { "间隔", "暴击" } },
    { "天陨之魂", { "间隔", "暴击" } },
    // 组B: 间隔(Lv4) + 承伤(Lv8) — 3个
    { "野性之魂", { "间隔", "承伤" } },
    { "蛮荒之魂", { "间隔", "承伤" } },
    { "冰晶之魂", { "间隔", "承伤" } },
    // 组C: 间隔(Lv4) + 治疗效果(Lv8) — 3个
    { "林野之魂", { "间隔", "治疗效果" } },
    { "爆破之魂", { "间隔", "治疗效果" } },
    { "先祖之魂", { "间隔", "治疗效果" } },
};

// 找到该魔魂第一个完全支持的条件组
static const ConditionGroup* findMatchingConditionGroup(const std::string& soulName, const std::vector<ConditionGroup>& conditionGroups)
{
    auto affixIt = kSoulAvailableAffixes.find(soulName);
    if (affixIt == kSoulAvailableAffixes.end()) {
        return nullptr;
    }
    const auto& availableAffixes = affixIt->second;
    for (const auto& group : conditionGroups) {
        bool allSupported = true;
        for (const auto& [level, rule] : group) {
            if (!rule.displayName.empty() && !availableAffixes.count(rule.displayName)) {
                allSupported = false;
                break;
            }
        }
        if (allSupported) {
            return &group;
        }
    }
    return nullptr;
}

// ──── 单次升级流程 ────
static bool doOneUpgrade(
    MaaContext* context,
    const std::set<std::string>& targetSoulNames,
    const std::vector<ConditionGroup>& conditionGroups,
    int& upgradeCount,
    int& keepCount,
    int& decomposeCount,
    int& startGridPos)
{
    auto targetSoul = findTargetSoul(context, targetSoulNames, startGridPos);
    if (targetSoul.name.empty()) {
        return false;
    }

    const ConditionGroup* matchedGroup = findMatchingConditionGroup(targetSoul.name, conditionGroups);
    if (!matchedGroup) {
        LogUtils::log("当前魔魂升级后不可能满足配置的保留属性", "#f59e0b");
        decomposeSoulDirectly(context, targetSoul, decomposeCount);
        startGridPos = targetSoul.gridPosition; // 分解后不移动位置
        upgradeCount++;
        return true;
    }

    if (!enterUpgradeUI(context, targetSoul)) {
        return false;
    }

    // 逐级升级并校验条件
    bool allConditionsPassed = true;
    for (const auto& [level, rule] : *matchedGroup) {
        upgradeToTargetLevel(context, level);
        std::string rawText;
        double actualValue = readAffixValue(context, rule.check.ocrKeyword.c_str(), rule.check.readMinusSign, &rawText);
        if (actualValue < rule.threshold) {
            const char* suffix = rule.hasPercent ? "%" : "";

            std::vector<std::string> attrLines;
            std::vector<std::string> rawLines = split(rawText, '\n');
            for (const auto& line : rawLines) {
                if (line.find("：") != std::string::npos) {
                    continue;
                }
                attrLines.push_back(fmt("  %1", trim(line)));
            }

            LogUtils::log(
                fmt("【魔魂不满足条件，已分解】\n魔魂名称:%1\n魔魂等级:%2\n词条要求:%3>=%4%7,当前%3:%5%7\n词条属性:\n%6",
                    targetSoul.name,
                    level,
                    rule.displayName,
                    rule.threshold,
                    actualValue,
                    join(attrLines, "\n"),
                    suffix),
                "#f59e0b");
            allConditionsPassed = false;
            break;
        }
    }

    if (!allConditionsPassed) {
        handleSoulUnsatisfied(context, decomposeCount);
        startGridPos = targetSoul.gridPosition;
    }
    else {
        upgradeToTargetLevel(context, 8);
        handleSoulKept(context, targetSoul.name, 8);
        keepCount++;
        startGridPos = targetSoul.gridPosition + 1; // 保留后跳过
    }
    upgradeCount++;
    return true;
}

static void getTargetSoul(std::set<std::string>& targetSoulNames)
{
    for (const auto& [field, cn] : soulFlags) {
        if (g_user_soul.*field) {
            targetSoulNames.insert(cn);
        }
    }
    if (targetSoulNames.empty()) {
        targetSoulNames = allSoulNames;
    }
}

static void getConditionGroups(std::vector<ConditionGroup>& conditionGroups)
{
    auto addCondition = [&](ConditionGroup& group, bool checked, const char* affix, double threshold, bool hasPercent = true) {
        if (!checked) {
            return;
        }
        auto cfg = getAffixCheckConfig(affix);
        if (!cfg.ocrKeyword.empty()) {
            group[cfg.requiredLevel] = { cfg, threshold, affix, hasPercent };
        }
    };

    ConditionGroup g1;
    if (g_user_soul.retention_group1_enabled) {
        addCondition(g1, g_user_soul.g1_interval, "间隔", g_user_soul.group1_interval);
        addCondition(g1, g_user_soul.g1_damage, "承伤", g_user_soul.group1_damage);
        addCondition(g1, g_user_soul.g1_resist, "抗性", g_user_soul.group1_resist, false);
    }
    if (!g1.empty()) {
        conditionGroups.push_back(g1);
    }

    ConditionGroup g2;
    if (g_user_soul.retention_group2_enabled) {
        addCondition(g2, g_user_soul.g2_interval, "间隔", g_user_soul.group2_interval);
        addCondition(g2, g_user_soul.g2_crit, "暴击", g_user_soul.group2_crit);
    }
    if (!g2.empty()) {
        conditionGroups.push_back(g2);
    }

    ConditionGroup g3;
    if (g_user_soul.retention_group3_enabled) {
        addCondition(g3, g_user_soul.g3_interval, "间隔", g_user_soul.group3_interval);
        addCondition(g3, g_user_soul.g3_heal, "治疗效果", g_user_soul.group3_heal);
    }
    if (!g3.empty()) {
        conditionGroups.push_back(g3);
    }
}

MaaBool upgradeMonsterSoul(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    if (g_user_soul.countMode == -1) {
        LogUtils::log("当前服务器未配置魔魂升级策略,在启动游戏任务中可进行配置", "#ef4444");
        return false;
    }
    int maxUpgradeCount = (g_user_soul.count != 0) ? g_user_soul.count : 10;

    std::set<std::string> targetSoulNames;
    getTargetSoul(targetSoulNames);

    // 保留条件组（组间 OR)
    std::vector<ConditionGroup> conditionGroups;
    getConditionGroups(conditionGroups);

    int upgradeCount = 0, keepCount = 0, decomposeCount = 0;
    int startGridPos = -1;
    int round = 0;
    while (true) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            break;
        }
        if (g_user_soul.countMode == 1 && round >= maxUpgradeCount) {
            break;
        }
        std::string progress = (g_user_soul.countMode == 0) ? fmt("第%1次", round + 1) : fmt("[%1/%2]", round + 1, maxUpgradeCount);
        LogUtils::log(fmt("魔魂升级%1", progress), "#3b82f6");
        if (!doOneUpgrade(context, targetSoulNames, conditionGroups, upgradeCount, keepCount, decomposeCount, startGridPos)) {
            break;
        }
        round++;
    }

    LogUtils::log(fmt("魔魂升级完毕: 共升级%1个,保留%2个,分解%3个", upgradeCount, keepCount, decomposeCount), "#22c55e");
    return true;
}

static bool readBagCount(MaaContext* context, int& x, int& y)
{
    const char* json = R"({"count":{"recognition":"OCR","roi":[262,16,103,42],"expected":"\\d+/\\d+","only_rec":true}})";
    auto buf = MaaStringBufferCreate();
    MaaRect box;
    ScreenCap cap(context);
    if (!doRecognition(context, cap.img, "count", json, &box, buf)) {
        MaaStringBufferDestroy(buf);
        return false;
    }
    std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);
    auto parsed = json::parse(detail).value_or(json::value { });
    // 合并所有OCR结果，避免"150/200"被拆成"150"和"/200"
    std::string text;
    for (auto& item : parsed["all"].as_array()) {
        text += item["text"].as_string();
    }
    auto p = text.find('/');
    if (p == std::string::npos) {
        return false;
    }
    x = std::atoi(text.substr(0, p).c_str());
    y = std::atoi(text.substr(p + 1).c_str());
    return true;
}

// ──── 背包魔魂已满时升级 ────
MaaBool checkBagAndUpgradeSoul(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    int x = 0, y = 0;
    if (!readBagCount(context, x, y)) {
        LogUtils::log("读取背包魔魂数量失败", "#ef4444");
        return false;
    }
    LogUtils::log(fmt("背包魔魂数量: %1/%2", x, y), "#3b82f6");

    while (x >= y) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return false;
        }

        int need = x - y + 1;
        LogUtils::log(fmt("背包已满(%1/%2)，升级%3个魔魂", x, y, need), "#f59e0b");

        auto defaultCfg = getDefaultSoulUpgradeConfig();
        std::string conditionsJson = defaultCfg["conditions"].to_string();
        char param[1024];
        snprintf(
            param,
            sizeof(param),
            R"({"count":%d,"countMode":1,"decompose":true,"mode":0,"souls":[],"conditions":%s})",
            need,
            conditionsJson.c_str());
        upgradeMonsterSoul(context, taskId, nodeName, customActionName, param, recoId, box, transArg);

        if (!readBagCount(context, x, y)) {
            LogUtils::log("读取背包数量失败", "#ef4444");
            break;
        }
    }

    return true;
}

void registerCustomMonsterSoulUpgrade(MaaResource* resource, void* userData)
{
    registerCustomAction(resource, "UpgradeMonsterSoul", upgradeMonsterSoul, userData);
    registerCustomAction(resource, "CheckBagAndUpgradeSoul", checkBagAndUpgradeSoul, userData);
}
