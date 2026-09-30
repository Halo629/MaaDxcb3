#include "Comm.h"

// 魔魂列表 4×4 扫描区域
static const MaaRect scanGridRoi = { 45, 140, 630, 825 };
static constexpr int gridCellWidth = 157;  // 630/4
static constexpr int gridCellHeight = 206; // 825/4

// 魔魂词条分组
static const std::map<std::string, std::set<std::string>> soulAvailableAffixes = {
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

// 可升级魔魂信息
struct TargetSoulInfo
{
    std::string name;
    MaaRect clickBox;
    int gridPosition = 0; // 在 16 格中的位置 (0-15)
};

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

static int g_maxUpgradeCount = 0;
static int g_startGridPos = -1;
static bool g_mongsterSoulFullFlag = false;
static int g_keepCount = 0;
static int g_decomposeCount = 0;
static int g_curRound = 1;
static std::set<std::string> g_targetSoulNames;
using ConditionGroup = std::map<int, ConditionRule>; // key=检查等级, value=条件规则
static std::vector<ConditionGroup> g_conditionGroups;

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

// 右下角检测是否有 8 级魔魂（表示本页全满级，无需再找）
static bool isPageAllMaxLevel(MaaContext* context, const MaaImageBuffer* image)
{
    auto buffer = MaaStringBufferCreate();
    MaaRect resultBox;
    if (!doRecognition(context, image, "CurPageHasCanUpgradeMonsterSoulCheck", { }, &resultBox, buffer)) {
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

// 扫描当前页面 16 格区域，从 startGridPos 开始找第一个符合条件的魔魂
static bool scanPageForTargetSoul(MaaContext* context, TargetSoulInfo& targetSoulInfo)
{
    ScreenCap capture(context);
    if (g_startGridPos == -1 && isPageAllMaxLevel(context, capture.img)) {
        return false;
    }

    // 当前页面4*4格子逐个扫描
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            int gridPos = i * 4 + j;
            if (gridPos < g_startGridPos) {
                continue;
            }

            MaaRect cellRoi = {
                scanGridRoi.x + j * gridCellWidth,
                scanGridRoi.y + i * gridCellHeight,
                gridCellWidth,
                gridCellHeight,
            };
            char detailJson[128];
            snprintf(
                detailJson,
                sizeof(detailJson),
                R"({"CurPageGridOcr":{"roi":[%d,%d,%d,%d]}})",
                cellRoi.x,
                cellRoi.y,
                cellRoi.width,
                cellRoi.height);

            auto buffer = MaaStringBufferCreate();
            MaaRect resultBox;
            if (!doRecognition(context, capture.img, "CurPageGridOcr", detailJson, &resultBox, buffer)) {
                MaaStringBufferDestroy(buffer);
                continue;
            }
            std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
            MaaStringBufferDestroy(buffer);
            auto parsed = json::parse(detail).value_or(json::value { });

            for (auto& item : parsed["all"].as_array()) {
                std::string text = item["text"].as_string();
                if (!g_targetSoulNames.count(text)) {
                    continue;
                }
                int boxX = item["box"][0].as_integer(), boxY = item["box"][1].as_integer();
                int boxW = item["box"][2].as_integer(), boxH = item["box"][3].as_integer();
                int level = ocrSingleLevel(context, capture.img, { boxX + boxW - 20, boxY - 35, 50, 35 });
                if (level > 0 && level < 8) {
                    targetSoulInfo.name = text;
                    targetSoulInfo.clickBox = { boxX, boxY - 35, boxW, boxH };
                    targetSoulInfo.gridPosition = gridPos;
                    return true;
                }
            }
        }
    }
    return false;
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

// 找到该魔魂第一个完全支持的条件组
static const ConditionGroup* findMatchingConditionGroup(const std::string& soulName)
{
    auto affixIt = soulAvailableAffixes.find(soulName);
    if (affixIt == soulAvailableAffixes.end()) {
        return nullptr;
    }
    const auto& availableAffixes = affixIt->second;
    for (const auto& group : g_conditionGroups) {
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

static bool readBagCount(MaaContext* context, int& curCount, int& capacity)
{
    auto buf = MaaStringBufferCreate();
    ScreenCap cap(context);
    if (!doRecognition(context, cap.img, "MonsterSoulGetCurSoulCount", "{}", nullptr, buf)) {
        MaaStringBufferDestroy(buf);
        return false;
    }
    std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);
    auto parsed = json::parse(detail).value_or(json::value { });
    std::string text = parsed["best"]["text"].as_string();

    auto p = text.find('/');
    if (p == std::string::npos) {
        return false;
    }
    curCount = std::atoi(text.substr(0, p).c_str());
    capacity = std::atoi(text.substr(p + 1).c_str());
    return true;
}

// 通过指定 entry 识别当前页面的魔魂名称
static bool getSoulName(MaaContext* context, const char* entry, std::string& soulName)
{
    ScreenCap cap(context);
    auto buf = MaaStringBufferCreate();
    if (!doRecognition(context, cap.img, entry, "{}", nullptr, buf)) {
        MaaStringBufferDestroy(buf);
        return false;
    }
    std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);
    auto parsed = json::parse(detail).value_or(json::value { });
    soulName = parsed["best"]["text"].as_string();
    return true;
}

// 通过指定 entry 识别当前页面的魔魂等级，识别失败返回 -1
static int getSoulLevel(MaaContext* context, const char* entry)
{
    ScreenCap cap(context);
    auto buf = MaaStringBufferCreate();
    if (!doRecognition(context, cap.img, entry, "{}", nullptr, buf)) {
        MaaStringBufferDestroy(buf);
        return -1;
    }
    std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);
    auto parsed = json::parse(detail).value_or(json::value { });
    return parseLevel(parsed["best"]["text"].as_string());
}

static bool upgradeToTargetLevel(MaaContext* context, int targetLevel)
{
    // 检查扭动之魂
    ScreenCap cap(context);
    if (!doRecognition(context, cap.img, "MonsterSoulUpgradeCheckWarpedSoul", "{}", nullptr, nullptr)) {
        LogUtils::log("升级魔魂失败，扭动之魂不足", "#ef4444");
        return false;
    }
    int currentLevel = getSoulLevel(context, "UpgradeMonsterSoulUpgradePageSoulLevel");
    if (currentLevel >= targetLevel) {
        return true;
    }
    int needed = targetLevel - currentLevel;

    MaaContextRunTask(
        context,
        "MonsterSoulOneKeyFillAndUpgrade",
        fmt(R"({"MonsterSoulOneKeyFillAndUpgrade":{"repeat":%1}})", needed).c_str());

    // 检查是否升级到指定等级(点击升级后画面比较卡)
    while (true) {
        ScreenCap cap(context);
        int newLevel = getSoulLevel(context, "UpgradeMonsterSoulUpgradePageSoulLevel");
        if (newLevel >= targetLevel) {
            return true;
        }
        else if (newLevel == currentLevel) {
            Sleep(500);
        }
        else {
            // 升级后等级有变化，但是没达到目标等级，可能是扭动之魂不足
            if (!doRecognition(context, cap.img, "MonsterSoulUpgradeCheckWarpedSoul", "{}", nullptr, nullptr)) {
                LogUtils::log("升级魔魂失败，扭动之魂不足", "#ef4444");
                return false;
            }
            else {
                // 有扭动之魂，可能是点击次数不够
                upgradeToTargetLevel(context, targetLevel);
            }
        }
    }
    return true;
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
    if (MaaTaskerStopping(MaaContextGetTasker(context))) {
        return true;
    }

    LogUtils::log(fmt("魔魂升级第%1次", g_curRound++), "#3b82f6");

    // 魔魂名称
    std::string soulName;
    if (!getSoulName(context, "UpgradeMonsterSoulUpgradePageSoulName", soulName) || !g_targetSoulNames.count(soulName)) {
        return false;
    }

    // 魔魂等级
    int curLevel = getSoulLevel(context, "UpgradeMonsterSoulUpgradePageSoulLevel");
    if (curLevel < 1 || curLevel >= 8) {
        return false;
    }

    const ConditionGroup* matchedGroup = findMatchingConditionGroup(soulName);
    if (!matchedGroup) {
        LogUtils::log("当前魔魂升级后不可能满足配置的保留条件,分解", "#f59e0b");
        ++g_decomposeCount;
        MaaContextRunTask(context, "MonsterSoulUpgradePageReturnAndDecompose", "{}");
        return true;
    }

    // 逐级升级并校验条件
    bool allConditionsPassed = true;
    std::string failLog;
    std::vector<std::string> finalAttrLines;
    int finalLevel = 0;
    for (const auto& [level, rule] : *matchedGroup) {
        bool res = upgradeToTargetLevel(context, level);
        if (!res) {
            return false;
        }
        finalLevel = level;
        std::string rawText;
        double actualValue = readAffixValue(context, rule.check.ocrKeyword.c_str(), rule.check.readMinusSign, &rawText);

        std::vector<std::string> attrLines;
        std::vector<std::string> rawLines = split(rawText, '\n');
        for (const auto& line : rawLines) {
            if (line.find("：") != std::string::npos) {
                continue;
            }
            attrLines.push_back(fmt("  %1", trim(line)));
        }
        finalAttrLines = attrLines;

        if (actualValue < rule.threshold) {
            const char* suffix = rule.hasPercent ? "%" : "";
            failLog =
                fmt("【魔魂不满足保留条件，已分解】\n魔魂名称:%1\n魔魂等级:%2\n词条要求:%3>=%4%7,当前%3:%5%7\n词条属性:\n%6",
                    soulName,
                    level,
                    rule.displayName,
                    rule.threshold,
                    actualValue,
                    join(attrLines, "\n"),
                    suffix);
            allConditionsPassed = false;
            break;
        }
    }

    if (!allConditionsPassed) {
        ++g_decomposeCount;
        MaaContextRunTask(context, "MonsterSoulUpgradePageReturnAndDecompose", "{}");
        LogUtils::log(failLog, "#f59e0b");
    }
    else {
        ++g_keepCount;
        ++g_startGridPos;
        MaaContextRunTask(context, "MonsterSoulUpgradePageReturnAndLock", "{}");
        LogUtils::log(
            fmt("【魔魂满足条件，已锁定】\n魔魂名称:%1\n魔魂等级:%2\n词条属性:\n%3", soulName, finalLevel, join(finalAttrLines, "\n")),
            "#22c55e");
    }
    return true;
}

// 魔魂升级配置
MaaBool monsterSoulUpgradeConfigCheck(
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

    g_startGridPos = -1;
    g_keepCount = 0;
    g_decomposeCount = 0;
    g_curRound = 1;
    g_maxUpgradeCount = g_user_soul.count ? g_user_soul.count : INT_MAX;
    auto attach = getNodeAttach(context, nodeName);
    g_mongsterSoulFullFlag = attach.get("monster_soul_full_flag", json::value(false)).as_boolean();

    getTargetSoul(g_targetSoulNames);
    // 保留条件组（组间 OR)
    getConditionGroups(g_conditionGroups);
    return true;
}

MaaBool findTargetMonsterSoul(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    // 背包魔魂已满时，升级到背包容量以下就停止
    if (g_mongsterSoulFullFlag) {
        int curCount = 0;
        int capacity = 0;
        readBagCount(context, curCount, capacity);
        if (g_startGridPos == -1 && curCount >= capacity) {
            LogUtils::log(fmt("背包魔魂已满(%1/%2)，开始升级魔魂", curCount, capacity), "#3b82f6");
        }
        if (curCount < capacity) {
            return false;
        }
    }
    int cnt = 0;
    while (true) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return false;
        }

        auto* controller = MaaTaskerGetController(MaaContextGetTasker(context));
        TargetSoulInfo targetSoulInfo;
        auto find = scanPageForTargetSoul(context, targetSoulInfo);
        if (!find) {
            MaaContextRunTask(context, "MonsterSoulPageSwipeUp", "{}");
            if (g_startGridPos != -1) {
                g_startGridPos = 0;
                if (++cnt >= 3) {
                    // 连续滑动三次没找到退出
                    return false;
                }
            }
            continue;
        }
        g_startGridPos = targetSoulInfo.gridPosition;
        outBox->x = targetSoulInfo.clickBox.x;
        outBox->y = targetSoulInfo.clickBox.y;
        outBox->width = targetSoulInfo.clickBox.width;
        outBox->height = targetSoulInfo.clickBox.height;
        return true;
    }
}

// 魔魂升级前做二次检查
MaaBool upgradeMonsterSoulCheck(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    ScreenCap cap(context);
    // 魔魂检查
    std::string soulName;
    if (!getSoulName(context, "UpgradeMonsterTargetSoulCheck", soulName) || !g_targetSoulNames.count(soulName)) {
        return false;
    }

    // 等级检查
    int curLevel = getSoulLevel(context, "UpgradeMonsterSoulLevelCheck");
    if (curLevel < 1 || curLevel >= 8) {
        return false;
    }
    // 魔魂已上锁检查
    if (doRecognition(context, cap.img, "UpgradeMonsterSoulLockedCheck", "{}", nullptr, nullptr)) {
        return false;
    }

    return true;
}

MaaBool monsterSoulUpgradeFinish(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    LogUtils::log(
        fmt("魔魂升级完成，共升级%1个魔魂，保留%2个，分解%3个", g_decomposeCount + g_keepCount, g_keepCount, g_decomposeCount),
        "#22c55e");
    return true;
}

void registerCustomMonsterSoulUpgrade(MaaResource* resource, void* userData)
{
    registerCustomAction(resource, "UpgradeMonsterSoul", upgradeMonsterSoul, userData);
    registerCustomAction(resource, "MonsterSoulUpgradeConfigCheck", monsterSoulUpgradeConfigCheck, userData);
    registerCustomAction(resource, "MonsterSoulUpgradeFinish", monsterSoulUpgradeFinish, userData);
    registerCustomRecognition(resource, "UpgradeMonsterSoulCheck", upgradeMonsterSoulCheck, userData);
    registerCustomRecognition(resource, "FindTargetMonsterSoul", findTargetMonsterSoul, userData);
}
