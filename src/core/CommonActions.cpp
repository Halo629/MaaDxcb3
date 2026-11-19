#include "CommonActions.h"

#include "LogUtils.h"
#include "Registry.h"
#include "config/Config.h"
#include "map/MapNavigator.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

namespace {

bool isNodeEnabled(MaaContext* context, const char* nodeName)
{
    MaaBuffer buf;
    if (!MaaContextGetNodeData(context, nodeName, buf.get())) {
        return true;
    }
    auto node = json::parse(buf.str());
    if (!node) {
        return true;
    }
    auto enabled = (*node)["enabled"];
    return enabled.is_boolean() ? enabled.as_boolean() : true;
}

std::string getNodeFocusDesc(MaaContext* context, const char* nodeName)
{
    MaaBuffer buf;
    if (!MaaContextGetNodeData(context, nodeName, buf.get())) {
        return {};
    }
    auto node = json::parse(buf.str());
    if (!node) {
        return {};
    }
    auto focus = (*node)["focus"];
    if (!focus.is_object()) {
        return {};
    }
    for (auto& [event, msg] : focus.as_object()) {
        std::string text = msg.as_string();
        const std::string prefix = "开始任务:";
        if (text.rfind(prefix, 0) == 0) {
            text = text.substr(prefix.size());
        }
        return text;
    }
    return {};
}

} // namespace

MaaBool CommonActions::clearHitCount(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customActionName, const char* customActionParam,
                                     MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    auto param = jsonutil::parse(customActionParam ? customActionParam : "");
    auto nodes = param.get("nodes", json::value {});
    if (!nodes.is_array() || nodes.as_array().empty()) {
        LogUtils::error("清除节点命中计数错误:未指定节点");
        return false;
    }

    bool strict = param.get("strict", json::value(false)).as_boolean();
    bool hasFailure = false;
    for (const auto& item : nodes.as_array()) {
        std::string name = item.as_string();
        if (name.empty()) {
            hasFailure = true;
            continue;
        }
        if (!MaaContextClearHitCount(context, name.c_str())) {
            LogUtils::error(fmt("清除节点命中计数错误: 清除 %1 命中计数失败", name));
            hasFailure = true;
        }
    }
    return !(hasFailure && strict);
}

MaaBool CommonActions::failTask(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                const char* customActionName, const char* customActionParam,
                                MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return false;
}

MaaBool CommonActions::saveConfig(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customActionName, const char* customActionParam,
                                  MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    Config::parseUserConfig(Config::nodeAttach(context, nodeName));
    return true;
}

MaaBool CommonActions::subTask(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                               const char* customActionName, const char* customActionParam,
                               MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    auto parsed = json::parse(customActionParam ? customActionParam : "{}");
    if (!parsed || !parsed->contains("sub")) {
        LogUtils::error("SubTask: custom_action_param.sub 缺失");
        return false;
    }
    auto sub = (*parsed)["sub"].as_array();
    if (sub.empty()) {
        LogUtils::error("SubTask: custom_action_param.sub 为空");
        return false;
    }

    bool continueOnFailure = (*parsed).get("continue", json::value(false)).as_boolean();
    bool strict = (*parsed).get("strict", json::value(true)).as_boolean();

    auto* tasker = MaaContextGetTasker(context);
    bool hasFailure = false;
    for (auto& item : sub) {
        std::string name = item.as_string();
        if (name.empty()) {
            hasFailure = true;
            if (!continueOnFailure) {
                break;
            }
            continue;
        }
        if (!isNodeEnabled(context, name.c_str())) {
            continue;
        }

        MaaTaskId subId = MaaContextRunTask(context, name.c_str(), "{}");
        if (subId == MaaInvalidId) {
            std::string desc = getNodeFocusDesc(context, name.c_str());
            LogUtils::warn(fmt("任务失败:%1", desc.empty() ? name : desc), false);
            hasFailure = true;
            if (!continueOnFailure) {
                break;
            }
            continue;
        }

        MaaStatus status = MaaStatus_Invalid;
        MaaSize nodeIdListSize = 0;
        MaaTaskerGetTaskDetail(tasker, subId, nullptr, nullptr, &nodeIdListSize, &status);
        if (status != MaaStatus_Succeeded) {
            std::string desc = getNodeFocusDesc(context, name.c_str());
            LogUtils::warn(fmt("任务失败:%1", desc.empty() ? name : desc), false);
            hasFailure = true;
            if (!continueOnFailure) {
                break;
            }
        }
    }
    return !(hasFailure && strict);
}

MaaBool CommonActions::adjustPosition(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    if (!box) {
        return false;
    }
    auto param = jsonutil::parse(customActionParam ? customActionParam : "{}");
    auto offset = jsonutil::readIntArray(param.get("target_offset", json::value()), { 0, 0, 0, 0 });
    screen::adjustRectToClickable(context, nodeName ? nodeName : "", offset, *box);
    return true;
}

MaaBool CommonActions::adjustPositionAndClick(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                              const char* customActionName, const char* customActionParam,
                                              MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    if (!box) {
        return false;
    }
    auto param = jsonutil::parse(customActionParam ? customActionParam : "{}");
    auto offset = jsonutil::readIntArray(param.get("target_offset", json::value()), { 0, 0, 0, 0 });
    MaaRect target = screen::adjustRectToClickable(context, nodeName ? nodeName : "", offset, *box);
    screen::clickPoint(context, target.x + target.width / 2, target.y + target.height / 2);
    return true;
}

MaaBool CommonActions::checkRoleInTargetMap(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                            const char* customRecognitionName, const char* customRecognitionParam,
                                            const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                            MaaRect* outBox, MaaStringBuffer* outDetail)
{
    std::string targetMap;
    bool resetFlag = true;
    auto param = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    targetMap = param.get("map_name", json::value("")).as_string();

    if (targetMap.empty()) {
        auto attach = Config::nodeAttach(context, nodeName);
        resetFlag = attach.get("reset_flag", json::value(true)).as_boolean();
        targetMap = attach.get("map_name", json::value("")).as_string();
    }
    if (targetMap.empty()) {
        LogUtils::error("重置地图: 未指定地图名称");
        return false;
    }

    MaaRect roleBox { 0, 0, 0, 0 };
    std::string mapOfRole = MapNavigator::mapOfRole(context, &roleBox);
    if (mapOfRole.empty()) {
        return false;
    }

    bool matched = false;
    for (auto& alt : split(targetMap, '|')) {
        if (mapOfRole.find(alt) != std::string::npos) {
            matched = true;
            break;
        }
    }
    if (!matched) {
        return false;
    }
    if (!resetFlag) {
        return true;
    }

    char detailJson[256];
    int boxY = roleBox.y;

    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemOcr":{"roi":[125,225,80,%d]}})", boxY - 280);
    if (recog::run(context, image, "MapListItemOcr", detailJson, outBox, nullptr)) {
        return true;
    }
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemTemplate":{"roi":[125,225,80,%d]}})", boxY - 280);
    if (recog::run(context, image, "MapListItemTemplate", detailJson, outBox, nullptr)) {
        return true;
    }
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemOcr":{"roi":[125,%d,80,%d]}})", boxY + 100, 880 - boxY);
    if (recog::run(context, image, "MapListItemOcr", detailJson, outBox, nullptr)) {
        return true;
    }
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemTemplate":{"roi":[125,%d,80,%d]}})", boxY + 100, 880 - boxY);
    if (recog::run(context, image, "MapListItemTemplate", detailJson, outBox, nullptr)) {
        return true;
    }
    return false;
}

MaaBool CommonActions::findFirstCanClickOption(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                               const char* customRecognitionName, const char* customRecognitionParam,
                                               const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                               MaaRect* outBox, MaaStringBuffer* outDetail)
{
    MaaBuffer detailBuf;
    MaaRect dummyBox;
    if (!recog::run(context, image, "DialogOption", "{}", &dummyBox, detailBuf.get())) {
        return false;
    }
    auto j = jsonutil::parse(detailBuf.str());
    if (!j.is_object()) {
        return false;
    }
    json::value arr = j["filtered"];
    if (!arr.is_array()) {
        arr = j["all"];
    }
    if (!arr.is_array()) {
        return false;
    }

    std::vector<MaaRect> boxes;
    for (auto& item : arr.as_array()) {
        auto& b = item["box"];
        if (b.is_array() && b.as_array().size() >= 4) {
            boxes.push_back({ b[0].as_integer(), b[1].as_integer(), b[2].as_integer(), b[3].as_integer() });
        }
    }

    auto* tasker = MaaContextGetTasker(context);
    for (auto& b : boxes) {
        std::string param =
            fmt(R"({"method":40,"lower":[0,245,240],"upper":[5,255,255],"roi":[%1,%2,%3,%4],"count":20})",
                b.x, b.y, b.width, b.height);
        auto id = MaaContextRunRecognitionDirect(context, "ColorMatch", param.c_str(), image);
        if (id == MaaInvalidId) {
            continue;
        }
        MaaBool hit = false;
        MaaRect cb;
        MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &cb, nullptr, nullptr, nullptr);
        if (!hit) {
            *outBox = b;
            return true;
        }
    }
    LogUtils::warn("未找到可点击选项");
    return false;
}

namespace {

MaaBool clearHitCountThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                           const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CommonActions::clearHitCount(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool failTaskThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                      const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CommonActions::failTask(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool saveConfigThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CommonActions::saveConfig(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool subTaskThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                     const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CommonActions::subTask(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool adjustPositionThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                            const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CommonActions::adjustPosition(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool adjustPositionAndClickThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                    const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CommonActions::adjustPositionAndClick(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool checkRoleInTargetMapThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                  void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CommonActions::checkRoleInTargetMap(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool findFirstCanClickOptionThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                     const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                     void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CommonActions::findFirstCanClickOption(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void CommonActions::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "ClearHitCount", clearHitCountThunk, userData);
    registerCustomAction(res, "FailTask", failTaskThunk, userData);
    registerCustomAction(res, "SaveConfig", saveConfigThunk, userData);
    registerCustomAction(res, "SubTask", subTaskThunk, userData);
    registerCustomAction(res, "AdjustPosition", adjustPositionThunk, userData);
    registerCustomAction(res, "AdjustPositionAndClick", adjustPositionAndClickThunk, userData);
    registerCustomRecognition(res, "CheckRoleInTargetMap", checkRoleInTargetMapThunk, userData);
    registerCustomRecognition(res, "FindFirstCanClickOption", findFirstCanClickOptionThunk, userData);
}
