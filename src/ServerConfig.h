#ifndef TASKDATADEF_H
#define TASKDATADEF_H

#include <set>
#include <string>
#include <vector>

#define SoulTomb_Level_None -1
#define SoulTomb_Level_Max 9999

#define Skill_None 0
#define Skill_Xiaoqing 1

#define Skill_Xiaoqing_Interval 60

// 时间线战吼触发点
struct CryTrigger
{
    int elapsedSeconds = 0; // 战斗开始后第几秒
    int cryCount = 1;       // 吼几次
};

// ===== 战吼滑动坐标（3 列） =====
struct SwipeCol
{
    int beginX = 0, beginY = 0;
    int endX = 0, endY = 0;
    int duration = 100;
};

// 战斗配置通用参数
struct BattleConfigParam
{
    int beanColorLower[3] = { 10, 80, 80 };   // ColorMatch 下界 (HSV: H,S,V, 豆子H≈14-30)
    int beanColorUpper[3] = { 35, 255, 255 }; // ColorMatch 上界 (HSV: H,S,V)
    int beanColorCount = 250;                 // ColorMatch 连通像素阈值（满豆~308-472px，恢复中~20-152px）

    SwipeCol cryCols[3] = { { 150, 1050, 150, 750 }, { 350, 1050, 350, 750 }, { 550, 1050, 550, 750 } };
    // ===== 技能参数 =====
    int skillRoi[4] = { 5, 1170, 260, 110 }; // 技能图标验证区域
};

// 战斗配置（界面参数，默认全 0 表示未配置）
struct UserBattleConfig
{
    BattleConfigParam configParam;
    int cryAnywayThreshold = 6;
    int reserve = 0;        // 保留豆子数
    int cryInterval = 0;    // 战吼间隔（秒）
    int burstSeconds = 0;   // 爆发时间（秒）
    int firstSkillTime = 0; // 首次技能时机（秒）
    int skill = 0;          // 技能（0=未配置，1=小青）
    int cryColumn = 0;      // 战吼列（0=未配置，1/2/3=第几列）
};

// 战吼决策
enum BattleCryDecision
{
    BATTLE_CRY_NONE = 0,
    BATTLE_CRY_NORMAL,
    BATTLE_CRY_BURST
};

// 魔魂升级配置（界面参数，默认全 0/false 表示未配置）
struct SoulUpgradeConfig
{
    int countMode = -1; // 0=最大次数(MaxCount)，1=指定次数(SpecifiedCount)
    int count = 0;      // 指定次数值

    bool soul_yexing = false, soul_jielue = false, soul_wugu = false, soul_baolue = false;
    bool soul_xiongsha = false, soul_xiedian = false, soul_jiaozhi = false, soul_manhuang = false;
    bool soul_xiongshi = false, soul_kuangnu = false, soul_shuangxue = false, soul_bingjing = false;
    bool soul_linye = false, soul_lingdong = false, soul_jihan = false, soul_baopo = false;
    bool soul_chenyuan = false, soul_kuangre = false, soul_tianyun = false, soul_xianzu = false;

    bool retention_group1_enabled = false, retention_group2_enabled = false, retention_group3_enabled = false;

    bool g1_interval = false, g1_damage = false, g1_resist = false;
    double group1_interval = 0.0, group1_damage = 0.0, group1_resist = 0.0;

    bool g2_interval = false, g2_crit = false;
    double group2_interval = 0.0, group2_crit = 0.0;

    bool g3_interval = false, g3_heal = false;
    double group3_interval = 0.0, group3_heal = 0.0;
};

// 全魔魂字典
const std::set<std::string> allSoulNames = { "巫蛊之魂", "暴虐之魂", "野性之魂", "凶煞之魂", "邪典之魂", "劫掠之魂", "狡智之魂",
                                             "蛮荒之魂", "雄狮之魂", "狂怒之魂", "霜雪之魂", "冰晶之魂", "林野之魂", "林动之魂",
                                             "极寒之魂", "爆破之魂", "沉渊之魂", "狂热之魂", "天陨之魂", "先祖之魂" };

// 魔魂选择：结构体字段 -> 中文名
const std::vector<std::pair<bool SoulUpgradeConfig::*, const char*>> soulFlags = {
    { &SoulUpgradeConfig::soul_yexing, "野性之魂" },    { &SoulUpgradeConfig::soul_jielue, "劫掠之魂" },
    { &SoulUpgradeConfig::soul_wugu, "巫蛊之魂" },      { &SoulUpgradeConfig::soul_baolue, "暴虐之魂" },
    { &SoulUpgradeConfig::soul_xiongsha, "凶煞之魂" },  { &SoulUpgradeConfig::soul_xiedian, "邪典之魂" },
    { &SoulUpgradeConfig::soul_jiaozhi, "狡智之魂" },   { &SoulUpgradeConfig::soul_manhuang, "蛮荒之魂" },
    { &SoulUpgradeConfig::soul_xiongshi, "雄狮之魂" },  { &SoulUpgradeConfig::soul_kuangnu, "狂怒之魂" },
    { &SoulUpgradeConfig::soul_shuangxue, "霜雪之魂" }, { &SoulUpgradeConfig::soul_bingjing, "冰晶之魂" },
    { &SoulUpgradeConfig::soul_linye, "林野之魂" },     { &SoulUpgradeConfig::soul_lingdong, "林动之魂" },
    { &SoulUpgradeConfig::soul_jihan, "极寒之魂" },     { &SoulUpgradeConfig::soul_baopo, "爆破之魂" },
    { &SoulUpgradeConfig::soul_chenyuan, "沉渊之魂" },  { &SoulUpgradeConfig::soul_kuangre, "狂热之魂" },
    { &SoulUpgradeConfig::soul_tianyun, "天陨之魂" },   { &SoulUpgradeConfig::soul_xianzu, "先祖之魂" },
};

#endif
