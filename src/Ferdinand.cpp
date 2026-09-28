#include "Comm.h"

// Forward declarations
static bool findFerdinandInGremid(MaaContext*, MaaRect&);
static bool findFerdinandInIsleOfTimesEnd(MaaContext*, MaaRect&);
static bool findFerdinandInCrescentCounty(MaaContext*, MaaRect&);
static bool findFerdinandInHolyCityMonastery(MaaContext*, MaaRect&);
static bool findFerdinandInFurnaceCity(MaaContext*, MaaRect&);

// 目标区域：费迪南德需要在此范围内才能点击
static constexpr MaaRect kFerdinandTargetROI = { 196, 436, 402, 685 };

// 检查 outBox 中心是否在目标区域内
static bool isInTargetROI(const MaaRect& box)
{
    int cx = box.x + box.width / 2;
    int cy = box.y + box.height / 2;
    return cx >= kFerdinandTargetROI.x && cx <= kFerdinandTargetROI.x + kFerdinandTargetROI.width && cy >= kFerdinandTargetROI.y
           && cy <= kFerdinandTargetROI.y + kFerdinandTargetROI.height;
}

// 识别费迪南德，识别到后确保在目标区域内，不在则滑动调整
static bool HasFerdinand(MaaContext* context, MaaRect& outBox)
{
    for (int attempt = 0; attempt < 10; ++attempt) {
        ScreenCap cap(context);
        if (!doRecognition(context, cap.img, "HasFerdinand", "{}", &outBox, nullptr)) {
            return false; // 没识别到，交给上层滑动搜索
        }

        if (isInTargetROI(outBox)) {
            return true; // 已在目标区域
        }

        // 计算偏差方向并滑动
        int cx = outBox.x + outBox.width / 2;
        int cy = outBox.y + outBox.height / 2;

        int fromX = 500, fromY = 800, toX = 500, toY = 800;
        if (cx < kFerdinandTargetROI.x) {
            // 人物在目标左边 → 向右滑
            fromX = 300;
            toX = 400;
        }
        else if (cx > kFerdinandTargetROI.x + kFerdinandTargetROI.width) {
            // 人物在目标右边 → 向左滑
            fromX = 500;
            toX = 400;
        }
        if (cy < kFerdinandTargetROI.y) {
            // 人物在目标上方 → 向下滑
            fromY = 700;
            toY = 800;
        }
        else if (cy > kFerdinandTargetROI.y + kFerdinandTargetROI.height) {
            // 人物在目标下方 → 向上滑
            fromY = 800;
            toY = 700;
        }

        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, fromX, fromY, toX, toY, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
    }

    // 尝试次数用完，返回最后一次识别结果
    return isInTargetROI(outBox);
}

// 点击费迪南德进入战斗
static bool AttackFerdinand(MaaContext* context, const MaaRect& ferdinandBox)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    // 点击费迪南德（向上偏移点击人物而不是文字）
    auto id = MaaControllerPostClick(ctrl, ferdinandBox.x + ferdinandBox.width / 2, ferdinandBox.y + ferdinandBox.height / 2 - 50);
    MaaControllerWait(ctrl, id);

    // 等角色移动停止
    postWaitFreezes(context, ScreenArea_CenterCol, 1000, 30000);

    // 点击"发起攻击"进入战斗
    ScreenCap cap(context);
    MaaRect outBox;
    if (doRecognition(
            context,
            cap.img,
            "ferdinandAttack",
            R"({"ferdinandAttack":{"recognition":"OCR","roi":[291, 738, 145, 100],"expected":"发起攻击"}})",
            &outBox,
            nullptr)) {
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto aid = MaaControllerPostClick(ctrl, outBox.x + outBox.width / 2, outBox.y + outBox.height / 2);
        MaaControllerWait(ctrl, aid);
        return true;
    }
    return false;
}

void resetMap(MaaContext* context, MaaTaskId task_id, const std::string& curMapName)
{
    // 1. 进入一个不同的地图重置位置
    auto mapEntries = getCurMapList(context);
    if (mapEntries.is_array()) {
        for (auto& item : mapEntries.as_array()) {
            std::string text = item["text"].as_string();
            if (!text.empty() && text != curMapName) {
                enterMapByName(context, text);
                break;
            }
        }
    }

    // 2. 退出返回营火
    exitToCampfire(context);

    // 3. 点营火，重新进入目标地图
    clickCampfire(context);
    enterMapByName(context, curMapName);
}

bool findFerdinandAndAttack(MaaContext* context, MapIndex mapIndex)
{
    MaaRect ferdinandBox;
    bool found = false;
    switch (mapIndex) {
    case MapIndex::IsleOfTimesEnd:
        found = findFerdinandInIsleOfTimesEnd(context, ferdinandBox);
        break;
    case MapIndex::CrescentCounty:
        found = findFerdinandInCrescentCounty(context, ferdinandBox);
        break;
    case MapIndex::HolyCityMonastery:
        found = findFerdinandInHolyCityMonastery(context, ferdinandBox);
        break;
    case MapIndex::Map_Gremid:
        found = findFerdinandInGremid(context, ferdinandBox);
        break;
    case MapIndex::FurnaceCity:
        found = findFerdinandInFurnaceCity(context, ferdinandBox);
        break;
    default:
        return false;
    }
    if (!found) {
        return false;
    }

    return AttackFerdinand(context, ferdinandBox);
}

// 时之尽头的孤岛
static bool findFerdinandInIsleOfTimesEnd(MaaContext* context, MaaRect& ferdinandBox)
{
    waitArriveMap(context);
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    // 往左滑动找费迪南德
    auto id = MaaControllerPostSwipe(ctrl, 500, 800, 400, 800, 100);
    MaaControllerWait(ctrl, id);
    postWaitFreezes(context);

    if (HasFerdinand(context, ferdinandBox)) {
        ScreenCap cap(context);
        MaaRect outBox;
        // 识别寒石堡垒
        if (!waitUntilRecognitionSuccess(context, "HasColdstoneFortress", "{}", &outBox, nullptr)) {
            return false;
        }
        // 点击寒石堡垒
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        id = MaaControllerPostClick(ctrl, outBox.x + outBox.width / 2, outBox.y + outBox.height / 2);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);

        // 点击穿过堡垒
        ScreenCap cap2(context);
        if (!waitUntilRecognitionSuccess(
                context,
                "crossFortress",
                R"({"crossFortress":{"recognition":"OCR","roi":[279, 777, 173, 124],"expected":"穿越堡垒"}})",
                &outBox,
                nullptr)) {
            return false;
        }
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        id = MaaControllerPostClick(ctrl, outBox.x + outBox.width / 2, outBox.y + outBox.height / 2);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    for (int i = 0; i < 3; ++i) {
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, 400, 800, 500, 800, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    return false;
}

// 新月郡
static bool findFerdinandInCrescentCounty(MaaContext* context, MaaRect& ferdinandBox)
{
    waitArriveMap(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }
    // 往左滑动四次找
    for (int i = 0; i < 4; ++i) {
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, 500, 800, 400, 800, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    // 往上滑动找
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostSwipe(ctrl, 500, 800, 500, 700, 100);
    MaaControllerWait(ctrl, id);
    postWaitFreezes(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }
    return false;
}

// 圣城修道院
static bool findFerdinandInHolyCityMonastery(MaaContext* context, MaaRect& ferdinandBox)
{
    waitArriveMap(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    // 往上滑动找
    auto id = MaaControllerPostSwipe(ctrl, 500, 800, 500, 700, 100);
    MaaControllerWait(ctrl, id);
    postWaitFreezes(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }

    // 往右滑动找
    ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    id = MaaControllerPostSwipe(ctrl, 400, 800, 500, 800, 100);
    MaaControllerWait(ctrl, id);
    postWaitFreezes(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }
    // 往左滑动五次找
    for (int i = 0; i < 5; ++i) {
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, 500, 800, 400, 800, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    return false;
}

// 格雷米德
static bool findFerdinandInGremid(MaaContext* context, MaaRect& ferdinandBox)
{
    waitArriveMap(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));

    // 往左滑动找
    auto id = MaaControllerPostSwipe(ctrl, 500, 800, 400, 800, 100);
    MaaControllerWait(ctrl, id);
    postWaitFreezes(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }

    // 往右滑动找
    for (int i = 0; i < 3; ++i) {
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        id = MaaControllerPostSwipe(ctrl, 200, 800, 300, 800, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    return false;
}

// 熔炉之城
static bool findFerdinandInFurnaceCity(MaaContext* context, MaaRect& ferdinandBox)
{
    waitArriveMap(context);
    if (HasFerdinand(context, ferdinandBox)) {
        return true;
    }
    // 往右滑动3次找
    for (int i = 0; i < 3; ++i) {
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, 500, 800, 600, 800, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    // 往下滑动3次找
    for (int i = 0; i < 3; ++i) {
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, 500, 800, 500, 900, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    // 往左滑动4次找
    for (int i = 0; i < 4; ++i) {
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, 500, 800, 400, 800, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    // 往上滑动3次找
    for (int i = 0; i < 3; ++i) {
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, 500, 800, 500, 700, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);
        if (HasFerdinand(context, ferdinandBox)) {
            return true;
        }
    }

    return false;
}

// 进入费迪南德所在的地图
// 如果刚开始人物和费迪南德在同一个地图，先进入另一个地图，再返回该地图，重置人物位置，避免人物遮挡费迪南德
bool ferdinandMapEntry(MaaContext* context, MaaTaskId task_id, MapIndex& mapIndex)
{
    // 识别费迪南德和人物所在的地图
    MaaRect ferdinandBox;
    std::string mapOfFerdinand = getMapOfFerdinand(context, &ferdinandBox);
    std::string mapOfRole = getMapOfRole(context);

    if (mapOfFerdinand.empty()) {
        return true; // 未识别到费迪南德
    }

    if (mapOfFerdinand == mapOfRole) {
        // 人物和费迪南德在同一张地图，切换地图再进入，重置人物位置
        resetMap(context, task_id, mapOfFerdinand);
    }
    else {
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto fid = MaaControllerPostClick(ctrl, ferdinandBox.x + ferdinandBox.width / 2, ferdinandBox.y + ferdinandBox.height / 2);
        MaaControllerWait(ctrl, fid);
        if (!waitArriveMap(context)) {
            return false;
        }
    }
    mapIndex = MapIndex::Map_None;
    for (auto& [name, idx] : mapTable) {
        if (mapOfFerdinand.find(name) != std::string::npos) {
            mapIndex = idx;
            break;
        }
    }
    return true;
}

MaaBool findFerdinand(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    if (MaaTaskerStopping(MaaContextGetTasker(context))) {
        return true;
    }

    MapIndex mapIndex = MapIndex::Map_None;
    if (!ferdinandMapEntry(context, task_id, mapIndex)) {
        return false;
    }

    // 未识别到费迪南德，返回主页
    if (mapIndex == MapIndex::Map_None) {
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostClickKey(ctrl, 111); // ESC
        MaaControllerWait(ctrl, id);
        return true;
    }

    return findFerdinandAndAttack(context, mapIndex);
}

void registerCustomFerdinand(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "FindFerdinand", findFerdinand, user_data);
}
