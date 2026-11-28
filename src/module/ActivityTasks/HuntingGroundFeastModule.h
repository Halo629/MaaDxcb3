#pragma once

#include "MaaFramework/MaaAPI.h"

#include <string>

class HuntingGroundFeastModule
{
public:
    static MaaBool skillStatistics(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                   const char* customActionName, const char* customActionParam,
                                   MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool remainingCountCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customRecognitionName, const char* customRecognitionParam,
                                       const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                       MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool init(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                        const char* customActionName, const char* customActionParam,
                        MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool curLevelRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customActionName, const char* customActionParam,
                                  MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool lowerDifficulty(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                   const char* customActionName, const char* customActionParam,
                                   MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool adjustToCurrentLevelCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                             const char* customRecognitionName, const char* customRecognitionParam,
                                             const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                             MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool adjustLevelToTarget(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customActionName, const char* customActionParam,
                                       MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool autoLowerLevelCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customRecognitionName, const char* customRecognitionParam,
                                       const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                       MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool notTargetLevel(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customRecognitionName, const char* customRecognitionParam,
                                  const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                  MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);

private:
    static int m_huntingGroundFeastSkillLimit;
    static std::string m_currentHuntingGroundFeastMaxLevel;
    static std::string m_currentHuntingGroundFeastLevel;
    static std::string m_targetLevel;
    static int m_currentAcquiredHuntingSkills;
    static bool m_currentAcquiredHuntingSkillsLimitReached;
    static int m_huntingGroundFeastConsecutiveFailCount;
    static bool m_hasLowerLevel; // 是否已经降低过难度，如果降低过难度，则胜利后需要调整回胜利时的难度，因为猎场盛宴胜利后自动回到最高难度
    static int m_mode;           // 0:智能模式 1：指定等级
};
