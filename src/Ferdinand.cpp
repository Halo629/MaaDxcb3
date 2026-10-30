#include "Comm.h"

// 全局记录：费迪南德所在地图
static MapIndex g_ferdinandMapIndex = MapIndex::Map_None;

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

void registerCustomFerdinand(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "FerdinandMapEntry", ferdinandMapEntry, user_data);
}
