#include "SoulTombModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "config/Config.h"
#include "util/Recognition.h"
#include "util/StringUtil.h"

#include <chrono>
#include <cstdlib>
#include <thread>

int SoulTombModule::m_consecutiveFails = 0;
int SoulTombModule::m_sessionWin = 0;
int SoulTombModule::m_sessionLose = 0;

// ======================== 连续失败降低难度 ========================
MaaBool SoulTombModule::adjustLevel(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = Config::instance().nodeAttach(context, nodeName);
    std::string mode = attach.get("target_level", json::value("current")).as_string();
    bool startAdjust = attach.get("start_adjust", json::value(false)).as_boolean();

    if (startAdjust) {
        m_consecutiveFails = 0;
        m_sessionWin = 0;
        m_sessionLose = 0;
    }
    if (mode == "current") {
        return true; // 不调整
    }

    if (mode == "max") { // 最大等级
        MaaContextRunTask(context, "SoulTombLevelMax", "{}");
        return true;
    }

    if (mode != "specified") {
        return true;
    }

    int targetLevel = attach.get("level", json::value(0)).as_integer();
    if (targetLevel <= 0) {
        LogUtils::error("魂墓挑战: 未指定目标等级");
        return false;
    }

    // OCR 读当前等级
    int curLevel = 0;
    if (!recog::readNumber(context, "CustomGetSoulTombCurLevel", "SoulTombCurLevel", curLevel)) {
        LogUtils::error("魂墓挑战: 读取当前等级失败");
        return false;
    }

    // 提升等级：每次 +1
    auto addLevel = [&]() {
        if (std::abs(targetLevel - curLevel) <= 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        MaaContextRunTask(context, "SoulTombLevelAdd1", "{}");
        return recog::readNumber(context, "CustomGetSoulTombCurLevel", "SoulTombCurLevel", curLevel);
    };

    // 降低等级：差 >=10 级 -10，否则 -1
    auto subLevel = [&]() {
        if (std::abs(targetLevel - curLevel) <= 20) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        int diff = curLevel - targetLevel;
        if (diff >= 10) {
            MaaContextRunTask(context, "SoulTombLevelSub10", "{}");
        }
        else {
             if (std::abs(targetLevel - curLevel) <= 3) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            MaaContextRunTask(context, "SoulTombLevelSub1", "{}");
        }
        return recog::readNumber(context, "CustomGetSoulTombCurLevel", "SoulTombCurLevel", curLevel);
    };

    while (curLevel != targetLevel) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return true;
        }
        bool ok = (curLevel < targetLevel) ? addLevel() : subLevel();
        if (!ok) {
            LogUtils::error("魂墓挑战: 读取当前等级失败");
            return false;
        }
    }

    return true;
}

// 战斗失败：只记录连续失败次数和本次失败次数
MaaBool SoulTombModule::battleFailedRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    m_consecutiveFails++;
    m_sessionLose++;
    LogUtils::warn(fmt("魂墓挑战: 战斗失败，本次 %1 胜 %2 败", m_sessionWin, m_sessionLose));
    return true;
}

// 战斗胜利：连续失败置 0，打印胜利日志 + 本次胜负
MaaBool SoulTombModule::battleVictoryRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    m_consecutiveFails = 0;
    m_sessionWin++;
    LogUtils::info(fmt("魂墓挑战: 战斗胜利，本次 %1 胜 %2 败", m_sessionWin, m_sessionLose));
    return true;
}

// 判断连续失败是否达到配置次数（达到则自动降低难度）
MaaBool SoulTombModule::battleAutoLowerLevel(
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
    auto attach = Config::instance().nodeAttach(context, nodeName);
    bool lowerOnFail = attach.get("lower_on_fail", json::value(true)).as_boolean();
    int lowerFailCount = attach.get("lower_fail_count", json::value(5)).as_integer();

    if (lowerOnFail && m_consecutiveFails >= lowerFailCount) {
        LogUtils::warn(fmt("魂墓挑战: 连续失败%1次,降低10级难度", m_consecutiveFails));
        m_consecutiveFails = 0;
        return true;
    }
    return false;
}

namespace
{

MaaBool adjustLevelThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                         const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return SoulTombModule::adjustLevel(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool battleFailedRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return SoulTombModule::battleFailedRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool battleVictoryRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                 const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return SoulTombModule::battleVictoryRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool battleAutoLowerLevelThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                  void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return SoulTombModule::battleAutoLowerLevel(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void SoulTombModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "AdjustSoulTombLevel", adjustLevelThunk, userData);
    registerCustomAction(res, "SoulTombBattleFailedRecord", battleFailedRecordThunk, userData);
    registerCustomAction(res, "SoulTombBattleVictoryRecord", battleVictoryRecordThunk, userData);
    registerCustomRecognition(res, "SoulTombBattleAutoLowerLevel", battleAutoLowerLevelThunk, userData);
}
