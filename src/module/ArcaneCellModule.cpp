#include "ArcaneCellModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "config/Config.h"
#include "util/StringUtil.h"

// 奥术监牢战斗全局状态
int ArcaneCellModule::consecutiveFails_ = 0; // 连续失败次数
int ArcaneCellModule::sessionWin_ = 0;       // 本次胜利次数
int ArcaneCellModule::sessionLose_ = 0;      // 本次失败次数

// 战斗失败：连续失败次数 +1，打印日志
MaaBool ArcaneCellModule::arcaneCellBattleFailed(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    consecutiveFails_++;
    sessionLose_++;
    LogUtils::error(fmt("奥术监牢: 战斗失败，连续失败 %1 次", consecutiveFails_));
    return true;
}

// 战斗胜利：连续失败置 0，打印胜利日志 + 本次胜负
MaaBool ArcaneCellModule::arcaneCellBattleVictoryRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    consecutiveFails_ = 0;
    sessionWin_++;
    LogUtils::info(fmt("奥术监牢: 战斗胜利，本次 %1 胜 %2 败", sessionWin_, sessionLose_));
    return true;
}

// 判断连续失败是否达到配置次数
MaaBool ArcaneCellModule::arcaneCellBattleQuit(
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
    auto attach = Config::nodeAttach(context, nodeName);
    bool stopFailReduce = attach.get("stop_fail_reduce", json::value(true)).as_boolean();
    int stopFailCount = attach.get("stop_fail_count", json::value(3)).as_integer();
    if (!stopFailReduce) {
        return false; // 未启用连续失败停止
    }
    return consecutiveFails_ >= stopFailCount;
}

namespace {

MaaBool arcaneCellBattleFailedThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                    const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return ArcaneCellModule::arcaneCellBattleFailed(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool arcaneCellBattleVictoryRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                           const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return ArcaneCellModule::arcaneCellBattleVictoryRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool arcaneCellBattleQuitThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                  void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return ArcaneCellModule::arcaneCellBattleQuit(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void ArcaneCellModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "ArcaneCellBattleFailedRecord", arcaneCellBattleFailedThunk, userData);
    registerCustomAction(res, "ArcaneCellBattleVictoryRecord", arcaneCellBattleVictoryRecordThunk, userData);
    registerCustomRecognition(res, "ArcaneCellBattleQuit", arcaneCellBattleQuitThunk, userData);
}
