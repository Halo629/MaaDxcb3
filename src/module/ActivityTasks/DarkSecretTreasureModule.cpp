#include "DarkSecretTreasureModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "map/MapNavigator.h"
#include "movement/Movement.h"
#include "util/JsonUtil.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

#include <string>
#include <utility>
#include <vector>
#include <windows.h>

namespace
{

// 线索地图名 → SceneEnterXXXNoReset 节点映射（地图名支持 "a|b|c" 多别名，任一命中即可）
const std::vector<std::pair<const char*, const char*>> g_clueMapNodeTable = {
    { "时之尽头的孤岛|时之尽头|孤岛", "SceneEnterIsleOfTimesEndNoReset" },
    { "新月郡|新月", "SceneEnterCrescentCountyNoReset" },
    { "圣城修道院|圣城|修道院", "SceneEnterHolyCityMonasteryNoReset" },
    { "格雷米德|格雷", "SceneEnterGremidNoReset" },
    { "熔炉之城|熔炉", "SceneEnterFurnaceCityNoReset" },
    { "蛮牛谷|蛮牛", "SceneEnterWildBullValleyNoReset" },
    { "银月谷|银月", "SceneEnterSilverMoonValleyNoReset" },
    { "伊瑟瑞尔|伊瑟|瑞尔", "SceneEnterIserelNoReset" },
};

// 读指定 OCR 节点文本
std::string readText(MaaContext* context, const char* entry)
{
    ScreenCap cap(context);
    return recog::readTextViaEntry(context, cap.img, entry, "{}", nullptr);
}

// 寻宝时的动态状态：罗盘方位 + 人物世界坐标
struct TreasureState {
    CompassDir compassDir = CompassDir::Unknown;
    int cx = 0;
    int cy = 0;
    bool hasCoord = false;
};

// 点罗盘（如果有）刷新方位，并读人物坐标，填充 state。
void readTreasureState(MaaContext* context, TreasureState& state)
{
    // 点击罗盘，让方位文字刷新出来
    {
        ScreenCap cap(context);
        MaaRect compassBox;
        if (recog::run(context, cap.img, "HasTreasureCompass", "{}", &compassBox, nullptr)) {
            screen::clickPoint(context, compassBox.x + compassBox.width / 2, compassBox.y + compassBox.height / 2);
            Sleep(1000);
        }
    }
    state.compassDir = direction::parseDirection(readText(context, "GetTreasureDirection"));
    state.hasCoord = direction::parseCoord(readText(context, "SceneMapCharacterPosition"), state.cx, state.cy);
}

// 重新读方位和坐标（穿墙后人物位置变了，需要刷新）。
void refreshTreasureState(MaaContext* context, TreasureState& state)
{
    state.compassDir = direction::parseDirection(readText(context, "GetTreasureDirection"));
    state.hasCoord = direction::parseCoord(readText(context, "SceneMapCharacterPosition"), state.cx, state.cy);
}

// 在目标点位附近找挖掘图标并点击挖掘。返回是否挖掘成功。
bool tryDigAtPoint(MaaContext* context, const json::value& pt)
{
    std::string text = pt.get("text", json::value("")).as_string();

    // 找挖掘图标并点击
    ScreenCap cap(context);
    MaaRect digBox;
    if (recog::run(context, cap.img, "DigTreasure", "{}", &digBox, nullptr)) {
        screen::clickPoint(context, digBox.x + digBox.width / 2, digBox.y + digBox.height / 2);
        LogUtils::info(fmt("SeekClueTreasure: 在[%1]挖掘成功", text));
        return true;
    }
    return false;
}

} // namespace

// 自定义动作：读取 GoToClueMap 的 OCR 文本，识别线索所在地图，把 next 定向到对应 SceneEnterXXXNoReset 节点
MaaBool DarkSecretTreasureModule::goToClueMap(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    ScreenCap cap(context);
    std::string text = recog::readTextViaEntry(context, cap.img, "GoToClueMap", "{}", nullptr);

    for (const auto& [mapName, nextNode] : g_clueMapNodeTable) {
        for (const auto& alt : split(mapName, '|')) {
            if (text.find(alt) != std::string::npos) {
                LogUtils::info(fmt("当前秘宝线索在地图[%1]", alt));
                MapNavigator::setNextNode(context, nodeName, nextNode);
                return true;
            }
        }
    }

    LogUtils::error(fmt("未识别到当前线索所在地图，OCR 文本: %1", text));
    return false;
}

// 自定义动作：寻找线索宝藏
// custom_action_param:
//   points: [{ "x":19, "y":73, "text":"避世树屋", "offset":[-47,29,-46,-2] }, ...]
//   墙配置由 DefineMapWalls 自定义动作定义（按地图名），走位时自动判断是否穿墙。
MaaBool DarkSecretTreasureModule::seekClueTreasure(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto param = jsonutil::parse(customActionParam ? customActionParam : "{}");
    auto points = param.get("points", json::value { });
    if (!points.is_array() || points.as_array().empty()) {
        LogUtils::error("SeekClueTreasure: 缺少 points 配置");
        return false;
    }

    // 读罗盘方位和人物坐标
    TreasureState state;
    readTreasureState(context, state);
    LogUtils::debug(fmt("SeekClueTreasure: 方位=%1，坐标=(%2,%3)", static_cast<int>(state.compassDir), state.cx, state.cy));

    // 遍历候选点位
    for (const auto& pt : points.as_array()) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return false;
        }
        int tx = pt.get("x", json::value(0)).as_integer();
        int ty = pt.get("y", json::value(0)).as_integer();

        // 用坐标算出该点位的方向，罗盘方向过滤不符的点位
        CompassDir pointDir = state.hasCoord ? direction::vectorToDirection(tx - state.cx, ty - state.cy) : state.compassDir;
        if (state.compassDir != CompassDir::Unknown && state.hasCoord && pointDir != state.compassDir) {
            continue;
        }

        // 走到目标点位（含穿墙），返回位置名框
        std::string task = pt.get("task", json::value("")).as_string();
        auto offset = jsonutil::readIntArray(pt.get("offset", json::value()), { 0, 0, 0, 0 });

        MaaRect textBox;
        if (!Movement::walkToTargetPoint(context, tx, ty, task, textBox, 30)) {
            continue; // 走不到，试下一个点位
        }

        // 点击位置名（触发人物寻路）
        int clickX = textBox.x + textBox.width / 2 + (offset.size() > 0 ? offset[0] : 0);
        int clickY = textBox.y + textBox.height / 2 + (offset.size() > 1 ? offset[1] : 0);
        screen::clickPoint(context, clickX, clickY);
        Sleep(1500); // 等人物寻路

        // 走位后人物坐标变了，刷新 state
        refreshTreasureState(context, state);

        // 在目标点位附近找位置名 + 点击挖掘
        if (tryDigAtPoint(context, pt)) {
            return true;
        }
    }

    LogUtils::warn("所有点位尝试完毕，未找到秘宝");
    return false;
}

namespace
{

MaaBool goToClueMapThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                         const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return DarkSecretTreasureModule::goToClueMap(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool seekClueTreasureThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                              const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return DarkSecretTreasureModule::seekClueTreasure(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void DarkSecretTreasureModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "GoToClueMap", goToClueMapThunk, userData);
    registerCustomAction(res, "SeekClueTreasure", seekClueTreasureThunk, userData);
}
