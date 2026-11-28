#pragma once

#include "MaaFramework/MaaAPI.h"
#include "ConfigDef.h"

#include <meojson/json.hpp>

#include <map>
#include <string>
#include <vector>

class Config
{
public:
    static Config& instance();

    // 读取节点 merge 后的 attach（JSON 对象），供自定义动作/识别读配置
    json::value nodeAttach(MaaContext* context, const char* nodeName);

    // 把节点 attach 解析进用户配置结构体
    void parseUserConfig(const json::value& attach);

    const UserBattleConfig& battle() const { return m_battle; }
    const SoulUpgradeConfig& soul() const { return m_soul; }

    const std::map<std::string, std::vector<std::string>>& recipeConfig();

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    double parseDouble(const std::string& s, double def);
    UserBattleConfig m_battle;
    SoulUpgradeConfig m_soul;
};
