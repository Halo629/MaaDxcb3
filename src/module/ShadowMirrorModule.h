#pragma once

#include "MaaFramework/MaaAPI.h"

class ShadowMirrorModule
{
public:
    static MaaBool record(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                          const char* customActionName, const char* customActionParam,
                          MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool countCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customRecognitionName, const char* customRecognitionParam,
                              const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                              MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);

private:
    static int remainingShards_;
};
