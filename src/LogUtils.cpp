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

LogLevel colorToLevel(const std::string& color)
{
    if (color == "#ef4444") {
        return LogLevel::Error; // 红
    }
    if (color == "#f59e0b") {
        return LogLevel::Warn; // 黄
    }
    return LogLevel::Info;
}

void write(LogLevel lv, const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // 文件：全部级别，完整时间 + 级别（便于落盘排查）
    if (g_file.is_open()) {
        g_file << levelName(lv) << " " << message << "\n";
        g_file.flush();
    }

    // stdout：Info 及以上，只时间 + 消息（对齐 MXU 界面：无日期、无级别）
    if (lv >= kStdoutLevel) {
        std::cout << message << std::endl;
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

void LogUtils::debug(const std::string& message)
{
    write(LogLevel::Debug, message);
}

void LogUtils::info(const std::string& message)
{
    write(LogLevel::Info, message);
}

void LogUtils::warn(const std::string& message)
{
    write(LogLevel::Warn, message);
}

void LogUtils::error(const std::string& message)
{
    write(LogLevel::Error, message);
}

void LogUtils::log(const std::string& message, const std::string& color)
{
    write(colorToLevel(color), message);
}
