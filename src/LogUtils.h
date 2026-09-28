#pragma once

#include <string>

enum class LogLevel
{
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

// 纯 C++ 日志，双写：
//   - stdout（Info 及以上）：`HH:mm:ss 消息`（对齐 MXU 界面，无日期、无级别）
//   - 文件（全部级别）：`YYYY-MM-DD HH:mm:ss LEVEL 消息`
// 参照 MaaEnd go-service 的做法（zerolog MultiLevelWriter 同时写 stdout 与文件）。
class LogUtils
{
public:
    // 初始化：创建日志目录并打开 debug/cpp-service.log。main 里调用一次。
    static void init(const std::string& logDir = "./debug");

    // 带级别日志（新代码建议用这些）
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);

    // 兼容旧调用：按颜色映射级别（红=error、黄=warn、其它=info）
    static void log(const std::string& message, const std::string& color = "#ffffff");
};
