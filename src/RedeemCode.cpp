#include "Comm.h"

namespace
{
// 兑换码列表（InputRedeemCodes 解析后保存），g_redeemIndex 指向下一个未兑换的码。
std::vector<std::string> g_redeemCodes;
size_t g_redeemIndex = 0;
}

// 自定义动作：解析用户输入的兑换码（用 / 分割），保存到全局。
MaaBool inputRedeemCodes(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = getNodeAttach(context, nodeName);
    std::string code = attach.get("code", json::value("")).as_string();

    g_redeemCodes.clear();
    g_redeemIndex = 0;

    size_t start = 0;
    while (start <= code.size()) {
        size_t pos = code.find('/', start);
        std::string token = (pos == std::string::npos) ? code.substr(start) : code.substr(start, pos - start);
        if (!token.empty()) {
            g_redeemCodes.push_back(token);
        }
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }

    LogUtils::debug(fmt("兑换码解析: 共 %1 个", g_redeemCodes.size()));
    return true;
}

// 自定义识别：是否兑换完所有兑换码。
MaaBool redeemComplete(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    return g_redeemIndex >= g_redeemCodes.size();
}

// 自定义动作：粘贴下一个未兑换的兑换码。
MaaBool inputSingleRedeemCode(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    if (g_redeemIndex >= g_redeemCodes.size()) {
        return false;
    }

    std::string code = g_redeemCodes[g_redeemIndex];
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostInputText(ctrl, code.c_str());
    MaaControllerWait(ctrl, id);

    g_redeemIndex++;
    LogUtils::debug(fmt("粘贴兑换码: %1（剩余 %2 个）", code, g_redeemCodes.size() - g_redeemIndex));
    return true;
}

void registerCustomRedeemCode(MaaResource* res, void* userData)
{
    registerCustomAction(res, "InputRedeemCodes", inputRedeemCodes, userData);
    registerCustomRecognition(res, "RedeemComplete", redeemComplete, userData);
    registerCustomAction(res, "InputSingleRedeemCode", inputSingleRedeemCode, userData);
}
