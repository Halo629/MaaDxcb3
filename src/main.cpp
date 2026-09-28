#include <iostream>
#include <string>

#include <windows.h>

#include "MaaAgentServer/MaaAgentServerAPI.h"
#include "MaaFramework/MaaAPI.h"

#include "Reco.h"
#include "LogUtils.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: cpp-service <identifier>" << std::endl;
        return 1;
    }

    // 自己的日志：双写（stdout Info+ / 文件全量）
    LogUtils::init();

    // MaaFramework 日志目录
    std::string log_dir = "./debug";
    MaaGlobalSetOption(MaaGlobalOption_LogDir, static_cast<void*>(log_dir.data()), log_dir.size());

    // 注册所有自定义识别 / 动作（内部通过 MaaAgentServer 注册）。
    registerAllCustomRecognition(nullptr, nullptr);

    const char* identifier = argv[1];
    if (!MaaAgentServerStartUp(identifier)) {
        std::cerr << "MaaAgentServerStartUp failed" << std::endl;
        return 1;
    }

    MaaAgentServerJoin();

    MaaAgentServerShutDown();
    return 0;
}
