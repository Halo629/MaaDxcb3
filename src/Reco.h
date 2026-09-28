#pragma once

#include "MaaFramework/MaaDef.h"

// 注册所有自定义识别到 resource（在 init 中调用）
void registerAllCustomRecognition(MaaResource* res, void* user_data = nullptr);
