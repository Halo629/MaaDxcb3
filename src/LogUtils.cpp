#include "LogUtils.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

namespace
{
std::ofstream g_file;
std::mutex g_mutex;
constexpr LogLevel kStdoutLevel = LogLevel::Info;

const char* levelName(LogLevel lv)
{
    switch (lv) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

std::string timestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm t { };
    localtime_s(&t, &tt);
    char buf[32];
    std::snprintf(
        buf,
        sizeof(buf),
        "%04d-%02d-%02d %02d:%02d:%02d",
        t.tm_year + 1900,
        t.tm_mon + 1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec);
    return buf;
}

// 判断颜色是否为"无色"：默认白 / 空串直接按纯文本输出，避免把所有日志都套上 span
static bool isPlainColor(const std::string& color)
{
    return color.empty() || color == "#ffffff";
}

// 转义 HTML 特殊字符，防止消息里的 < > & 破坏内联 HTML 结构
static std::string escapeHtml(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

void write(LogLevel lv, const std::string& message, const std::string& color = "", bool show = true)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // 文件：全部级别，LEVEL + 纯文本消息（便于落盘排查，不带上色标记）
    if (g_file.is_open()) {
        g_file << levelName(lv) << " " << message << "\n";
        g_file.flush();
    }

    // 只写文件，不展示到界面
    if (!show) {
        return;
    }

    // stdout：Info 及以上
    if (lv < kStdoutLevel) {
        return;
    }

    if (isPlainColor(color)) {
        std::cout << message << std::endl;
    }
    else {
        // MXU 日志面板把 agent 的 stdout 统一按灰色(type=agent)渲染，不识别 ANSI 或裸色值；
        // 唯一能上色的途径是内联 HTML 的 style 属性（MXU 的 DOMPurify 白名单放行了 style）。
        std::cout << "<span style=\"color:" << color << "\">" << escapeHtml(message)
                  << "</span>" << std::endl;
    }
}
}

void LogUtils::init(const std::string& logDir)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    g_file.open(logDir + "/cpp-service.log", std::ios::out | std::ios::app);
}

void LogUtils::debug(const std::string& message, bool show)
{
    write(LogLevel::Debug, message, "", show);
}

void LogUtils::info(const std::string& message, bool show)
{
    write(LogLevel::Info, message, "", show);
}

void LogUtils::warn(const std::string& message, bool show)
{
    write(LogLevel::Warn, message, "#f59e0b", show);
}

void LogUtils::error(const std::string& message, bool show)
{
    write(LogLevel::Error, message, "#ef4444", show);
}
