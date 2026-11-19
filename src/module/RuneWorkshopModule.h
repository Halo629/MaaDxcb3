#pragma once

#include "MaaFramework/MaaAPI.h"

class RuneWorkshopModule
{
public:
    static MaaBool hasRuneUnlock(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customRecognitionName, const char* customRecognitionParam,
                                 const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                 MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool selectRune(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customRecognitionName, const char* customRecognitionParam,
                              const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                              MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);
};
