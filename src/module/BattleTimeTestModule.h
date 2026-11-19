#pragma once

#include "MaaFramework/MaaAPI.h"

class BattleTimeTestModule
{
public:
    static MaaBool battleTimeTestAct(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customActionName, const char* customActionParam,
                                     MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);
};
