#include "Comm.h"

namespace
{
// ======================== 连续失败降低难度 ========================
int g_soulTombConsecutiveFails = 0; // 连续失败次数
int g_soulTombSessionWin = 0;       // 本次胜利次数
int g_soulTombSessionLose = 0;      // 本次失败次数

MaaBool adjustSoulTombLevel(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_soulTombConsecutiveFails = 0;
    g_soulTombSessionWin = 0;
    g_soulTombSessionLose = 0;

    auto attach = getNodeAttach(context, nodeName);
    std::string mode = attach.get("target_level", json::value("current")).as_string();

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
        LogUtils::log("魂墓挑战: 未指定目标等级", "#ef4444");
        return false;
    }

    // OCR 读当前等级
    ScreenCap cap(context);
    std::string digits = ocrText(context, cap.img, "CustomGetSoulTombCurLevel", nullptr, nullptr);

    if (digits.empty()) {
        LogUtils::log("魂墓挑战: 读取当前等级失败", "#f59e0b");
        return false;
    }
    int curLevel = std::stoi(digits);

    if (curLevel == targetLevel) {
        return true;
    }

    if (curLevel < targetLevel) {
        for (int i = 0; i < targetLevel - curLevel; ++i) {
            if (MaaTaskerStopping(MaaContextGetTasker(context))) {
                return true;
            }
            MaaContextRunTask(context, "SoulTombLevelAdd1", "{}");
        }
    }
    else {
        int diff = curLevel - targetLevel;
        for (int i = 0; i < diff / 10; ++i) {
            if (MaaTaskerStopping(MaaContextGetTasker(context))) {
                return true;
            }
            MaaContextRunTask(context, "SoulTombLevelSub10", "{}");
        }
        for (int i = 0; i < diff % 10; ++i) {
            if (MaaTaskerStopping(MaaContextGetTasker(context))) {
                return true;
            }
            MaaContextRunTask(context, "SoulTombLevelSub1", "{}");
        }
    }
    return true;
}

// 战斗失败：只记录连续失败次数和本次失败次数
MaaBool soulTombBattleFailedRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_soulTombConsecutiveFails++;
    g_soulTombSessionLose++;
    LogUtils::log(fmt("魂墓挑战: 战斗失败，本次 %1 胜 %2 败", g_soulTombSessionWin, g_soulTombSessionLose), "#ef4444");
    return true;
}

// 战斗胜利：连续失败置 0，打印胜利日志 + 本次胜负
MaaBool soulTombBattleVictoryRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_soulTombConsecutiveFails = 0;
    g_soulTombSessionWin++;
    LogUtils::log(fmt("魂墓挑战: 战斗胜利，本次 %1 胜 %2 败", g_soulTombSessionWin, g_soulTombSessionLose), "#22c55e");
    return true;
}

// 判断连续失败是否达到配置次数（达到则自动降低难度）
MaaBool soulTombBattleAutoLowerLevel(
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
    bool lowerOnFail = attach.get("lower_on_fail", json::value(true)).as_boolean();
    int lowerFailCount = attach.get("lower_fail_count", json::value(5)).as_integer();

    if (lowerOnFail && g_soulTombConsecutiveFails >= lowerFailCount) {
        LogUtils::log(fmt("魂墓挑战: 连续失败%1次,降低10级难度", g_soulTombConsecutiveFails), "#ef4444");
        g_soulTombConsecutiveFails = 0;
        return true;
    }
    return false;
}

} // namespace

void registerCustomSoulTomb(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "AdjustSoulTombLevel", adjustSoulTombLevel, user_data);
    registerCustomAction(res, "SoulTombBattleFailedRecord", soulTombBattleFailedRecord, user_data);
    registerCustomAction(res, "SoulTombBattleVictoryRecord", soulTombBattleVictoryRecord, user_data);
    registerCustomRecognition(res, "SoulTombBattleAutoLowerLevel", soulTombBattleAutoLowerLevel, user_data);
}
