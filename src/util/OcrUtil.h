#pragma once

#include "MaaFramework/MaaAPI.h"

#include <meojson/json.hpp>

#include <string>
#include <vector>

namespace ocr {

struct Text {
    std::string text;
    MaaRect box = { 0, 0, 0, 0 };
};

// 从识别 detail 递归收集文本与框，兼容 And 节点的子结果数组。
// 优先级：all/filtered（含框）> best（可能无框）。
std::vector<Text> collectTexts(const json::value& detail);

// 从文本列表取第一个数字，失败返回 false。
bool extractNumber(const std::vector<Text>& texts, int& outValue);

// 解析 OCR 文本中的数值，支持 万/亿/K/M/B 后缀，保留小数。
bool parseNumeric(const std::string& text, double& outValue);

} // namespace ocr
