#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

// 战吼滑动坐标（3 列）
struct SwipeCol
{
    std::pair<int, int> begin = { 0, 0 };
    std::pair<int, int> end = { 0, 0 };
    int duration = 100;
};

// 战斗配置通用参数
struct BattleConfigParam
{
    int beanColorLower[3] = { 10, 80, 50 };
    int beanColorUpper[3] = { 35, 255, 255 };
    int beanColorCount = 250;

    SwipeCol cryCols[3] = { { { 150, 1050 }, { 150, 750 } }, { { 350, 1050 }, { 350, 750 } }, { { 550, 1050 }, { 550, 750 } } };
};

enum class Skill : int
{
    None = 0,
    Xiaoqing = 1,
};

// 战斗配置（界面参数，默认全 0 表示未配置）
struct UserBattleConfig
{
    BattleConfigParam configParam;
    int cryAnywayThreshold = 6;
    int reserve = 0;
    int cryInterval = 0;
    int burstSeconds = 0;
    int firstSkillTime = 0;
    Skill skill = Skill::None;
    int cryColumn = 0;
};

enum class BattleCryDecision
{
    None = 0,
    Normal,
    Burst,
};

// 魔魂保留词条组配置：是否启用 + 各词条的开关与阈值
struct SoulGroupConfig
{
    bool enabled = false; // 是否启用该组

    bool interval = false; // 间隔（开关）
    bool damage = false;   // 承伤（开关）
    bool resist = false;   // 抗性（开关）
    bool crit = false;     // 暴击（开关）
    bool heal = false;     // 治疗效果（开关）

    double intervalValue = 0.0; // 间隔阈值
    double damageValue = 0.0;   // 承伤阈值
    double resistValue = 0.0;   // 抗性阈值
    double critValue = 0.0;     // 暴击阈值
    double healValue = 0.0;     // 治疗效果阈值
};

// 魔魂升级配置（界面参数，默认全 0/false 表示未配置）
struct SoulUpgradeConfig
{
    int countMode = -1;
    int count = 0;

    bool soulYexing = false, soulJielue = false, soulWugu = false, soulBaolue = false;
    bool soulXiongsha = false, soulXiedian = false, soulJiaozhi = false, soulManhuang = false;
    bool soulXiongshi = false, soulKuangnu = false, soulShuangxue = false, soulBingjing = false;
    bool soulLinye = false, soulLingdong = false, soulJihan = false, soulBaopo = false;
    bool soulChenyuan = false, soulKuangre = false, soulTianyun = false, soulXianzu = false;

    SoulGroupConfig group1;
    SoulGroupConfig group2;
    SoulGroupConfig group3;
};

// 全魔魂字典
const std::set<std::string> allSoulNames = { "巫蛊之魂", "暴虐之魂", "野性之魂", "凶煞之魂", "邪典之魂", "劫掠之魂", "狡智之魂",
                                             "蛮荒之魂", "雄狮之魂", "狂怒之魂", "霜雪之魂", "冰晶之魂", "林野之魂", "林动之魂",
                                             "极寒之魂", "爆破之魂", "沉渊之魂", "狂热之魂", "天陨之魂", "先祖之魂" };

// 魔魂字段描述：结构体字段 -> 中文名 -> JSON key
struct SoulField {
    bool SoulUpgradeConfig::* field;
    const char* cnName;
    const char* jsonKey;
};

const std::vector<SoulField> soulFields = {
    { &SoulUpgradeConfig::soulYexing, "野性之魂", "soul_yexing" },       { &SoulUpgradeConfig::soulJielue, "劫掠之魂", "soul_jielue" },
    { &SoulUpgradeConfig::soulWugu, "巫蛊之魂", "soul_wugu" },           { &SoulUpgradeConfig::soulBaolue, "暴虐之魂", "soul_baolue" },
    { &SoulUpgradeConfig::soulXiongsha, "凶煞之魂", "soul_xiongsha" },   { &SoulUpgradeConfig::soulXiedian, "邪典之魂", "soul_xiedian" },
    { &SoulUpgradeConfig::soulJiaozhi, "狡智之魂", "soul_jiaozhi" },     { &SoulUpgradeConfig::soulManhuang, "蛮荒之魂", "soul_manhuang" },
    { &SoulUpgradeConfig::soulXiongshi, "雄狮之魂", "soul_xiongshi" },   { &SoulUpgradeConfig::soulKuangnu, "狂怒之魂", "soul_kuangnu" },
    { &SoulUpgradeConfig::soulShuangxue, "霜雪之魂", "soul_shuangxue" }, { &SoulUpgradeConfig::soulBingjing, "冰晶之魂", "soul_bingjing" },
    { &SoulUpgradeConfig::soulLinye, "林野之魂", "soul_linye" },         { &SoulUpgradeConfig::soulLingdong, "林动之魂", "soul_lingdong" },
    { &SoulUpgradeConfig::soulJihan, "极寒之魂", "soul_jihan" },         { &SoulUpgradeConfig::soulBaopo, "爆破之魂", "soul_baopo" },
    { &SoulUpgradeConfig::soulChenyuan, "沉渊之魂", "soul_chenyuan" },   { &SoulUpgradeConfig::soulKuangre, "狂热之魂", "soul_kuangre" },
    { &SoulUpgradeConfig::soulTianyun, "天陨之魂", "soul_tianyun" },     { &SoulUpgradeConfig::soulXianzu, "先祖之魂", "soul_xianzu" },
};
