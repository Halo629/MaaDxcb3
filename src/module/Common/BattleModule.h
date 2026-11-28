#pragma once

#include "MaaFramework/MaaAPI.h"

class BattleModule
{
public:
    static MaaBool battleAi(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                            const char* customActionName, const char* customActionParam,
                            MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);
};
