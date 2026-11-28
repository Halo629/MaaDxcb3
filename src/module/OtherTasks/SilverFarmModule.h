#pragma once

#include "MaaFramework/MaaAPI.h"

class SilverFarmModule
{
public:
    static MaaBool recordSilver(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                const char* customActionName, const char* customActionParam,
                                MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool clearRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                               const char* customActionName, const char* customActionParam,
                               MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool recordEnterMaze(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                   const char* customActionName, const char* customActionParam,
                                   MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

private:
    static int m_totalSilver;
    static int m_enterMazeCount;
};
