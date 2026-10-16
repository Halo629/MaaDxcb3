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
//   - stdout（Info 及以上）：无色消息直接输出；带色消息用 <span style="color:..."> 包裹，
//     MXU 日志面板通过富文本渲染上色（agent 的 stdout 本身固定渲染为灰色，不识别 ANSI/裸色值）。
//   - 文件（全部级别）：`LEVEL 消息`（纯文本，便于落盘排查，不带上色标记）。
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
