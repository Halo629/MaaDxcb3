#pragma once

#include "MaaFramework/MaaAPI.h"

class CelestialIsleModule
{
public:
    static MaaBool gotoMap(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                           const char* customActionName, const char* customActionParam,
                           MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool gotoMinerCamp(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customActionName, const char* customActionParam,
                                 MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool recordEnemyCard(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                   const char* customRecognitionName, const char* customRecognitionParam,
                                   const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                   MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool freeTeleportRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool freeTeleportEnabled(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customRecognitionName, const char* customRecognitionParam,
                                       const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                       MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool trialGroundDoorCanPassRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                                const char* customActionName, const char* customActionParam,
                                                MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool trialGroundDoorCanPass(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customRecognitionName, const char* customRecognitionParam,
                                          const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                          MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool enlightenmentConfidantRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                                const char* customActionName, const char* customActionParam,
                                                MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool enlightenmentConfidant(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customRecognitionName, const char* customRecognitionParam,
                                          const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                          MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool queenFamiliarRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customActionName, const char* customActionParam,
                                       MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool queenFamiliar(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customRecognitionName, const char* customRecognitionParam,
                                 const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                 MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool enemyCardNotRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customRecognitionName, const char* customRecognitionParam,
                                      const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                      MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool gambleDecision(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customActionName, const char* customActionParam,
                                  MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

private:
    static int m_enemyCardCount;
};
