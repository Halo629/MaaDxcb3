#pragma once

#include "MaaFramework/MaaAPI.h"

class CommonActions
{
public:
    static MaaBool clearHitCount(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customActionName, const char* customActionParam,
                                 MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool failTask(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                            const char* customActionName, const char* customActionParam,
                            MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool saveConfig(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customActionName, const char* customActionParam,
                              MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool subTask(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                           const char* customActionName, const char* customActionParam,
                           MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool adjustPosition(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customActionName, const char* customActionParam,
                                  MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool adjustPositionAndClick(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customActionName, const char* customActionParam,
                                          MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool checkRoleInTargetMap(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customRecognitionName, const char* customRecognitionParam,
                                        const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                        MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool findFirstCanClickOption(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                           const char* customRecognitionName, const char* customRecognitionParam,
                                           const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                           MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);
};
