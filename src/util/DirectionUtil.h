#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// 罗盘 12 方位（每格 30°，0°=正北，顺时针）
enum class CompassDir {
    North = 0,
    NorthByEast = 1,
    EastByNorth = 2,
    East = 3,
    EastBySouth = 4,
    SouthByEast = 5,
    South = 6,
    SouthByWest = 7,
    WestBySouth = 8,
    West = 9,
    WestByNorth = 10,
    NorthByWest = 11,
    Unknown = 12,
};

namespace direction {

// 罗盘方位文本 → 12 方位（支持"北偏东""正北""北"等）
inline CompassDir parseDirection(const std::string& text)
{
    static const std::vector<std::pair<const char*, CompassDir>> g_dirTable = {
        { "北偏东", CompassDir::NorthByEast }, { "北偏西", CompassDir::NorthByWest },
        { "东偏北", CompassDir::EastByNorth }, { "东偏南", CompassDir::EastBySouth },
        { "南偏东", CompassDir::SouthByEast }, { "南偏西", CompassDir::SouthByWest },
        { "西偏北", CompassDir::WestByNorth }, { "西偏南", CompassDir::WestBySouth },
        { "正北", CompassDir::North },         { "正东", CompassDir::East },
        { "正南", CompassDir::South },         { "正西", CompassDir::West },
        { "西", CompassDir::West },            { "北", CompassDir::North },
        { "东", CompassDir::East },            { "南", CompassDir::South },
    };
    for (const auto& [kw, dir] : g_dirTable) {
        if (text.find(kw) != std::string::npos) {
            return dir;
        }
    }
    return CompassDir::Unknown;
}

// 世界坐标向量 → 12 方位（+x=东，+y=北，0°=北顺时针）
inline CompassDir vectorToDirection(int dx, int dy)
{
    if (dx == 0 && dy == 0) {
        return CompassDir::Unknown;
    }
    double angle = std::atan2(static_cast<double>(dx), static_cast<double>(dy)) * 180.0 / 3.14159265358979323846;
    if (angle < 0) {
        angle += 360.0;
    }
    int idx = static_cast<int>(std::lround(angle / 30.0)) % 12;
    return static_cast<CompassDir>(idx);
}

// 解析 "(19,73)" 或 "(19, 73)" 形式的坐标
inline bool parseCoord(const std::string& text, int& x, int& y)
{
    std::vector<int> nums;
    std::string digits;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
        else if (!digits.empty()) {
            nums.push_back(std::atoi(digits.c_str()));
            digits.clear();
        }
    }
    if (!digits.empty()) {
        nums.push_back(std::atoi(digits.c_str()));
    }
    if (nums.size() >= 2) {
        x = nums[0];
        y = nums[1];
        return true;
    }
    return false;
}

} // namespace direction
