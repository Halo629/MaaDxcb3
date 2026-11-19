#pragma once

#include "MaaFramework/MaaAPI.h"

class ExpressionRecognizer
{
public:
    static MaaBool recognize(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                             const char* customRecognitionName, const char* customRecognitionParam,
                             const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                             MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);
};
