#include "Config.h"
#include "util/MaaBuffer.h"

Config& Config::instance()
{
    static Config config;
    return config;
}

double Config::parseDouble(const std::string& s, double def)
{
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        return pos > 0 ? v : def;
    }
    catch (...) {
        return def;
    }
}

json::value Config::nodeAttach(MaaContext* context, const char* nodeName)
{
    MaaBuffer buf;
    if (!MaaContextGetNodeData(context, nodeName, buf.get())) {
        return json::object {};
    }
    auto node = json::parse(buf.str());
    if (!node) {
        return json::object {};
    }
    const auto& attach = (*node)["attach"];
    return attach.is_object() ? attach : json::value(json::object {});
}

void Config::parseUserConfig(const json::value& attach)
{
    m_battle = UserBattleConfig {};
    m_battle.reserve = attach.get("reserve", json::value(0)).as_integer();
    m_battle.cryInterval = attach.get("cry_interval", json::value(0)).as_integer();
    m_battle.burstSeconds = attach.get("burst_seconds", json::value(0)).as_integer();
    m_battle.firstSkillTime = attach.get("first_skill_time", json::value(0)).as_integer();
    const std::string skill = attach.get("skill", json::value("None")).as_string();
    m_battle.skill = (skill == "Xiaoqing") ? Skill::Xiaoqing : Skill::None;
    const std::string cryColumn = attach.get("cry_column", json::value("0")).as_string();
    m_battle.cryColumn =
        (cryColumn == "Column1") ? 1 : (cryColumn == "Column3") ? 3 : (cryColumn == "Column2") ? 2 : 0;

    m_soul = SoulUpgradeConfig {};
    m_soul.countMode = (attach.get("countMode", json::value("0")).as_string() == "1") ? 1 : 0;
    m_soul.count = attach.get("count", json::value(0)).as_integer();

    for (const auto& soulField : soulFields) {
        m_soul.*soulField.field = attach.get(soulField.jsonKey, json::value(false)).as_boolean();
    }

    m_soul.group1.enabled = attach.get("retention_group1_enabled", json::value(false)).as_boolean();
    m_soul.group2.enabled = attach.get("retention_group2_enabled", json::value(false)).as_boolean();
    m_soul.group3.enabled = attach.get("retention_group3_enabled", json::value(false)).as_boolean();

    m_soul.group1.interval = attach.get("g1_interval", json::value(false)).as_boolean();
    m_soul.group1.damage = attach.get("g1_damage", json::value(false)).as_boolean();
    m_soul.group1.resist = attach.get("g1_resist", json::value(false)).as_boolean();
    m_soul.group1.intervalValue = parseDouble(attach.get("group1_interval", json::value("0")).as_string(), 0.0);
    m_soul.group1.damageValue = parseDouble(attach.get("group1_damage", json::value("0")).as_string(), 0.0);
    m_soul.group1.resistValue = parseDouble(attach.get("group1_resist", json::value("0")).as_string(), 0.0);

    m_soul.group2.interval = attach.get("g2_interval", json::value(false)).as_boolean();
    m_soul.group2.crit = attach.get("g2_crit", json::value(false)).as_boolean();
    m_soul.group2.intervalValue = parseDouble(attach.get("group2_interval", json::value("0")).as_string(), 0.0);
    m_soul.group2.critValue = parseDouble(attach.get("group2_crit", json::value("0")).as_string(), 0.0);

    m_soul.group3.interval = attach.get("g3_interval", json::value(false)).as_boolean();
    m_soul.group3.heal = attach.get("g3_heal", json::value(false)).as_boolean();
    m_soul.group3.intervalValue = parseDouble(attach.get("group3_interval", json::value("0")).as_string(), 0.0);
    m_soul.group3.healValue = parseDouble(attach.get("group3_heal", json::value("0")).as_string(), 0.0);
}

const std::map<std::string, std::vector<std::string>>& Config::recipeConfig()
{
    static const std::map<std::string, std::vector<std::string>> config = {
        { "黏糊糊的食物", { "肉", "水果" } },
        { "面包", { "麦粉", "发酵粉" } },
        { "混合蔬菜浓汤", { "野菜", "干净的水", "盐" } },
        { "果汁", { "水果", "干净的水" } },
        { "腌菜", { "野菜", "盐" } },
        { "炸鱼", { "鱼", "橄榄油" } },
        { "蘑菇浓汤", { "野菜", "干净的水" } },
        { "炖肉", { "肉", "干净的水" } },
        { "虾饼", { "麦粉", "虾" } },
        { "腌制鱼肉香肠", { "鱼", "盐" } },
        { "鱼汤", { "鱼", "干净的水" } },
        { "野菜沙拉", { "野菜", "乳酪" } },
        { "鸟肉串", { "肉", "野菜" } },
        { "肉丸子", { "麦粉", "肉" } },
        { "风干香肠", { "肉", "鱼", "盐" } },
        { "饼干", { "麦粉", "牛奶" } },
        { "蒸虾", { "虾", "盐" } },
        { "肉酱", { "肉", "盐" } },
        { "鱼肉沙拉", { "鱼", "乳酪" } },
        { "水果布丁", { "水果", "干净的水", "牛奶" } },
        { "发酵饮品", { "麦粉", "发酵粉", "干净的水" } },
        { "炒野菜", { "肉", "野菜", "盐" } },
        { "水果酿", { "水果", "发酵粉" } },
        { "荧果肉酱", { "肉", "盐", "荧果" } },
        { "炸鸡", { "肉", "发酵粉", "盐" } },
        { "胡萝卜面包", { "麦粉", "野菜" } },
        { "胡萝卜汁", { "砂糖", "野菜" } },
        { "水果馅饼", { "麦粉", "水果", "盐" } },
        { "鸟肉馅饼", { "麦粉", "肉", "盐" } },
        { "牛奶面包", { "麦粉", "发酵粉", "牛奶" } },
        { "煎肉排", { "肉", "橄榄油" } },
        { "乳酪馅饼", { "麦粉", "乳酪" } },
        { "鲜虾野菜汤", { "野菜", "虾", "干净的水" } },
        { "烤鸟肉沙拉", { "肉", "水果", "乳酪" } },
        { "海鲜浓汤", { "鱼", "干净的水", "盐" } },
        { "鱼肉馅饼", { "麦粉", "鱼", "发酵粉" } },
        { "海陆烤肉拼盘", { "肉", "鱼" } },
        { "乱炖", { "肉", "乳酪" } },
    };
    return config;
}
