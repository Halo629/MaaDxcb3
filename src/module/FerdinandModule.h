#pragma once

#include "MaaFramework/MaaAPI.h"
#include "map/MapNavigator.h"

class FerdinandModule
{
public:
    static MaaBool ferdinandMapEntry(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customActionName, const char* customActionParam,
                                     MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

private:
    // 全局记录：费迪南德所在地图
    static MapIndex ferdinandMapIndex_;
};
