#pragma once

#include "MaaFramework/MaaAPI.h"

class RuneUpgradeModule
{
public:
    static MaaBool canUpgrade(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customRecognitionName, const char* customRecognitionParam,
                              const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                              MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool isMaxLevel(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customRecognitionName, const char* customRecognitionParam,
                              const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                              MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);
};
