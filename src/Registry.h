#pragma once

#include "MaaAgentServer/MaaAgentServerAPI.h"
#include "MaaFramework/MaaAPI.h"

// 原项目通过 MaaResourceRegisterCustomRecognition/Action(res, ...) 在进程内注册。
// cpp-service 作为 MaaAgentServer 子进程运行，这里把同样的注册调用重定向到
// MaaAgentServer。第一个 MaaResource* 参数被忽略，仅保留以最小化对自定义代码的改动。
inline MaaBool registerCustomRecognition(MaaResource* /*res*/, const char* name, MaaCustomRecognitionCallback cb, void* arg)
{
    return MaaAgentServerRegisterCustomRecognition(name, cb, arg);
}

inline MaaBool registerCustomAction(MaaResource* /*res*/, const char* name, MaaCustomActionCallback cb, void* arg)
{
    return MaaAgentServerRegisterCustomAction(name, cb, arg);
}
