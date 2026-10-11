#include "Comm.h"

// 全局记录：费迪南德所在地图
static MapIndex g_ferdinandMapIndex = MapIndex::Map_None;

// 目标区域：费迪南德需要在此范围内才能点击
static constexpr MaaRect kFerdinandTargetROI = { 196, 436, 402, 685 };

// 检查 box 中心是否在目标区域内
static bool isInTargetROI(const MaaRect& box)
{
    int cx = box.x + box.width / 2;
    int cy = box.y + box.height / 2;
    return cx >= kFerdinandTargetROI.x && cx <= kFerdinandTargetROI.x + kFerdinandTargetROI.width && cy >= kFerdinandTargetROI.y
           && cy <= kFerdinandTargetROI.y + kFerdinandTargetROI.height;
}

// 读取费迪南德所在地图，把 next 定向到对应的 SceneEnterXXX 节点
MaaBool ferdinandMapEntry(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    MaaRect ferdinandBox;
    std::string mapOfFerdinand = getMapOfFerdinand(context, &ferdinandBox);

    g_ferdinandMapIndex = MapIndex::Map_None; // 每次进入清空

    const char* nextNode = "";
    for (auto& [name, idx] : mapTable) {
        if (!mapOfFerdinand.empty() && mapOfFerdinand.find(name) != std::string::npos) {
            g_ferdinandMapIndex = idx; // 记录费迪南德所在地图
            switch (idx) {
            case MapIndex::IsleOfTimesEnd:
                nextNode = "SceneEnterIsleOfTimesEnd";
                break;
            case MapIndex::CrescentCounty:
                nextNode = "SceneEnterCrescentCounty";
                break;
            case MapIndex::HolyCityMonastery:
                nextNode = "SceneEnterHolyCityMonastery";
                break;
            case MapIndex::Map_Gremid:
                nextNode = "SceneEnterGremid";
                break;
            case MapIndex::FurnaceCity:
                nextNode = "SceneEnterFurnaceCity";
                break;
            default:
                return false;
            }
            break;
        }
    }

    setNextNode(context, node_name, nextNode);
    return true;
}

// 自定义动作：微调费迪南德位置（不在目标框内就滑动）
MaaBool adjustFerdinandPosition(MaaContext* context, MaaTaskId, const char*, const char*, const char*, MaaRecoId, const MaaRect* box, void*)
{
    MaaRect ferdinandBox = box ? *box : MaaRect { 0, 0, 0, 0 };

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (isInTargetROI(ferdinandBox)) {
            return true;
        }

        // 计算偏差方向并滑动
        int cx = ferdinandBox.x + ferdinandBox.width / 2;
        int cy = ferdinandBox.y + ferdinandBox.height / 2;

        int fromX = 500, fromY = 800, toX = 500, toY = 800;
        if (cx < kFerdinandTargetROI.x) {
            fromX = 300;
            toX = 400;
        }
        else if (cx > kFerdinandTargetROI.x + kFerdinandTargetROI.width) {
            fromX = 500;
            toX = 400;
        }
        if (cy < kFerdinandTargetROI.y) {
            fromY = 700;
            toY = 800;
        }
        else if (cy > kFerdinandTargetROI.y + kFerdinandTargetROI.height) {
            fromY = 800;
            toY = 700;
        }

        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, fromX, fromY, toX, toY, 100);
        MaaControllerWait(ctrl, id);
        postWaitFreezes(context);

        // 重新识别费迪南德
        ScreenCap cap(context);
        if (!doRecognition(context, cap.img, "HasFerdinand", "{}", &ferdinandBox, nullptr)) {
            return false;
        }
    }

    return isInTargetROI(ferdinandBox);
}

void registerCustomFerdinand(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "FerdinandMapEntry", ferdinandMapEntry, user_data);
    registerCustomAction(res, "AdjustFerdinandPosition", adjustFerdinandPosition, user_data);
}
