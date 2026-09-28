#include "Comm.h"

namespace
{
int huntingGroundFeastSkillLimit = 12;
const char* huntingGroundFeastSkillCount = "狩猎技巧";
std::string huntingGroundFeastCurLevel; // 本次软件运行期间记录的当前狩猎强度

// 难度从低到高：D < C < B < A < S < SS-1 < SS-2 < ... < SS-x，返回 -1 表示未识别
int difficultyRank(const std::string& s)
{
    if (s.empty()) {
        return -1;
    }
    char c = s[0];
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
    ScreenCap cap(context);
    auto detailBuf = MaaStringBufferCreate();
    MaaRect resultBox;
    if (!doRecognition(context, cap.img, "HuntingGroundFeastHuntingSkillCount", "{}", &resultBox, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return false;
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

    LogUtils::log(fmt("本次获得%1个狩猎技巧", count), "#22c55e");
    return true;
}

// 今日获得的狩猎技巧数量是否已达上限（每日上限12个/20个）
MaaBool huntingGroundFeastSkillLimitReached(
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
    return false;
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
    huntingGroundFeastCurLevel = ocrText(context, cap.img, "HuntingGroundFeastCurLevel", "{}", nullptr);
    LogUtils::log(fmt("当前狩猎强度：%1", huntingGroundFeastCurLevel), "#22c55e");
    return true;
}

// 降低难度：点击降低按钮并识别，直到低于当前难度，然后更新当前难度
MaaBool HuntingGroundFeastLowerDifficulty(
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
    int curRank = difficultyRank(huntingGroundFeastCurLevel);
    if (curRank < 0) {
        return false;
    }
    int targetRank = curRank - 1; // 目标：低于记录值一级

    for (int i = 0; i < 35; ++i) {
        // 先识别当前实际难度：战斗结束后游戏会自动调回最高已通过强度（如 SS-4）
        ScreenCap cap(context);
        std::string level = ocrText(context, cap.img, "HuntingGroundFeastCurLevel", "{}", nullptr);
        int cur = difficultyRank(level);
        if (cur < 0) {
            continue; // 未识别到，重试
        }
        if (cur < curRank) {
            huntingGroundFeastCurLevel = level;
            LogUtils::log(fmt("已降低难度至：%1", level), "#22c55e");
            return true;
        }
        clickLowerDifficulty(context, targetRank);
    }
    return false;
}

// 调整难度到当前难度（战斗后游戏会自动调到最高难度）
MaaBool HuntingGroundFeastAdjustLevelToTarget(
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
    int targetRank = difficultyRank(huntingGroundFeastCurLevel);
    if (targetRank < 0) {
        return true; // 未记录当前难度，无需调整
    }

    for (int i = 0; i < 35; ++i) {
        ScreenCap cap(context);
        std::string level = ocrText(context, cap.img, "HuntingGroundFeastCurLevel", "{}", nullptr);
        int curRank = difficultyRank(level);
        if (curRank < 0) {
            continue; // 未识别到，重试
        }
        if (curRank == targetRank) {
            return true;
        }
        if (curRank < targetRank) {
            return false; // 低于目标，无法升高
        }
        clickLowerDifficulty(context, targetRank);
    }
    return false;
}

void registerCustomHuntingGroundFeast(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "HuntingGroundFeastSkillStatistics", huntingGroundFeastSkillStatistics, user_data);
    registerCustomAction(res, "HuntingGroundFeastCurLevelRecord", huntingGroundFeastCurLevelRecord, user_data);
    registerCustomRecognition(res, "HuntingGroundFeastSkillLimitReached", huntingGroundFeastSkillLimitReached, user_data);
    registerCustomRecognition(res, "HuntingGroundFeastLowerDifficulty", HuntingGroundFeastLowerDifficulty, user_data);
    registerCustomRecognition(res, "HuntingGroundFeastAdjustLevelToTarget", HuntingGroundFeastAdjustLevelToTarget, user_data);
}
