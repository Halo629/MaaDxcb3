#include "ShadowMirrorModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/StringUtil.h"

#include <cstdlib>

int ShadowMirrorModule::remainingShards_ = 0;

namespace
{

// ──── 常量 ────
constexpr int kMaxShardsPerDay = 10;

// 通过节点名直接取该节点识别结果中的 "x/10" 文本（ShadowMirrorAvailableCount 是 And 节点）
std::string getCountTextByNodeName(MaaContext* context, const char* nodeName)
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
    MaaBuffer buf;
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, recoId, nullptr, nullptr, &hit, nullptr, buf.get(), nullptr, nullptr);
    std::string detail = buf.str();

    // And 节点的 detail 是子结果数组，逐个找 OCR 的 "x/10" 文本
    auto j = jsonutil::parse(detail);
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
MaaBool ShadowMirrorModule::record(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto param = jsonutil::parse(customActionParam ? customActionParam : "{}");

    if (param.contains("AvailableCountRecord")) {
        std::string countText = getCountTextByNodeName(context, "ShadowMirrorAvailableCount");
        if (!countText.empty()) {
            auto slash = countText.find('/');
            int collected = (slash != std::string::npos) ? std::atoi(countText.substr(0, slash).c_str()) : 0;
            remainingShards_ = kMaxShardsPerDay - collected;
            LogUtils::info(fmt("掠影之境: 今日已获取%1，剩余可获取%2个镜片", collected, remainingShards_));
        }
        else {
            LogUtils::warn("掠影之境: 读取可获得的镜片数量失败");
        }
    }

    if (param.contains("LootRecord")) {
        if (remainingShards_ > 0) {
            --remainingShards_;
        }
        LogUtils::info(fmt("掠影之境: 获得镜片，还需获取%1个", remainingShards_));
    }

    return true;
}

// ──── ShadowMirrorCountCheck: 判断当前获取的镜片数量是否已达上限 ────
MaaBool ShadowMirrorModule::countCheck(
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
    return remainingShards_ <= 0;
}

namespace
{

MaaBool recordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                    const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return ShadowMirrorModule::record(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool countCheckThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                        void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return ShadowMirrorModule::countCheck(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void ShadowMirrorModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "ShadowMirrorRecord", recordThunk, userData);
    registerCustomRecognition(res, "ShadowMirrorCountCheck", countCheckThunk, userData);
}
