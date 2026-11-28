#pragma once

#include "MaaFramework/MaaAPI.h"

class ArcaneCellModule
{
public:
    static MaaBool arcaneCellBattleFailed(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customActionName, const char* customActionParam,
                                          MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool arcaneCellBattleVictoryRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                                 const char* customActionName, const char* customActionParam,
                                                 MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool arcaneCellBattleQuit(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customRecognitionName, const char* customRecognitionParam,
                                        const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                        MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);

private:
    static int m_consecutiveFails; // 连续失败次数
    static int m_sessionWin;       // 本次胜利次数
    static int m_sessionLose;      // 本次失败次数
};
