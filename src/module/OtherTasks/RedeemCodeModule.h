#pragma once

#include "MaaFramework/MaaAPI.h"

#include <cstddef>
#include <string>
#include <vector>

class RedeemCodeModule
{
public:
    static MaaBool inputCodes(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customActionName, const char* customActionParam,
                              MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool isComplete(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customRecognitionName, const char* customRecognitionParam,
                              const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                              MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool inputSingle(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                               const char* customActionName, const char* customActionParam,
                               MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

private:
    static std::vector<std::string> m_codes;
    static size_t m_index;
};
