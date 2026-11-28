#include <iostream>
#include <string>

#include <windows.h>

#include "MaaAgentServer/MaaAgentServerAPI.h"
#include "MaaFramework/MaaAPI.h"

#include "core/Registrar.h"
#include "LogUtils.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: cpp-service <identifier>" << std::endl;
        return 1;
    }

    LogUtils::init();

    std::string log_dir = "./debug";
    MaaGlobalSetOption(MaaGlobalOption_LogDir, static_cast<void*>(log_dir.data()), log_dir.size());

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
