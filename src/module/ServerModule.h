#pragma once

#include "MaaFramework/MaaAPI.h"

#include <string>
#include <unordered_set>
#include <vector>

class ServerModule
{
public:
    static MaaBool findTargetServer(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                    const char* customRecognitionName, const char* customRecognitionParam,
                                    const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                    MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool GetServerListAct(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                    const char* customActionName, const char* customActionParam,
                                    MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool ServerRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                const char* customActionName, const char* customActionParam,
                                MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool targetServerCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customRecognitionName, const char* customRecognitionParam,
                                     const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                     MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);

private:
    static std::vector<std::string> server_;           // 保持识别顺序
    static std::unordered_set<std::string> serverSeen_; // 去重
};
