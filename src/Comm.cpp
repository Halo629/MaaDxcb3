#include "Comm.h"
#include <algorithm>
#include <fstream>

const int one_second_delay = 1000;
const int two_second_delay = 2000;

ScreenCap::ScreenCap(MaaContext* c)
    : img(MaaImageBufferCreate())
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(c));
    auto id = MaaControllerPostScreencap(ctrl);
    MaaControllerWait(ctrl, id);
    MaaControllerCachedImage(ctrl, img);
}

ScreenCap::~ScreenCap()
{
    MaaImageBufferDestroy(img);
}

bool doRecognition(
    MaaContext* context,
    const MaaImageBuffer* img,
    const char* entry,
    const char* detail_json,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    auto* tasker = MaaContextGetTasker(context);
    auto id = MaaContextRunRecognition(context, entry, detail_json ? detail_json : "{}", img);
    if (id == MaaInvalidId) {
        return false;
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, out_box, out_detail, nullptr, nullptr);
    return hit;
}

bool waitUntilRecognitionSuccess(
    MaaContext* context,
    const char* entry,
    const char* detail_json,
    MaaRect* out_box,
    MaaStringBuffer* out_detail,
    int timeoutMs)
{
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        ScreenCap cap(context);
        if (doRecognition(context, cap.img, entry, detail_json, out_box, out_detail)) {
            return true;
        }
        Sleep(one_second_delay);
        elapsed += one_second_delay;
    }
    return false;
}

void clickRandomTarget(MaaContext* context, int x, int y, int w, int h)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    int rx = x + rand() % w;
    int ry = y + rand() % h;
    auto id = MaaControllerPostClick(ctrl, rx, ry);
    MaaControllerWait(ctrl, id);
}

std::string ocrText(MaaContext* context, const MaaImageBuffer* img, const char* entry, const char* detailJson, MaaRect* box)
{
    auto detailBuf = MaaStringBufferCreate();
    if (!doRecognition(context, img, entry, detailJson, box, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto j = json::parse(detail);
    if (!j || !j->contains("all")) {
        return { };
    }
    std::string result;
    for (auto& item : (*j)["all"].as_array()) {
        result += item["text"].as_string();
    }
    return result;
}

// 服务器名归一化：去掉冒号（中英文）和空格/制表符，统一 OCR 结果避免符号差异
std::string normalizeServerName(const std::string& raw)
{
    std::string s = raw;
    // 去掉中文冒号（UTF-8 三字节）
    size_t pos;
    while ((pos = s.find("\xEF\xBC\x9A")) != std::string::npos) {
        s.erase(pos, 3);
    }
    // 去掉英文冒号和空白
    std::string result;
    for (char c : s) {
        if (c == ':' || c == ' ' || c == '\t') {
            continue;
        }
        result += c;
    }
    return result;
}

bool waitArriveMap(MaaContext* context)
{
    return waitUntilRecognitionSuccess(context, "InMap", "{}", nullptr, nullptr);
}

std::string getMapOfRole(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    MaaRect roleBox;
    auto id = MaaContextRunRecognition(context, "RolePos", "{}", cap.img);
    if (id == MaaInvalidId) {
        return { };
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &roleBox, nullptr, nullptr, nullptr);
    if (!hit) {
        return { };
    }
    if (outBox) {
        *outBox = roleBox;
    }
    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"mapName":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})",
        roleBox.x - 250,
        roleBox.y - 10,
        250,
        80);
    auto buffer = MaaStringBufferCreate();
    MaaRect ocrBox;
    id = MaaContextRunRecognition(context, "mapName", ocrJson, cap.img);
    if (id == MaaInvalidId) {
        MaaStringBufferDestroy(buffer);
        return { };
    }
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &ocrBox, buffer, nullptr, nullptr);
    if (!hit) {
        MaaStringBufferDestroy(buffer);
        return { };
    }
    std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
    MaaStringBufferDestroy(buffer);
    auto j = json::parse(detail).value_or(json::value { });
    if (auto& best = j["best"]; best.is_object()) {
        return best["text"].as_string();
    }
    return { };
}

std::string getMapOfFerdinand(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    auto buf = MaaStringBufferCreate();
    MaaRect box;
    std::string result;
    if (doRecognition(context, cap.img, "MapOfFerdinand", "{}", outBox ? outBox : &box, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
        // MapOfFerdinand 是 And 识别，detail 是子结果数组，取第二个（OCR）的文本
        if (j.is_array() && j.as_array().size() >= 2) {
            auto& ocrDetail = j.as_array()[1]["detail"];
            if (auto& best = ocrDetail["best"]; best.is_object()) {
                result = best["text"].as_string();
            }
            else if (auto& all = ocrDetail["all"]; all.is_array() && !all.empty()) {
                result = all[0]["text"].as_string();
            }
        }
    }
    MaaStringBufferDestroy(buf);
    return result;
}

json::value getCurMapList(MaaContext* context)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    auto detailBuf = MaaStringBufferCreate();
    MaaRect outBox;
    auto id = MaaContextRunRecognition(context, "CurMapList", "{}", cap.img);
    if (id == MaaInvalidId) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &outBox, detailBuf, nullptr, nullptr);
    if (!hit) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    std::string detailStr(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto parsed = json::parse(detailStr);
    if (!parsed || !parsed->exists("all")) {
        return { };
    }
    return (*parsed)["all"];
}

void exitToCampfire(MaaContext* context)
{
    MaaContextRunTask(context, "SceneEnterMainPage", "{}");
}

bool enterMapByName(MaaContext* context, const std::string& mapName)
{
    auto entries = getCurMapList(context);
    if (!entries.is_array()) {
        return false;
    }
    for (auto& item : entries.as_array()) {
        std::string text = item["text"].as_string();
        if (text.find(mapName) != std::string::npos) {
            auto& box = item.as_object().at("box");
            int bx = box[0].as_integer(), by = box[1].as_integer();
            clickRandomTarget(context, bx, by, box[2].as_integer(), box[3].as_integer());
            waitArriveMap(context);
            return true;
        }
    }
    return false;
}

void clickCampfire(MaaContext* context)
{
    constexpr int kMaxRetries = 3;
    constexpr int kRetryDelayMs = 3000;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        MaaContextRunTask(context, "ClickCampFire", "{}");
        if (waitUntilRecognitionSuccess(context, "InSelectDestinationPage", "{}", nullptr, nullptr, 3000)) {
            return;
        }
        if (attempt < kMaxRetries - 1) {
            Sleep(kRetryDelayMs);
        }
    }
}

void setNextNode(MaaContext* context, const char* nodeName, const char* nextNode)
{
    auto nextList = MaaStringListBufferCreate();
    auto nextBuf = MaaStringBufferCreate();
    MaaStringBufferSet(nextBuf, nextNode);
    MaaStringListBufferAppend(nextList, nextBuf);
    MaaStringBufferDestroy(nextBuf);
    MaaContextOverrideNext(context, nodeName, nextList);
    MaaStringListBufferDestroy(nextList);
}

// ──── 食谱配置（硬编码） ────

static std::map<std::string, std::vector<std::string>> s_recipeConfig;
static std::unordered_set<std::string> s_recipeNames;

void loadRecipeConfig()
{
    if (!s_recipeConfig.empty()) {
        return;
    }
    s_recipeConfig = {
        { "黏糊糊的食物", { "肉", "水果" } },
        { "面包", { "麦粉", "发酵粉" } },
        { "混合蔬菜浓汤", { "野菜", "干净的水", "盐" } },
        { "果汁", { "水果", "干净的水" } },
        { "腌菜", { "野菜", "盐" } },
        { "炸鱼", { "鱼", "橄榄油" } },
        { "蘑菇浓汤", { "野菜", "干净的水" } },
        { "炖肉", { "肉", "干净的水" } },
        { "虾饼", { "麦粉", "虾" } },
        { "腌制鱼肉香肠", { "鱼", "盐" } },
        { "鱼汤", { "鱼", "干净的水" } },
        { "野菜沙拉", { "野菜", "乳酪" } },
        { "鸟肉串", { "肉", "野菜" } },
        { "肉丸子", { "麦粉", "肉" } },
        { "风干香肠", { "肉", "鱼", "盐" } },
        { "饼干", { "麦粉", "牛奶" } },
        { "蒸虾", { "虾", "盐" } },
        { "肉酱", { "肉", "盐" } },
        { "鱼肉沙拉", { "鱼", "乳酪" } },
        { "水果布丁", { "水果", "干净的水", "牛奶" } },
        { "发酵饮品", { "麦粉", "发酵粉", "干净的水" } },
        { "炒野菜", { "肉", "野菜", "盐" } },
        { "水果酿", { "水果", "发酵粉" } },
        { "荧果肉酱", { "肉", "盐", "荧果" } },
        { "炸鸡", { "肉", "发酵粉", "盐" } },
        { "胡萝卜面包", { "麦粉", "野菜" } },
        { "胡萝卜汁", { "砂糖", "野菜" } },
        { "水果馅饼", { "麦粉", "水果", "盐" } },
        { "鸟肉馅饼", { "麦粉", "肉", "盐" } },
        { "牛奶面包", { "麦粉", "发酵粉", "牛奶" } },
        { "煎肉排", { "肉", "橄榄油" } },
        { "乳酪馅饼", { "麦粉", "乳酪" } },
        { "鲜虾野菜汤", { "野菜", "虾", "干净的水" } },
        { "烤鸟肉沙拉", { "肉", "水果", "乳酪" } },
        { "海鲜浓汤", { "鱼", "干净的水", "盐" } },
        { "鱼肉馅饼", { "麦粉", "鱼", "发酵粉" } },
        { "海陆烤肉拼盘", { "肉", "鱼" } },
        { "乱炖", { "肉", "乳酪" } },
    };
    for (auto& [name, _] : s_recipeConfig) {
        s_recipeNames.insert(name);
    }
}

const std::unordered_set<std::string>& getRecipeNames()
{
    loadRecipeConfig();
    return s_recipeNames;
}

const std::map<std::string, std::vector<std::string>>& getRecipeConfig()
{
    loadRecipeConfig();
    return s_recipeConfig;
}

void switchMapSort(MaaContext* context, const std::string& targetOrder)
{
    auto* tasker = MaaContextGetTasker(context);
    ScreenCap cap(context);
    auto buffer = MaaStringBufferCreate();
    MaaRect sortBox;
    auto id = MaaContextRunRecognition(context, "MapListSortCheck", "{}", cap.img);
    if (id != MaaInvalidId) {
        MaaBool hit = false;
        MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &sortBox, buffer, nullptr, nullptr);
        if (hit) {
            std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
            auto j = json::parse(detail).value_or(json::value { });
            std::string text = j["best"]["text"].as_string();
            if (text.find(targetOrder) == std::string::npos) {
                clickRandomTarget(context, sortBox.x, sortBox.y, sortBox.width, sortBox.height);
                postWaitFreezes(context, ScreenArea_MidCenter, 300);
            }
        }
    }
    MaaStringBufferDestroy(buffer);
}

void postWaitFreezes(MaaContext* context, uint32_t areaMask, int waitTime, int timeout)
{
    char screen[64];
    snprintf(screen, sizeof(screen), R"({"timeout": %d})", timeout);
    for (int i = 0; i < 9; ++i) {
        if (areaMask & (1u << i)) {
            MaaContextWaitFreezes(context, waitTime, &ScreenAreaBox[i], screen);
        }
    }
}

void postWaitFreezes(MaaContext* context, MaaRect roi, int waitTime, int timeout)
{
    char screen[64];
    snprintf(screen, sizeof(screen), R"({"timeout": %d})", timeout);
    MaaContextWaitFreezes(context, waitTime, &roi, screen);
}

std::unordered_map<std::string, MapIndex> mapTable = {
    { "时之尽头的孤岛", MapIndex::IsleOfTimesEnd }, { "新月郡", MapIndex::CrescentCounty }, { "圣城修道院", MapIndex::HolyCityMonastery },
    { "格雷米德", MapIndex::Map_Gremid },           { "熔炉之城", MapIndex::FurnaceCity },
};

std::string getMapName(MapIndex idx)
{
    switch (idx) {
    case MapIndex::IsleOfTimesEnd:
        return "时之尽头的孤岛";
    case MapIndex::CrescentCounty:
        return "新月郡";
    case MapIndex::HolyCityMonastery:
        return "圣城修道院";
    case MapIndex::Map_Gremid:
        return "格雷米德";
    case MapIndex::FurnaceCity:
        return "熔炉之城";
    default:
        return "";
    }
}

json::value getDefaultSoulUpgradeConfig()
{
    static json::value s_cached;
    if (!s_cached.is_null()) {
        return s_cached;
    }
    std::ifstream cf("./resources/config/monster_soul_config.json");
    if (!cf.is_open()) {
        return json::object { };
    }
    std::string content((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
    auto root = json::parse(content).value_or(json::object { });
    s_cached = root["upgrade_configs"]["默认配置"];
    return s_cached;
}

// ──── 清除节点命中计数（max_hit）────
// custom_action_param 示例：{"nodes":["NodeA","NodeB"],"strict":false}
// strict 为 true 时，任一节点清除失败则整个 action 返回失败；默认 false。
MaaBool ClearHitCountAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto param = json::parse(std::string(custom_action_param ? custom_action_param : "")).value_or(json::value { });

    auto nodes = param.get("nodes", json::value { });
    if (!nodes.is_array() || nodes.as_array().empty()) {
        LogUtils::log("清除节点命中计数错误:未指定节点", "#ef4444");
        return false;
    }

    bool strict = param.get("strict", json::value(false)).as_boolean();
    bool hasFailure = false;

    for (const auto& item : nodes.as_array()) {
        std::string name = item.as_string();
        if (name.empty()) {
            LogUtils::log("清除节点命中计数错误: nodes 含空节点名", "#f59e0b");
            hasFailure = true;
            continue;
        }

        if (!MaaContextClearHitCount(context, name.c_str())) {
            LogUtils::log(fmt("清除节点命中计数错误: 清除 %1 命中计数失败", name), "#ef4444");
            hasFailure = true;
            continue;
        }
    }

    if (hasFailure && strict) {
        return false;
    }
    return true;
}

MaaBool FailTaskAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    // 先退回主页，再让当前节点 action 失败，触发 error_handling 结束当前 task，不 stop 整个 tasker
    exitToCampfire(context);
    return false;
}

// 读取指定节点 merge 后的 attach（JSON 对象），供自定义动作/识别读配置
json::value getNodeAttach(MaaContext* context, const char* nodeName)
{
    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return json::object { };
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto node = json::parse(raw);
    if (!node) {
        return json::object { };
    }
    const auto& attach = (*node)["attach"];
    return attach.is_object() ? attach : json::value(json::object { });
}

// 全局用户配置（SaveConfig 写入，战斗/魔魂升级读取）
UserBattleConfig g_user_battle;
SoulUpgradeConfig g_user_soul;

// 把节点 attach 解析进全局结构体
void parseUserConfig(const json::value& attach)
{
    auto parseDouble = [](const std::string& s, double def) -> double {
        try {
            size_t pos = 0;
            double v = std::stod(s, &pos);
            return pos > 0 ? v : def;
        }
        catch (...) {
            return def;
        }
    };

    // 战斗配置
    g_user_battle = UserBattleConfig { };
    g_user_battle.reserve = attach.get("reserve", json::value(0)).as_integer();
    g_user_battle.cryInterval = attach.get("cry_interval", json::value(0)).as_integer();
    g_user_battle.burstSeconds = attach.get("burst_seconds", json::value(0)).as_integer();
    g_user_battle.firstSkillTime = attach.get("first_skill_time", json::value(0)).as_integer();
    std::string skill = attach.get("skill", json::value("None")).as_string();
    g_user_battle.skill = (skill == "Xiaoqing") ? Skill_Xiaoqing : Skill_None;
    std::string cryColumn = attach.get("cry_column", json::value("0")).as_string();
    g_user_battle.cryColumn = (cryColumn == "Column1") ? 1 : (cryColumn == "Column3") ? 3 : (cryColumn == "Column2") ? 2 : 0;

    // 魔魂升级配置
    g_user_soul = SoulUpgradeConfig { };
    g_user_soul.countMode = (attach.get("countMode", json::value("0")).as_string() == "1") ? 1 : 0;
    g_user_soul.count = attach.get("count", json::value(0)).as_integer();

    g_user_soul.soul_yexing = attach.get("soul_yexing", json::value(false)).as_boolean();
    g_user_soul.soul_jielue = attach.get("soul_jielue", json::value(false)).as_boolean();
    g_user_soul.soul_wugu = attach.get("soul_wugu", json::value(false)).as_boolean();
    g_user_soul.soul_baolue = attach.get("soul_baolue", json::value(false)).as_boolean();
    g_user_soul.soul_xiongsha = attach.get("soul_xiongsha", json::value(false)).as_boolean();
    g_user_soul.soul_xiedian = attach.get("soul_xiedian", json::value(false)).as_boolean();
    g_user_soul.soul_jiaozhi = attach.get("soul_jiaozhi", json::value(false)).as_boolean();
    g_user_soul.soul_manhuang = attach.get("soul_manhuang", json::value(false)).as_boolean();
    g_user_soul.soul_xiongshi = attach.get("soul_xiongshi", json::value(false)).as_boolean();
    g_user_soul.soul_kuangnu = attach.get("soul_kuangnu", json::value(false)).as_boolean();
    g_user_soul.soul_shuangxue = attach.get("soul_shuangxue", json::value(false)).as_boolean();
    g_user_soul.soul_bingjing = attach.get("soul_bingjing", json::value(false)).as_boolean();
    g_user_soul.soul_linye = attach.get("soul_linye", json::value(false)).as_boolean();
    g_user_soul.soul_lingdong = attach.get("soul_lingdong", json::value(false)).as_boolean();
    g_user_soul.soul_jihan = attach.get("soul_jihan", json::value(false)).as_boolean();
    g_user_soul.soul_baopo = attach.get("soul_baopo", json::value(false)).as_boolean();
    g_user_soul.soul_chenyuan = attach.get("soul_chenyuan", json::value(false)).as_boolean();
    g_user_soul.soul_kuangre = attach.get("soul_kuangre", json::value(false)).as_boolean();
    g_user_soul.soul_tianyun = attach.get("soul_tianyun", json::value(false)).as_boolean();
    g_user_soul.soul_xianzu = attach.get("soul_xianzu", json::value(false)).as_boolean();

    g_user_soul.retention_group1_enabled = attach.get("retention_group1_enabled", json::value(false)).as_boolean();
    g_user_soul.retention_group2_enabled = attach.get("retention_group2_enabled", json::value(false)).as_boolean();
    g_user_soul.retention_group3_enabled = attach.get("retention_group3_enabled", json::value(false)).as_boolean();

    g_user_soul.g1_interval = attach.get("g1_interval", json::value(false)).as_boolean();
    g_user_soul.g1_damage = attach.get("g1_damage", json::value(false)).as_boolean();
    g_user_soul.g1_resist = attach.get("g1_resist", json::value(false)).as_boolean();
    g_user_soul.group1_interval = parseDouble(attach.get("group1_interval", json::value("0")).as_string(), 0.0);
    g_user_soul.group1_damage = parseDouble(attach.get("group1_damage", json::value("0")).as_string(), 0.0);
    g_user_soul.group1_resist = parseDouble(attach.get("group1_resist", json::value("0")).as_string(), 0.0);

    g_user_soul.g2_interval = attach.get("g2_interval", json::value(false)).as_boolean();
    g_user_soul.g2_crit = attach.get("g2_crit", json::value(false)).as_boolean();
    g_user_soul.group2_interval = parseDouble(attach.get("group2_interval", json::value("0")).as_string(), 0.0);
    g_user_soul.group2_crit = parseDouble(attach.get("group2_crit", json::value("0")).as_string(), 0.0);

    g_user_soul.g3_interval = attach.get("g3_interval", json::value(false)).as_boolean();
    g_user_soul.g3_heal = attach.get("g3_heal", json::value(false)).as_boolean();
    g_user_soul.group3_interval = parseDouble(attach.get("group3_interval", json::value("0")).as_string(), 0.0);
    g_user_soul.group3_heal = parseDouble(attach.get("group3_heal", json::value("0")).as_string(), 0.0);
}

// SaveConfig 动作：读取当前节点 attach 并解析进全局结构体
static MaaBool SaveConfigAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    parseUserConfig(getNodeAttach(context, node_name));
    return true;
}

// 判断节点是否 enabled（拿不到数据或字段时默认 enabled）
static bool isNodeEnabled(MaaContext* context, const char* nodeName)
{
    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return true;
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto node = json::parse(raw);
    if (!node) {
        return true;
    }
    auto enabled = (*node)["enabled"];
    return enabled.is_boolean() ? enabled.as_boolean() : true;
}

// 获取节点 focus 里的中文名称（去掉 "开始任务:" 前缀），拿不到返回空串
static std::string getNodeFocusDesc(MaaContext* context, const char* nodeName)
{
    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return { };
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto node = json::parse(raw);
    if (!node) {
        return { };
    }
    auto focus = (*node)["focus"];
    if (!focus.is_object()) {
        return { };
    }
    for (auto& [event, msg] : focus.as_object()) {
        std::string text = msg.as_string();
        const std::string prefix = "开始任务:";
        if (text.rfind(prefix, 0) == 0) {
            text = text.substr(prefix.size());
        }
        return text;
    }
    return { };
}

// 运行若干子任务（entry），子任务列表由 custom_action_param.sub 传入。
// continue: 某子任务失败后是否继续跑剩余子任务（默认 false，立即停止）
// strict:   有子任务失败时是否判定整个动作失败（默认 true）
MaaBool SubTaskAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto param = json::parse(custom_action_param ? custom_action_param : "{}");
    if (!param || !param->contains("sub")) {
        LogUtils::log("SubTask: custom_action_param.sub 缺失", "#ef4444");
        return false;
    }
    auto sub = (*param)["sub"].as_array();
    if (sub.empty()) {
        LogUtils::log("SubTask: custom_action_param.sub 为空", "#ef4444");
        return false;
    }

    bool continue_on_failure = (*param).get("continue", json::value(false)).as_boolean();
    bool strict = (*param).get("strict", json::value(true)).as_boolean();

    auto* tasker = MaaContextGetTasker(context);
    bool has_failure = false;

    for (auto& item : sub) {
        std::string name = item.as_string();
        if (name.empty()) {
            has_failure = true;
            if (!continue_on_failure) {
                break;
            }
            continue;
        }

        // 跳过已禁用的子任务
        if (!isNodeEnabled(context, name.c_str())) {
            continue;
        }

        MaaTaskId sub_id = MaaContextRunTask(context, name.c_str(), "{}");
        if (sub_id == MaaInvalidId) {
            std::string desc = getNodeFocusDesc(context, name.c_str());
            LogUtils::log(fmt("任务失败:%1", desc.empty() ? name : desc), "#f59e0b");
            has_failure = true;
            if (!continue_on_failure) {
                break;
            }
            continue;
        }

        MaaStatus status = MaaStatus_Invalid;
        MaaSize node_id_list_size = 0;
        MaaTaskerGetTaskDetail(tasker, sub_id, nullptr, nullptr, &node_id_list_size, &status);
        if (status != MaaStatus_Succeeded) {
            std::string desc = getNodeFocusDesc(context, name.c_str());
            LogUtils::log(fmt("任务失败:%1", desc.empty() ? name : desc), "#f59e0b");
            has_failure = true;
            if (!continue_on_failure) {
                break;
            }
        }
    }

    return !(has_failure && strict);
}

// 判断角色是否在目标地图（map_name 通过 custom_recognition_param 或 attach 传入）。
// 逻辑：getMapOfRole 拿角色地图 + box，比对 map_name；同一地图则运行 MapListSwipePos（改 roi）识别两次。
MaaBool checkRoleInTargetMap(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_recognition_name,
    const char* custom_recognition_param,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* trans_arg,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    std::string targetMap;
    auto param = json::parse(custom_recognition_param ? custom_recognition_param : "{}").value_or(json::value { });
    targetMap = param.get("map_name", json::value("")).as_string();

    // custom_recognition_param 未指定时，从节点 attach 读取 map_name，便于公共节点参数化
    if (targetMap.empty()) {
        targetMap = getNodeAttach(context, node_name).get("map_name", json::value("")).as_string();
    }

    if (targetMap.empty()) {
        LogUtils::log("重置地图: 未指定地图名称", "#ef4444");
        return false;
    }

    MaaRect roleBox { 0, 0, 0, 0 };
    std::string mapOfRole = getMapOfRole(context, &roleBox);
    if (mapOfRole.empty()) {
        return false;
    }

    // map_name 支持 "a|b|c" 多选，任一命中即通过
    bool matched = false;
    for (auto& alt : split(targetMap, '|')) {
        if (mapOfRole.find(alt) != std::string::npos) {
            matched = true;
            break;
        }
    }
    if (!matched) {
        return false;
    }

    // 注意：MapListItemOcr / MapListItemTemplate 是叶子识别节点，roi 覆盖才对它们生效
    // （MapListSwipePos 是 Or 节点，覆盖 roi 会被忽略）。
    char detailJson[256];
    int boxY = roleBox.y;

    // 角色上方区域
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemOcr":{"roi":[125,225,80,%d]}})", boxY - 280);
    if (doRecognition(context, image, "MapListItemOcr", detailJson, out_box, nullptr)) {
        return true;
    }
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemTemplate":{"roi":[125,225,80,%d]}})", boxY - 280);
    if (doRecognition(context, image, "MapListItemTemplate", detailJson, out_box, nullptr)) {
        return true;
    }

    // 角色下方区域
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemOcr":{"roi":[125,%d,80,%d]}})", boxY + 100, 880 - boxY);
    if (doRecognition(context, image, "MapListItemOcr", detailJson, out_box, nullptr)) {
        return true;
    }
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemTemplate":{"roi":[125,%d,80,%d]}})", boxY + 100, 880 - boxY);
    if (doRecognition(context, image, "MapListItemTemplate", detailJson, out_box, nullptr)) {
        return true;
    }

    return false;
}

static uint32_t g_shopMask = 0;        // 已找到（购买完成）的商品位掩码
static uint32_t g_shopEnabledMask = 0; // 启用购买的商品位掩码（由配置开关决定）
MaaBool shopAssistant(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = getNodeAttach(context, nodeName);
    bool initFlag = attach.get("initFlag", json::value(false)).as_boolean();

    if (initFlag) {
        // 初始化：清空状态，读取各商品开关
        g_shopMask = 0;
        g_shopEnabledMask = 0;
        for (auto& item : shopItemList) {
            if (attach.get(item.enabledKey, json::value(true)).as_boolean()) {
                g_shopEnabledMask |= (1U << item.bit);
            }
        }
    }
    // 每次调用都累加已找到的商品
    for (auto& item : shopItemList) {
        if (attach.get(item.foundKey, json::value(false)).as_boolean()) {
            g_shopMask |= (1U << item.bit);
        }
    }
    return true;
}

MaaBool shopFinishCheck(
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
    const uint32_t targetMask = g_shopEnabledMask;
    if (targetMask == 0) {
        return true; // 未启用任何商品，视为完成
    }
    return (g_shopMask & targetMask) == targetMask;
}

MaaBool itemNotBought(
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
    auto param = json::parse(customRecognitionParam ? customRecognitionParam : "{}").value_or(json::value { });
    std::string foundKey = param.get("foundKey", json::value("")).as_string();
    if (foundKey.empty()) {
        LogUtils::error("ItemNotBought: 缺少 custom_recognition_param.foundKey");
        return false;
    }

    for (auto& item : shopItemList) {
        if (foundKey == item.foundKey) {
            return (g_shopMask & (1U << item.bit)) == 0;
        }
    }

    LogUtils::error(fmt("ItemNotBought: 未找到商品 %1", foundKey));
    return false;
}

// ──── 马车商店滑动购买 ────
// 读货币数量与商品单价，滑到最大实测滑轨两端并读最大数量，算出购买数量后线性插值点出目标，再用 +/- 模板按钮微调。
// 注意：商品点击由外部 pipeline 节点完成，进入此 action 时购买弹窗必须已打开。
// custom_action_param：
//   currencyCount   货币数量 OCR 节点名（可为普通 OCR 或 And 节点）—— 必填
//   unitPrice       商品单价 OCR 节点名（默认 CarriageShopSlideBuyUnitPrice）
//   slider          滑块(thumb)识别节点名（默认 CarriageShopFindSlider）
//   increaseButton  + 按钮 TemplateMatch 节点名（默认 CarriageShopIncreaseButton）
//   decreaseButton  - 按钮 TemplateMatch 节点名（默认 CarriageShopDecreaseButton）
//   quantityOffset  数量框相对 thumb box 的偏移 [dx,dy,dw,dh]（默认 [-15,35,25,0]）
//   canBuyCheck     可购买校验 task 名（默认 CarriageShopCanBuyAfterSlide）

// 读取定长 int 数组，缺省用 def 补齐
static std::vector<int> readIntArray(const json::value& v, std::vector<int> def)
{
    if (!v.is_array()) {
        return def;
    }
    auto& arr = v.as_array();
    std::vector<int> out = def;
    for (size_t i = 0; i < arr.size() && i < out.size(); ++i) {
        out[i] = arr[i].as_integer();
    }
    return out;
}

// 递归收集 OCR detail 中的所有文本（含 And 节点子结果的递归）
static void collectOcrTexts(const json::value& v, std::vector<std::string>& out)
{
    if (v.is_object()) {
        const auto& obj = v.as_object();

        // best：最佳结果（对象，含 text）
        if (obj.contains("best")) {
            const auto& best = obj.at("best");
            if (best.is_object()) {
                const auto& bestObj = best.as_object();
                if (bestObj.contains("text")) {
                    std::string t = bestObj.at("text").as_string();
                    if (!t.empty()) {
                        out.push_back(t);
                    }
                }
            }
        }

        // filtered / all：结果数组，收集每个的 text
        for (const char* key : { "filtered", "all" }) {
            if (obj.contains(key)) {
                const auto& arr = obj.at(key);
                if (arr.is_array()) {
                    for (const auto& item : arr.as_array()) {
                        if (item.is_object()) {
                            const auto& itemObj = item.as_object();
                            if (itemObj.contains("text")) {
                                std::string t = itemObj.at("text").as_string();
                                if (!t.empty()) {
                                    out.push_back(t);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 递归子对象（And 节点 detail 是子结果数组）
        for (const auto& [k, child] : obj) {
            collectOcrTexts(child, out);
        }
    }
    else if (v.is_array()) {
        for (const auto& child : v.as_array()) {
            collectOcrTexts(child, out);
        }
    }
}

// 从 OCR 识别 detail(json) 中提取数字。subName 非空时定位 And 节点中 name==subName 的子结果
static bool extractNumberFromDetail(const std::string& detail, const char* subName, int& outValue)
{
    auto j = json::parse(detail);
    if (!j) {
        return false;
    }

    const json::value* target = &(*j);
    if (subName && subName[0] != '\0') {
        if (!j->is_array()) {
            return false;
        }
        target = nullptr;
        for (const auto& sub : j->as_array()) {
            if (sub.is_object() && sub.as_object().contains("name")) {
                if (sub.as_object().at("name").as_string() == subName) {
                    target = &sub;
                    break;
                }
            }
        }
        if (!target) {
            LogUtils::error(fmt("extractNumberFromDetail: 未找到子结果 %1", subName));
            return false;
        }
    }

    std::vector<std::string> texts;
    collectOcrTexts(*target, texts);

    for (const auto& text : texts) {
        std::string digits;
        for (char c : text) {
            if (c >= '0' && c <= '9') {
                digits += c;
            }
            else if (!digits.empty()) {
                break;
            }
        }
        if (!digits.empty()) {
            outValue = std::atoi(digits.c_str());
            return true;
        }
    }

    outValue = 0;
    return true;
}

// 运行指定识别节点(OCR/And)，提取其中的数字。subName 非空时从 And 节点指定子结果读取
static bool readNumberFromNode(MaaContext* context, const char* nodeName, const char* subName, int& outValue)
{
    ScreenCap cap(context);
    auto detailBuf = MaaStringBufferCreate();
    MaaRect box;
    if (!doRecognition(context, cap.img, nodeName, "{}", &box, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return false;
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    return extractNumberFromDetail(detail, subName, outValue);
}

// 直接 OCR 指定 roi 中的数字
static bool readNumberFromRoi(MaaContext* context, const MaaRect& roi, int& outValue)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    std::string param = fmt(R"({"roi":[%1,%2,%3,%4],"only_rec":true})", roi.x, roi.y, roi.width, roi.height);
    auto id = MaaContextRunRecognitionDirect(context, "OCR", param.c_str(), cap.img);
    if (id == MaaInvalidId) {
        return false;
    }
    auto detailBuf = MaaStringBufferCreate();
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &box, detailBuf, nullptr, nullptr);
    if (!hit) {
        MaaStringBufferDestroy(detailBuf);
        return false;
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    return extractNumberFromDetail(detail, nullptr, outValue);
}

// 精确点击指定坐标
static void clickPoint(MaaContext* context, int x, int y)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, x, y);
    MaaControllerWait(ctrl, id);
}

// 运行指定 task 并判断是否成功
static bool runTaskSuccess(MaaContext* context, const char* taskName)
{
    auto* tasker = MaaContextGetTasker(context);
    MaaTaskId id = MaaContextRunTask(context, taskName, "{}");
    if (id == MaaInvalidId) {
        return false;
    }
    MaaStatus status = MaaStatus_Invalid;
    MaaSize nodeCount = 0;
    MaaTaskerGetTaskDetail(tasker, id, nullptr, nullptr, &nodeCount, &status);
    return status == MaaStatus_Succeeded;
}

// 从一点滑动到另一点
static void swipeTo(MaaContext* context, int x1, int y1, int x2, int y2, int duration)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostSwipe(ctrl, x1, y1, x2, y2, duration);
    MaaControllerWait(ctrl, id);
}

// 运行识别节点，取识别框
static bool findNodeBox(MaaContext* context, const char* nodeName, MaaRect& outBox)
{
    ScreenCap cap(context);
    return doRecognition(context, cap.img, nodeName, "{}", &outBox, nullptr);
}

// 读取 handle 相对位置的数量（数量框 = handle box + qOffset）
static bool readQuantityAtHandle(MaaContext* context, const MaaRect& handleBox, const std::vector<int>& qOffset, int& out)
{
    MaaRect qb;
    qb.x = handleBox.x + qOffset[0];
    qb.y = handleBox.y + qOffset[1];
    qb.width = handleBox.width + qOffset[2];
    qb.height = handleBox.height + qOffset[3];
    return readNumberFromRoi(context, qb, out);
}

// 识别按钮(模板)并点击中心
static bool findAndClickButton(MaaContext* context, const char* buttonNode)
{
    MaaRect btnBox;
    if (!findNodeBox(context, buttonNode, btnBox)) {
        return false;
    }
    clickPoint(context, btnBox.x + btnBox.width / 2, btnBox.y + btnBox.height / 2);
    return true;
}

static MaaBool CarriageShopSlideBuyAct(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto param = json::parse(customActionParam ? customActionParam : "{}").value_or(json::value { });

    std::string sliderNode = param.get("slider", json::value("CarriageShopFindSlider")).as_string();
    std::string currencyNode = param.get("currencyCount", json::value("")).as_string();
    std::string unitPriceNode = param.get("unitPrice", json::value("CarriageShopSlideBuyUnitPrice")).as_string();
    std::string increaseBtn = param.get("increaseButton", json::value("CarriageShopIncreaseButton")).as_string();
    std::string decreaseBtn = param.get("decreaseButton", json::value("CarriageShopDecreaseButton")).as_string();
    std::string canBuyCheck = param.get("canBuyCheck", json::value("CarriageShopCanBuyAfterSlide")).as_string();
    auto qOffset = readIntArray(param.get("quantityOffset", json::value()), { -15, 35, 25, 0 });

    if (currencyNode.empty()) {
        LogUtils::error("CarriageShopSlideBuy: 缺少 currencyCount 配置");
        return false;
    }

    // 读货币数量（currencyCount 节点可为普通 OCR 或 And 节点，readNumberFromNode 均能解析）
    int currency = 0;
    if (!readNumberFromNode(context, currencyNode.c_str(), "TrialStoneCount", currency)) {
        return true;
    }

    // 商品已由外部节点点击打开购买弹窗，这里直接识别数量滑块,未识别到滑块由外部重新点击
    MaaRect thumbBox;
    if (!findNodeBox(context, sliderNode.c_str(), thumbBox)) {
        return true;
    }

    // 读商品单价（在弹窗中识别）
    int unitPrice = 0;
    if (!readNumberFromNode(context, unitPriceNode.c_str(), "UnitPrice", unitPrice) || unitPrice <= 0) {
        LogUtils::error("CarriageShopSlideBuy: 读取单价失败");
        return false;
    }

    // 记录滑轨起点，滑到最大
    MaaRect startBox = thumbBox;
    int startX = startBox.x + startBox.width / 2;
    int startY = startBox.y + startBox.height / 2;
    swipeTo(context, startX, startY, startX + 600, startY, 200); // 向右滑 600px 到最大
    Sleep(500);

    // 识别滑轨终点，读最大数量
    MaaRect endBox;
    if (!findNodeBox(context, sliderNode.c_str(), endBox)) {
        endBox = startBox;
    }
    int maxQuantity = 0;
    if (!readQuantityAtHandle(context, endBox, qOffset, maxQuantity) || maxQuantity < 1) {
        LogUtils::error("CarriageShopSlideBuy: 读取最大数量失败");
        return false;
    }

    // 计算购买数量（受货币数量限制，最大数量已从滑轨实际测得）
    int buyCount = (std::min)(maxQuantity, currency / unitPrice);
    if (buyCount <= 0) {
        return true;
    }

    // 目标 >= 最大，已滑到最大，直接返回（pipeline 后续点购买）
    if (buyCount >= maxQuantity) {
        return true;
    }

    // 线性插值（滑块最小是 1，整体 -1）
    int endX = endBox.x + endBox.width / 2;
    int endY = endBox.y + endBox.height / 2;
    int clickX = startX + (endX - startX) * (buyCount - 1) / (maxQuantity - 1);
    int clickY = startY + (endY - startY) * (buyCount - 1) / (maxQuantity - 1);
    clickPoint(context, clickX, clickY);
    Sleep(500);

    // 读实际数量并用 +/- 模板按钮微调
    const int maxRetry = 10;
    for (int attempt = 0; attempt < maxRetry; ++attempt) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return true;
        }
        MaaRect curBox;
        if (!findNodeBox(context, sliderNode.c_str(), curBox)) {
            curBox = endBox;
        }
        int actual = 0;
        if (!readQuantityAtHandle(context, curBox, qOffset, actual)) {
            clickPoint(context, clickX, clickY); // 读不到数量，重新点滑块
            Sleep(500);
            continue;
        }

        int diff = buyCount - actual;
        if (diff == 0) {
            break; // 命中目标数量
        }

        const char* btnNode = diff > 0 ? increaseBtn.c_str() : decreaseBtn.c_str();
        int steps = diff > 0 ? diff : -diff;
        if (steps > 30) {
            steps = 30; // 防误识别点太多
        }
        for (int i = 0; i < steps; ++i) {
            findAndClickButton(context, btnNode);
            Sleep(200);
        }
        Sleep(300); // 等界面更新，下一轮重新校验
    }

    // 校验当前数量是否可购买，不够就点 - 递减
    if (!canBuyCheck.empty()) {
        for (int attempt = 0; attempt < maxRetry; ++attempt) {
            if (MaaTaskerStopping(MaaContextGetTasker(context))) {
                return true;
            }
            if (runTaskSuccess(context, canBuyCheck.c_str())) {
                return true; // 可购买
            }
            MaaRect curBox;
            if (!findNodeBox(context, sliderNode.c_str(), curBox)) {
                curBox = endBox;
            }
            int cur = 0;
            if (readQuantityAtHandle(context, curBox, qOffset, cur) && cur <= 1) {
                return true; // 已减到 1，不能再减
            }
            findAndClickButton(context, decreaseBtn.c_str());
            Sleep(300);
        }
    }

    return true;
}

void registerCustomComm(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "ClearHitCount", ClearHitCountAct, user_data);
    registerCustomAction(res, "FailTask", FailTaskAct, user_data);
    registerCustomAction(res, "SaveConfig", SaveConfigAct, user_data);
    registerCustomAction(res, "SubTask", SubTaskAct, user_data);
    registerCustomAction(res, "ShopAssistant", shopAssistant, user_data);
    registerCustomAction(res, "CarriageShopSlideBuy", CarriageShopSlideBuyAct, user_data);
    registerCustomRecognition(res, "ShopFinishCheck", shopFinishCheck, user_data);
    registerCustomRecognition(res, "ItemNotBought", itemNotBought, user_data);
    registerCustomRecognition(res, "CheckRoleInTargetMap", checkRoleInTargetMap, user_data);
}
