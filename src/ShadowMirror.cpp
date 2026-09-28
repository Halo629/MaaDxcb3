#include "Comm.h"

namespace
{

// ──── 常量 ────
static constexpr int kMaxShardsPerDay = 10;

// 跨自定义动作共享：本轮还需获取的镜片数
static int g_remainingShards = 0;

// 通过节点名直接取该节点识别结果中的 "x/10" 文本（ShadowMirrorAvailableCount 是 And 节点）
static std::string getCountTextByNodeName(MaaContext* context, const char* nodeName)
{
    auto* tasker = MaaContextGetTasker(context);

    MaaNodeId nodeId = 0;
    if (!MaaTaskerGetLatestNode(tasker, nodeName, &nodeId)) {
        return { };
    }
    MaaRecoId recoId = 0;
    if (!MaaTaskerGetNodeDetail(tasker, nodeId, nullptr, &recoId, nullptr, nullptr)) {
        return { };
    }
    auto buf = MaaStringBufferCreate();
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, recoId, nullptr, nullptr, &hit, nullptr, buf, nullptr, nullptr);
    std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    // And 节点的 detail 是子结果数组，逐个找 OCR 的 "x/10" 文本
    auto j = json::parse(detail).value_or(json::value { });
    if (j.is_array()) {
        for (auto& item : j.as_array()) {
            auto& all = item["detail"]["all"];
            if (all.is_array() && !all.empty() && all[0]["text"].is_string()) {
                std::string text = all[0]["text"].as_string();
                if (text.find('/') != std::string::npos) {
                    return text;
                }
            }
        }
    }
    return { };
}

} // namespace

// ──── ShadowMirrorRecord: 记录掠影之境相关信息 ────
// custom_action_param 通过不同 key 控制记录不同信息：
//   AvailableCountRecord: 记录页面当前可获得的镜片数量（从 ShadowMirrorAvailableCount 识别结果取 "x/10"）
//   LootRecord:          记录一次战利品中获得镜片（剩余数量 -1）
MaaBool shadowMirrorRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto param = json::parse(customActionParam ? customActionParam : "{}").value_or(json::value { });

    if (param.contains("AvailableCountRecord")) {
        std::string countText = getCountTextByNodeName(context, "ShadowMirrorAvailableCount");
        if (!countText.empty()) {
            auto slash = countText.find('/');
            int collected = (slash != std::string::npos) ? std::atoi(countText.substr(0, slash).c_str()) : 0;
            g_remainingShards = kMaxShardsPerDay - collected;
            LogUtils::log(fmt("掠影之境: 今日已获取%1，剩余可获取%2个镜片", collected, g_remainingShards), "#3b82f6");
        }
        else {
            LogUtils::log("掠影之境: 读取可获得的镜片数量失败", "#f59e0b");
        }
    }

    if (param.contains("LootRecord")) {
        if (g_remainingShards > 0) {
            --g_remainingShards;
        }
        LogUtils::log(fmt("掠影之境: 获得镜片，还需获取%1个", g_remainingShards), "#22c55e");
    }

    return true;
}

// ──── ShadowMirrorCountCheck: 判断当前获取的镜片数量是否已达上限 ────
MaaBool shadowMirrorCountCheck(
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
    return g_remainingShards <= 0;
}

void registerCustomShadowMirror(MaaResource* resource, void* userData)
{
    registerCustomAction(resource, "ShadowMirrorRecord", shadowMirrorRecord, userData);
    registerCustomRecognition(resource, "ShadowMirrorCountCheck", shadowMirrorCountCheck, userData);
}
