#pragma once

#include "MaaFramework/MaaAPI.h"

#include <map>
#include <set>
#include <string>
#include <vector>

// ──── 条件解析 ────
struct AffixCheckConfig
{
    std::string ocrKeyword; // OCR 识别关键词（如 "行动间隔"）
    bool readMinusSign;     // 读减号(true)还是加号(false)
    int requiredLevel;      // 出现该词条需要的等级
};

struct ConditionRule
{
    AffixCheckConfig check;
    double threshold;
    std::string displayName; // UI 名称（如 "间隔"），用于匹配魔魂类型
    bool hasPercent = false; // 配置中是否带 %，e.g. 间隔>=3.0% → true, 抗性>=3.0 → false
};

using ConditionGroup = std::map<int, ConditionRule>; // key=检查等级, value=条件规则

class MonsterSoulUpgradeModule
{
public:
    static MaaBool upgradeMonsterSoul(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool configCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                               const char* customActionName, const char* customActionParam,
                               MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool findTargetMonsterSoul(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                         const char* customRecognitionName, const char* customRecognitionParam,
                                         const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                         MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool upgradeMonsterSoulCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                           const char* customRecognitionName, const char* customRecognitionParam,
                                           const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                           MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool finish(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                          const char* customActionName, const char* customActionParam,
                          MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

private:
    static int maxUpgradeCount_;
    static int startGridPos_;
    static bool mongsterSoulFullFlag_;
    static int keepCount_;
    static int decomposeCount_;
    static int curRound_;
    static std::set<std::string> targetSoulNames_;
    static std::vector<ConditionGroup> conditionGroups_;
};
