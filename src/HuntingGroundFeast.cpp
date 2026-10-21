#include "Comm.h"


int g_huntingGroundFeastSkillLimit = 12;
static std::string g_currentHuntingGroundFeastMaxLevel; 
static std::string g_currentHuntingGroundFeastLevel; 
static std::string g_targetLevel; 
static int g_currentAcquiredHuntingSkills = 0;
static bool g_currentAcquiredHuntingSkillsLimitReached = false;
static int g_huntingGroundFeastConsecutiveFailCount = 0;
static bool g_hasLowerLevel = false; // 是否已经降低过难度，如果降低过难度，则胜利后需要调整回胜利时的难度，因为猎场盛宴胜利后自动回到最高难度
static int g_mode = 0;//0:智能模式 1：指定等级

// 难度从低到高：D < C < B < A < S < SS-1 < SS-2 < ... < SS-x，返回 -1 表示未识别
int difficultyRank(const std::string& s)
{
    if (s.empty()) {
        return -1;
    }
    char c = s[0];
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    if (c == 'D') {
        return 0;
    }
    if (c == 'C') {
        return 1;
    }
    if (c == 'B') {
        return 2;
    }
    if (c == 'A') {
        return 3;
    }
    if (c == 'S') {
        if (s.size() == 1) {
            return 4; // S
        }
        std::string digits;
        for (char ch : s) {
            if (ch >= '0' && ch <= '9') {
                digits += ch;
            }
        }
        int n = digits.empty() ? 1 : std::atoi(digits.c_str());
        return 4 + n; // SS-1=5, SS-2=6, ...
    }
    return -1;
}

void clickLowerDifficulty(MaaContext* context, int targetRank)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    // 目标在 SS 段（>=5）用下面的按钮逐级降 SS；目标在 S 及以下（<=4）用上面的按钮 SS-x 直接跳 S
    bool targetInSS = targetRank >= 5;
    int clickX = targetInSS ? 229 : 130;
    int clickY = targetInSS ? 686 : 492;
    auto id = MaaControllerPostClick(ctrl, clickX, clickY);
    MaaControllerWait(ctrl, id);
    Sleep(500);
}

void clickIncreaseDifficulty(MaaContext* context, int targetRank)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    // 目标在 SS 段（>=5）用下面的按钮增加难度；目标在 S 及以下（<=4）用上面的按钮
    bool targetInSS = targetRank >= 5;
    int clickX = targetInSS ? 490 : 600;
    int clickY = targetInSS ? 685 : 500;
    auto id = MaaControllerPostClick(ctrl, clickX, clickY);
    MaaControllerWait(ctrl, id);
    Sleep(500);
}

// 从 And 节点 detail 中提取指定 sub_name 的 OCR 文本
std::string ocrTextFromAnd(MaaContext* context, const MaaImageBuffer* img, const char* entry, const char* subName)
{
    auto detailBuf = MaaStringBufferCreate();
    MaaRect box;
    if (!doRecognition(context, img, entry, "{}", &box, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto j = json::parse(detail).value_or(json::value { });

    std::string result;
    if (!j.is_array()) {
        return { };
    }
    for (auto& item : j.as_array()) {
        if (!item.is_object()) {
            continue;
        }
        auto& name = item["name"];
        if (!name.is_string() || name.as_string() != subName) {
            continue;
        }
        auto& sub = item["detail"];
        if (!sub.is_object()) {
            continue;
        }
        if (auto& all = sub["all"]; all.is_array()) {
            for (auto& r : all.as_array()) {
                result += r["text"].as_string();
            }
        }
    }
    return result;
}

// 统计今日获得的狩猎技巧数量
MaaBool huntingGroundFeastSkillStatistics(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_huntingGroundFeastConsecutiveFailCount = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ScreenCap cap(context);
    auto detailBuf = MaaStringBufferCreate();
    MaaRect resultBox;
    if (!doRecognition(context, cap.img, "HuntingGroundFeastHuntingSkillCount", "{}", &resultBox, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        g_currentAcquiredHuntingSkillsLimitReached = true;
        LogUtils::log(fmt("战斗胜利，本次战斗未获得狩猎技巧，今日获取狩猎技巧已达上限"), "#22c55e");
        return true;
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto j = json::parse(detail).value_or(json::value { });

    // 只取指定子识别（sub_name = HuntingSkillCount）识别到的数字并求和
    int total = 0;
    auto addText = [&total](const std::string& t) {
        std::string digits;
        for (char c : t) {
            if (c >= '0' && c <= '9') {
                digits += c;
            }
        }
        if (!digits.empty()) {
            total += std::atoi(digits.c_str());
        }
    };

    auto extractFrom = [&addText](json::value& detailObj) {
        if (auto& all = detailObj["all"]; all.is_array()) {
            for (auto& r : all.as_array()) {
                addText(r["text"].as_string());
            }
        }
    };

    if (j.is_array()) {
        // HuntingGroundFeastHuntingSkillCount 是 And 节点，detail 是子识别结果数组
        for (auto& item : j.as_array()) {
            if (!item.is_object()) {
                continue;
            }
            auto& name = item["name"];
            if (!name.is_string() || name.as_string() != "HuntingSkillCount") {
                continue;
            }
            auto& sub = item["detail"];
            if (sub.is_object()) {
                extractFrom(sub);
            }
        }
    }
    else if (j.is_object()) {
        extractFrom(j);
    }

    int count = total > 0 ? total : 2;
    if (count > 2) {
        // 祭典期间一次可获取5个技巧，上限变为20个
        g_huntingGroundFeastSkillLimit = 20;
    }
    g_currentAcquiredHuntingSkills += count;

    LogUtils::log(fmt("战斗胜利，获得%1个狩猎技巧，本次已获取%2个狩猎技巧", count, g_currentAcquiredHuntingSkills), "#22c55e");
    return true;
}

MaaBool huntingFeastRemainingCountCheck(
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
   return !g_currentAcquiredHuntingSkillsLimitReached && (g_currentAcquiredHuntingSkills < g_huntingGroundFeastSkillLimit);
}

MaaBool huntingGroundFeastInit(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_currentHuntingGroundFeastLevel = "";
    g_targetLevel = "";
    g_currentAcquiredHuntingSkills = 0;
    g_currentAcquiredHuntingSkillsLimitReached = 0;
    g_huntingGroundFeastConsecutiveFailCount = 0;
    g_mode = 0;
    ScreenCap cap(context);
    g_currentHuntingGroundFeastMaxLevel = ocrTextFromAnd(context, cap.img, "HuntingGroundFeastCurLevel", "CurLevelText");
    return true;
}

// 记录当前狩猎强度
MaaBool huntingGroundFeastCurLevelRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    ScreenCap cap(context);
    g_currentHuntingGroundFeastLevel = ocrTextFromAnd(context, cap.img, "HuntingGroundFeastCurLevel", "CurLevelText");
    LogUtils::log(fmt("当前狩猎强度：%1", g_currentHuntingGroundFeastLevel), "#22c55e");
    return true;
}

// 降低难度：点击降低按钮并识别，直到低于当前难度，然后更新当前难度
MaaBool HuntingGroundFeastLowerDifficulty(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_huntingGroundFeastConsecutiveFailCount = 0;
    g_hasLowerLevel = true;
    int curRank = difficultyRank(g_currentHuntingGroundFeastLevel);
    if (curRank < 0) {
        return false;
    }
    int targetRank = curRank - 1; // 目标：低于记录值一级

    for (int i = 0; i < 35; ++i) {
        // 先识别当前实际难度：战斗结束后游戏会自动调回最高已通过强度（如 SS-4）
        ScreenCap cap(context);
        std::string level = ocrTextFromAnd(context, cap.img, "HuntingGroundFeastCurLevel", "CurLevelText");
        int cur = difficultyRank(level);
        if (cur < 0) {
            continue; // 未识别到，重试
        }
        if (cur < curRank) {
            g_currentHuntingGroundFeastLevel = level;
            LogUtils::log(fmt("已降低难度至：%1", level), "#22c55e");
            return true;
        }
        clickLowerDifficulty(context, targetRank);
    }
    return false;
}

// 检查是否需要调整回当前难度
MaaBool adjustToCurrentLevelCheck(
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
    // 在已经降低过难度的情况下才调整到上次胜利时的等级
    return g_hasLowerLevel;
}

// 调整难度到当前难度（战斗后游戏会自动调到最高难度）
MaaBool HuntingGroundFeastAdjustLevelToTarget(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    int targetRank = difficultyRank(g_currentHuntingGroundFeastLevel);
    auto attach = getNodeAttach(context, nodeName);
    std::string mode = attach.get("target_level", json::value("current")).as_string();
    if (mode == "specified") {
        std::string targetLevel = attach.get("level", json::value("S")).as_string();
        targetRank = difficultyRank(targetLevel);
        if (targetRank == -1) {
            LogUtils::log("目标等级填写错误,将切换为智能模式执行", "#ef4444");
            return true;
        }

        int maxLevel = difficultyRank(g_currentHuntingGroundFeastMaxLevel);
        if (targetRank <= 4 && targetRank > maxLevel) {
            LogUtils::log("指定的目标等级低于SS强度，且高于当前通过的最大强度,无法调整，将切换为智能模式执行", "#ef4444");
            return true;
        }
        g_targetLevel = targetLevel;
        g_mode = 1;
    }

    // 智能模式下，如果本次还没有降低过难度，就不调整，因为活动刚开始的时候，一直胜利，不需要调整
    if (g_mode == 0 && !g_hasLowerLevel) {
        return true;
    }
    
    if (targetRank < 0) {
        return true; // 未记录当前难度，无需调整
    }

    for (int i = 0; i < 45; ++i) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return true;
        }
        ScreenCap cap(context);
        std::string level = ocrTextFromAnd(context, cap.img, "HuntingGroundFeastCurLevel", "CurLevelText");
        int curRank = difficultyRank(level);
        if (curRank < 0) {
            continue; // 未识别到，重试
        }
        if (curRank == targetRank) {
            return true;
        }
        if (curRank < targetRank) {
            clickIncreaseDifficulty(context, targetRank);
        } else {
            clickLowerDifficulty(context, targetRank);
        }
    }
    LogUtils::log("调整等级出错", "#ef4444");
    return false;
}

// 判断连续失败是否达到配置次数（达到则自动降低难度）
MaaBool huntingGroundFeastAutoLowerLevelCheck(
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
     if (g_mode == 1) {
        return true;
    }
    auto attach = getNodeAttach(context, nodeName);
    bool lowerOnFail = attach.get("lower_on_fail", json::value(true)).as_boolean();
    int lowerFailCount = attach.get("lower_fail_count", json::value(2)).as_integer();

    if (lowerOnFail && ++g_huntingGroundFeastConsecutiveFailCount >= lowerFailCount) {
        LogUtils::log(fmt("狩猎盛宴: 连续失败%1次,降低1级难度", g_huntingGroundFeastConsecutiveFailCount), "#f59e0b");
        return true;
    }
    return false;
}

MaaBool notTargetLevel(
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
    ScreenCap cap(context);
    auto curLevel = ocrTextFromAnd(context, cap.img, "HuntingGroundFeastCurLevel", "CurLevelText");
    return difficultyRank(g_targetLevel) != difficultyRank(curLevel);
}

void registerCustomHuntingGroundFeast(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "HuntingGroundFeastSkillStatistics", huntingGroundFeastSkillStatistics, user_data);
    registerCustomAction(res, "HuntingGroundFeastCurLevelRecord", huntingGroundFeastCurLevelRecord, user_data);
    registerCustomAction(res, "HuntingGroundFeastInit", huntingGroundFeastInit, user_data);
    registerCustomAction(res, "HuntingGroundFeastLowerDifficulty", HuntingGroundFeastLowerDifficulty, user_data);
    registerCustomAction(res, "HuntingGroundFeastAdjustLevelToTarget", HuntingGroundFeastAdjustLevelToTarget, user_data);
    registerCustomRecognition(res, "AdjustToCurrentLevelCheck", adjustToCurrentLevelCheck, user_data);
    registerCustomRecognition(res, "HuntingFeastRemainingCountCheck", huntingFeastRemainingCountCheck, user_data);
    registerCustomRecognition(res, "HuntingGroundFeastAutoLowerLevelCheck", huntingGroundFeastAutoLowerLevelCheck, user_data);
    registerCustomRecognition(res, "NotTargetLevel", notTargetLevel, user_data);
}
