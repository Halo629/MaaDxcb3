#pragma once
#include "LogUtils.h"
#include "MaaFramework/MaaAPI.h"
#include "MaaFramework/Utility/MaaBuffer.h"
#include "Registry.h"
#include "ServerConfig.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>
#include <functional>
#include <map>
#include <meojson/json.hpp>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <thread>
#include <chrono>

// ──── 字符串工具（原 Str.h）────
inline std::string toStr(const std::string& s)
{
    return s;
}

inline std::string toStr(const char* s)
{
    return s ? std::string(s) : std::string();
}

inline std::string toStr(char c)
{
    return std::string(1, c);
}

template <typename T>
inline std::string toStr(const T& v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

inline void replaceN(std::string& s, int n, const std::string& repl)
{
    std::string marker = "%" + std::to_string(n);
    size_t pos = 0;
    while ((pos = s.find(marker, pos)) != std::string::npos) {
        s.replace(pos, marker.size(), repl);
        pos += repl.size();
    }
}

template <typename... Args>
std::string fmt(const std::string& f, Args&&... args)
{
    std::string result = f;
    int i = 1;
    (replaceN(result, i++, toStr(std::forward<Args>(args))), ...);
    return result;
}

inline std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            if (!cur.empty()) {
                out.push_back(cur);
            }
            cur.clear();
        }
        else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

inline std::string join(const std::vector<std::string>& v, const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i != 0) {
            out += sep;
        }
        out += v[i];
    }
    return out;
}

inline std::string trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
        ++b;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
        --e;
    }
    return s.substr(b, e - b);
}

extern const int one_second_delay;
extern const int two_second_delay;

struct ScreenCap
{
    MaaImageBuffer* img;
    explicit ScreenCap(MaaContext* c);
    ~ScreenCap();
    ScreenCap(const ScreenCap&) = delete;
    ScreenCap& operator=(const ScreenCap&) = delete;
};

struct MaaRect;

// 等待到达地图界面（出现"返回秘境"）
bool waitArriveMap(MaaContext* context);

// 读目标所在地图名（通过指定JSON节点），可选返回识别框
std::string getMapOfRole(MaaContext* context, MaaRect* outBox = nullptr);
std::string getMapOfFerdinand(MaaContext* context, MaaRect* outBox);

// 读当前所在地图名（通过 CurrentSupportMap 的 InXXX 节点判断）
std::string getCurrentMap(MaaContext* context);

// OCR 地图列表，返回所有识别项的 json array
json::value getCurMapList(MaaContext* context);

// 退出当前地图，回到营火
void exitToCampfire(MaaContext* context);

// 切换地图排序（"正序"或"倒序"）
void switchMapSort(MaaContext* context, const std::string& targetOrder);

// 设置自定义动作的下一个节点
void setNextNode(MaaContext* context, const char* nodeName, const char* nextNode);

// 食谱配置
void loadRecipeConfig();                                                  // 初始化食谱配置，幂等
const std::unordered_set<std::string>& getRecipeNames();                  // 食谱名集合
const std::map<std::string, std::vector<std::string>>& getRecipeConfig(); // 食谱名 → 配方食材列表

// 从地图列表进入指定地图，返回是否成功
bool enterMapByName(MaaContext* context, const std::string& mapName);

bool doRecognition(
    MaaContext* context,
    const MaaImageBuffer* img,
    const char* entry,
    const char* detail_json,
    MaaRect* out_box,
    MaaStringBuffer* out_detail = nullptr);

bool waitUntilRecognitionSuccess(
    MaaContext* context,
    const char* entry,
    const char* detail_json,
    MaaRect* out_box,
    MaaStringBuffer* out_detail,
    int timeoutMs = 8000);

void clickRandomTarget(MaaContext* context, int x, int y, int w, int h);

std::string ocrText(MaaContext* context, const MaaImageBuffer* img, const char* entry, const char* detailJson, MaaRect* box);

// 从 And 节点识别结果中取指定 sub_name 子节点的 detail（含 all/filtered/best），未找到返回空 json::value
json::value getAndSubDetail(MaaContext* context, const MaaImageBuffer* img, const char* entry, const char* subName);

// 获取可操作区域（屏幕减去不可操作区域后的剩余矩形列表）
std::vector<MaaRect> getClickableAreas(MaaContext* context);

// 快速判断一个点是否在可操作区域内（只需判断是否落在任一不可操作区域内）
bool isClickable(MaaContext* context, int x, int y);

// 服务器名归一化：去掉冒号（中英文）和空格/制表符，统一 OCR 结果避免符号差异
std::string normalizeServerName(const std::string& raw);

// 屏幕区域划分 (720×1280 九宫格)
enum ScreenArea : uint32_t
{
    ScreenArea_TopLeft = 1 << 0,   // [0,    0,    240, 427]
    ScreenArea_TopCenter = 1 << 1, // [240,  0,    240, 427]
    ScreenArea_TopRight = 1 << 2,  // [480,  0,    240, 427]
    ScreenArea_MidLeft = 1 << 3,   // [0,    427,  240, 427]
    ScreenArea_MidCenter = 1 << 4, // [240,  427,  240, 427]
    ScreenArea_MidRight = 1 << 5,  // [480,  427,  240, 427]
    ScreenArea_BotLeft = 1 << 6,   // [0,    854,  240, 426]
    ScreenArea_BotCenter = 1 << 7, // [240,  854,  240, 426]
    ScreenArea_BotRight = 1 << 8,  // [480,  854,  240, 426]

    ScreenArea_TopRow = ScreenArea_TopLeft | ScreenArea_TopCenter | ScreenArea_TopRight,
    ScreenArea_MidRow = ScreenArea_MidLeft | ScreenArea_MidCenter | ScreenArea_MidRight,
    ScreenArea_BotRow = ScreenArea_BotLeft | ScreenArea_BotCenter | ScreenArea_BotRight,
    ScreenArea_LeftCol = ScreenArea_TopLeft | ScreenArea_MidLeft | ScreenArea_BotLeft,
    ScreenArea_CenterCol = ScreenArea_TopCenter | ScreenArea_MidCenter | ScreenArea_BotCenter,
    ScreenArea_RightCol = ScreenArea_TopRight | ScreenArea_MidRight | ScreenArea_BotRight,
    ScreenArea_Full = ScreenArea_TopRow | ScreenArea_MidRow | ScreenArea_BotRow,
};

// 720×1280 九宫格区域坐标
static constexpr MaaRect ScreenAreaBox[] = {
    { 0, 0, 240, 427 },      // ScreenArea_TopLeft
    { 240, 0, 240, 427 },    // ScreenArea_TopCenter
    { 480, 0, 240, 427 },    // ScreenArea_TopRight
    { 0, 427, 240, 427 },    // ScreenArea_MidLeft
    { 240, 427, 240, 427 },  // ScreenArea_MidCenter
    { 480, 427, 240, 427 },  // ScreenArea_MidRight
    { 0, 854, 240, 426 },    // ScreenArea_BotLeft
    { 240, 854, 240, 426 },  // ScreenArea_BotCenter
    { 480, 854, 240, 426 },  // ScreenArea_BotRight
};

void postWaitFreezes(MaaContext* context, uint32_t areaMask = ScreenArea_MidCenter, int waitTime = 300, int timeout = 3000);
void postWaitFreezes(MaaContext* context, MaaRect roi, int waitTime = 300, int timeout = 3000);

// ──── 罗盘方位与滑动/点击/走位（通用：秘宝任务与走位测试共用）────

// 罗盘 12 方位（每格 30°，0°=正北，顺时针）
enum class CompassDir {
    North = 0,        // 正北
    NorthByEast = 1,  // 北偏东
    EastByNorth = 2,  // 东偏北
    East = 3,         // 正东
    EastBySouth = 4,  // 东偏南
    SouthByEast = 5,  // 南偏东
    South = 6,        // 正南
    SouthByWest = 7,  // 南偏西
    WestBySouth = 8,  // 西偏南
    West = 9,         // 正西
    WestByNorth = 10, // 西偏北
    NorthByWest = 11, // 北偏西
    Unknown = 12,
};

// 罗盘方位文本 → 12 方位（支持"北偏东""正北""北"等）
CompassDir parseDirection(const std::string& text);

// 世界坐标向量 → 12 方位（+x=东，+y=北，0°=北顺时针）
CompassDir vectorToDirection(int dx, int dy);

// 解析 "(19,73)" 或 "(19, 73)" 形式的坐标
bool parseCoord(const std::string& text, int& x, int& y);

// 点击指定坐标；若不在可操作区域内，朝屏幕中心滑动把点击点拉进可操作区域后再点。
void clickAdjusted(MaaContext* context, int x, int y);

// 朝某方位滑动屏幕（拖拽视角：滑动方向与观察方向相反，看北=往下滑、看东=往左滑）。
// 滑动起点必须在可操作区域内；若起点落在据点上滑动变点击弹框，执行 QuitMapOutpost 退出并按上下左右偏移重试。
void swipeAvoidOutpost(MaaContext* context, CompassDir dir, int dist = 300, int duration = 300);

// 朝目标方向 (dx,dy) 连续滑动屏幕（dx=东分量、dy=北分量，拖拽视角：滑动方向与观察方向相反）。
// 不量化到 12 方位，方向更精确，避免地图长宽不等时越滑越偏。dist 为滑动距离。
void swipeToward(MaaContext* context, int dx, int dy, int dist = 300, int duration = 300);

// 屏幕 OCR 查找文字，命中返回 box。
bool findTextOnScreen(MaaContext* context, const MaaImageBuffer* img, const std::string& text, MaaRect& outBox);

// 走位单步结果
enum class WalkStep { Found, NotFound, Error };

// 单步走位：读人物坐标 → 朝目标方向滑动 → 找位置名。
// Found=找到位置名(outBox)；NotFound=没找到(继续滑动)；Error=读坐标/算方向失败。
WalkStep walkStep(MaaContext* context, int tx, int ty, const std::string& text, MaaRect& outBox);

// 运行一个 pipeline 任务并等待其完成。
void runTaskAndWait(MaaContext* context, const std::string& task);

// 走到目标点位（含穿墙），返回位置名框。找到返回 true。
// 墙配置由 DefineMapWalls 动作按地图名定义，这里自动按当前地图查配置、判断是否穿墙。
bool walkToTargetPoint(
    MaaContext* context,
    int tx,
    int ty,
    const std::string& text,
    MaaRect& outBox,
    int maxAttempts = 30);

// 地图数据
enum class MapIndex
{
    Map_None = 0,
    IsleOfTimesEnd,    // 时之尽头的孤岛
    CrescentCounty,    // 新月郡
    HolyCityMonastery, // 圣城修道院
    Map_Gremid,        // 格雷米德
    FurnaceCity        // 熔炉之城
};

struct ShopItemConfig
{
    const char* foundKey;   // 已找到（购买完成）标志在 attach 中的键名
    const char* enabledKey; // 是否购买的配置开关在 attach 中的键名
    uint32_t bit;           // 在掩码中的位
};

constexpr std::array<ShopItemConfig, 6> shopItemList = { {
    { "timeBondFragmentFound", "timeBondFragmentEnabled", 0U },
    { "starsprigTwigFound", "starsprigTwigEnabled", 1U },
    { "ouroborosRingFound", "ouroborosRingEnabled", 2U },
    { "heartbeatRubbingFound", "heartbeatRubbingEnabled", 3U },
    { "godsWeightsFound", "godsWeightsEnabled", 4U },
    { "divineShadeSoulFound", "divineShadeSoulEnabled", 5U },
} };

extern std::unordered_map<std::string, MapIndex> mapTable;
std::string getMapName(MapIndex idx);

// 读取指定节点的 attach（合并后的 JSON 对象），供各自定义动作/识别读配置
json::value getNodeAttach(MaaContext* context, const char* nodeName);

// 魔魂升级配置
json::value getDefaultSoulUpgradeConfig();

// 全局用户配置（SaveConfig 动作写入，战斗/魔魂升级读取）
extern UserBattleConfig g_user_battle;
extern SoulUpgradeConfig g_user_soul;

// 把节点 attach 解析进全局结构体（SaveConfig 与 SwitchServerAct 共用）
void parseUserConfig(const json::value& attach);

// 注册自定义动作（ClearHitCount 等）
void registerCustomComm(MaaResource* res, void* user_data);
