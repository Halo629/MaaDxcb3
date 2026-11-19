#pragma once

#include "MaaFramework/MaaAPI.h"

class SoulTombModule
{
public:
    static MaaBool adjustLevel(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                               const char* customActionName, const char* customActionParam,
                               MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool battleFailedRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool battleVictoryRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customActionName, const char* customActionParam,
                                       MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool battleAutoLowerLevel(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customRecognitionName, const char* customRecognitionParam,
                                        const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                        MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);

private:
    static int consecutiveFails_;
    static int sessionWin_;
    static int sessionLose_;
};
