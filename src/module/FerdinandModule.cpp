#include "FerdinandModule.h"

#include "Registry.h"
#include "map/MapNavigator.h"

MapIndex FerdinandModule::ferdinandMapIndex_ = MapIndex::Map_None;

// 读取费迪南德所在地图，把 next 定向到对应的 SceneEnterXXX 节点
MaaBool FerdinandModule::ferdinandMapEntry(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    MaaRect ferdinandBox;
    std::string mapOfFerdinand = MapNavigator::mapOfFerdinand(context, &ferdinandBox);

    ferdinandMapIndex_ = MapIndex::Map_None; // 每次进入清空

    const char* nextNode = "";
    for (auto& [name, idx] : MapNavigator::mapTable()) {
        if (!mapOfFerdinand.empty() && mapOfFerdinand.find(name) != std::string::npos) {
            ferdinandMapIndex_ = idx; // 记录费迪南德所在地图
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

    MapNavigator::setNextNode(context, nodeName, nextNode);
    return true;
}

namespace {

MaaBool ferdinandMapEntryThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                               const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return FerdinandModule::ferdinandMapEntry(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void FerdinandModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "FerdinandMapEntry", ferdinandMapEntryThunk, userData);
}
