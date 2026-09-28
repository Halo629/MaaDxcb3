#pragma once

#include <sstream>
#include <string>
#include <vector>

// 纯 C++ 字符串工具，用于替代原项目中的 Qt QString / QStringList 用法。

inline std::string toStr(const std::string& s)
{
    return s;
}

inline std::string toStr(const char* s)
{
    return s ? std::string(s) : std::string();
}

inline std::string toStr(char c)
{
    return std::string(1, c);
}

template <typename T>
inline std::string toStr(const T& v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

inline void replaceN(std::string& s, int n, const std::string& repl)
{
    std::string marker = "%" + std::to_string(n);
    size_t pos = 0;
    while ((pos = s.find(marker, pos)) != std::string::npos) {
        s.replace(pos, marker.size(), repl);
        pos += repl.size();
    }
}

// 依次替换 %1 %2 %3 ...，等价于 Qt 的 .arg(a).arg(b).arg(c) 链式调用。
template <typename... Args>
std::string fmt(const std::string& f, Args&&... args)
{
    std::string result = f;
    int i = 1;
    (replaceN(result, i++, toStr(std::forward<Args>(args))), ...);
    return result;
}

// 等价于 QString::split(delim, Qt::SkipEmptyParts)
inline std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            if (!cur.empty()) {
                out.push_back(cur);
            }
            cur.clear();
        }
        else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

// 等价于 QStringList::join(sep)
inline std::string join(const std::vector<std::string>& v, const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i != 0) {
            out += sep;
        }
        out += v[i];
    }
    return out;
}

// 等价于 QString::trimmed()
inline std::string trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
        ++b;
    }
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
        --e;
    }
    return s.substr(b, e - b);
}
