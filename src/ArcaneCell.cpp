#include "Comm.h"

namespace
{
// 奥术监牢战斗全局状态
int g_arcaneCellConsecutiveFails = 0; // 连续失败次数
int g_arcaneCellSessionWin = 0;       // 本次胜利次数
int g_arcaneCellSessionLose = 0;      // 本次失败次数
}

// 战斗失败：连续失败次数 +1，打印日志
MaaBool arcaneCellBattleFailed(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_arcaneCellConsecutiveFails++;
    g_arcaneCellSessionLose++;
    LogUtils::log(fmt("奥术监牢: 战斗失败，连续失败 %1 次", g_arcaneCellConsecutiveFails), "#ef4444");
    return true;
}

// 战斗胜利：连续失败置 0，打印胜利日志 + 本次胜负
MaaBool arcaneCellBattleVictoryRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_arcaneCellConsecutiveFails = 0;
    g_arcaneCellSessionWin++;
    LogUtils::log(fmt("奥术监牢: 战斗胜利，本次 %1 胜 %2 败", g_arcaneCellSessionWin, g_arcaneCellSessionLose), "#22c55e");
    return true;
}

// 判断连续失败是否达到配置次数
MaaBool arcaneCellBattleQuit(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    auto attach = getNodeAttach(context, nodeName);
    bool stopFailReduce = attach.get("stop_fail_reduce", json::value(true)).as_boolean();
    int stopFailCount = attach.get("stop_fail_count", json::value(3)).as_integer();
    if (!stopFailReduce) {
        return false; // 未启用连续失败停止
    }
    return g_arcaneCellConsecutiveFails >= stopFailCount;
}

void registerCustomArcaneCell(MaaResource* res, void* userData)
{
    registerCustomAction(res, "ArcaneCellBattleFailedRecord", arcaneCellBattleFailed, userData);
    registerCustomAction(res, "ArcaneCellBattleVictoryRecord", arcaneCellBattleVictoryRecord, userData);
    registerCustomRecognition(res, "ArcaneCellBattleQuit", arcaneCellBattleQuit, userData);
}
