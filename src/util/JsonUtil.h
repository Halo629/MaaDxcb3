#pragma once

#include <meojson/json.hpp>

#include <string>
#include <vector>

namespace jsonutil {

// 解析失败返回空 value，替代各处重复的 json::parse(x).value_or(json::value{})
inline json::value parse(const std::string& text)
{
    return json::parse(text).value_or(json::value {});
}

// 读取 int 数组，元素不足时用 def 补齐
inline std::vector<int> readIntArray(const json::value& v, std::vector<int> def)
{
    if (!v.is_array()) {
        return def;
    }
    const auto& arr = v.as_array();
    for (size_t i = 0; i < arr.size() && i < def.size(); ++i) {
        def[i] = arr[i].as_integer();
    }
    return def;
}

} // namespace jsonutil
