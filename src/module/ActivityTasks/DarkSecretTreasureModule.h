#pragma once

#include "MaaFramework/MaaAPI.h"

class DarkSecretTreasureModule
{
public:
    static MaaBool goToClueMap(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                               const char* customActionName, const char* customActionParam,
                               MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool seekClueTreasure(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                    const char* customActionName, const char* customActionParam,
                                    MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);
};
