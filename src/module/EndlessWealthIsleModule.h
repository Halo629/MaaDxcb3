#pragma once

#include "MaaFramework/MaaAPI.h"

class EndlessWealthIsleModule
{
public:
    static MaaBool assignChallengeTimes(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customActionName, const char* customActionParam,
                                        MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool exchangeRewards(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                   const char* customActionName, const char* customActionParam,
                                   MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);
};
