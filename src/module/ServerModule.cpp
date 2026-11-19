#include "ServerModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "config/Config.h"
#include "map/MapNavigator.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"
#include "util/StringUtil.h"

#include <windows.h>

std::vector<std::string> ServerModule::server_;
std::unordered_set<std::string> ServerModule::serverSeen_;

// 在服务器列表中查找目标服务器
MaaBool ServerModule::findTargetServer(
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
    auto attach = Config::nodeAttach(context, nodeName);
    std::string name = MapNavigator::normalizeServerName(attach.get("target_server_name", json::value("")).as_string());
    std::string line = MapNavigator::normalizeServerName(attach.get("target_server_line", json::value("")).as_string());
    if (name.empty()) {
        LogUtils::error("切换服务器失败: 未指定目标服务器名称");
        return false;
    }
    std::string target = name + line;

    auto sub = recog::getAndSubDetail(context, image, "CustomServerList", "ServerName");
    if (!sub.is_object()) {
        LogUtils::error("读取服务器列表失败");
        return false;
    }
    auto& all = sub["all"];
    if (!all.is_array()) {
        LogUtils::error("读取服务器列表失败");
        return false;
    }

    // 归一化后做 contains 匹配（去掉冒号空格，避免符号差异）
    for (auto& item : all.as_array()) {
        std::string text = MapNavigator::normalizeServerName(item["text"].as_string());
        if (text.empty()) {
            continue;
        }
        if (serverSeen_.insert(text).second) {
            server_.push_back(text); // 记录识别到的服务器（按识别顺序去重）
        }
        if (text.find(target) == std::string::npos) {
            continue;
        }

        LogUtils::info(fmt("找到服务器:[%1],切换中...", text));

        outBox->x = item["box"][0].as_integer();
        outBox->y = item["box"][1].as_integer();
        outBox->width = item["box"][2].as_integer();
        outBox->height = item["box"][3].as_integer();

        return true;
    }
    return false;
}

// 滚动扫描服务器列表：OCR → 上滑 → OCR → 直到没有新服务器
MaaBool ServerModule::GetServerListAct(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    std::vector<std::string> ordered; // 保持插入顺序
    std::unordered_set<std::string> seen;

    auto doOCR = [&]() -> std::vector<std::string> {
        ScreenCap cap(context);
        MaaBuffer detailBuf;
        MaaRect outBox;
        if (!recog::run(
                context,
                cap.img,
                "scan_list",
                R"({"scan_list":{"recognition":"OCR","roi":[104,300,350,663],"order":"Vertical"}})",
                &outBox,
                detailBuf.get())) {
            return { };
        }
        std::string detailStr = detailBuf.str();

        auto j = jsonutil::parse(detailStr);
        if (!j.contains("all")) {
            return { };
        }

        std::vector<std::string> result;
        for (auto& item : j["all"].as_array()) {
            std::string text = MapNavigator::normalizeServerName(item["text"].as_string());
            if (text.empty()) {
                continue;
            }
            // 排除非服务器条目（灵魂链接等级等 UI 文本）
            if (text.find("灵魂链接") != std::string::npos) {
                continue;
            }
            if (text.find("等级") != std::string::npos) {
                continue;
            }
            result.push_back(text);
        }
        return result;
    };

    for (int round = 0; round < 20; ++round) {
        auto servers = doOCR();
        int before = (int)seen.size();
        for (auto& s : servers) {
            if (seen.insert(s).second) {
                ordered.push_back(s); // 新条目按发现顺序追加
            }
        }
        int added = (int)seen.size() - before;

        LogUtils::info(fmt("扫描第%1轮: +%2个新服务器 (累计%3)", round + 1, added, seen.size()));

        if (added == 0) {
            break; // 本轮无新增，到底了
        }

        // 上滑露出更多
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto sid = MaaControllerPostSwipe(ctrl, 360, 900, 360, 200, 200);
        MaaControllerWait(ctrl, sid);
        Sleep(500);
    }

    LogUtils::info(fmt("服务器列表扫描完成，共 %1 个", ordered.size()));
    return true;
}

MaaBool ServerModule::ServerRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = Config::nodeAttach(context, nodeName);
    bool init = attach.get("init", json::value(false)).as_boolean();
    bool output = attach.get("output", json::value(false)).as_boolean();

    if (init) {
        server_.clear();
        serverSeen_.clear();
        return true;
    }

    if (output) {
        if (server_.empty()) {
            LogUtils::error("未识别到任何服务器");
        }
        else {
            std::string servers;
            for (auto& name : server_) {
                if (!servers.empty()) {
                    servers += ", ";
                }
                servers += name;
            }
            LogUtils::warn(fmt("识别到的服务器: %1", servers));
        }
        return true;
    }

    return true;
}

// 判断是否已在目标服务器页面：OCR 服务器名/线路，与 attach 传入的目标比对。
MaaBool ServerModule::targetServerCheck(
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
    auto attach = Config::nodeAttach(context, nodeName);
    std::string name = MapNavigator::normalizeServerName(attach.get("target_server_name", json::value("")).as_string());
    std::string line = MapNavigator::normalizeServerName(attach.get("target_server_line", json::value("")).as_string());
    if (name.empty()) {
        return false;
    }
    std::string target = name + line;

    if (!roi) {
        return false;
    }

    auto recoParam = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    bool isInGame = false;
    if (recoParam.is_object()) {
        isInGame = recoParam.get("isInGame", json::value(false)).as_boolean();
    }
    std::string current;
    if (isInGame) {
        current = MapNavigator::normalizeServerName(recog::readTextViaEntry(context, image, "CustomGetCurrentServerNameInGame", "{}", nullptr));
    }
    else {
        // CustomCurrentServerName 是 And 节点，取 LoginPageServerName 子识别的 OCR 结果
        auto sub = recog::getAndSubDetail(context, image, "CustomCurrentServerName", "LoginPageServerName");
        if (sub.is_object()) {
            auto& all = sub["all"];
            if (all.is_array()) {
                for (auto& item : all.as_array()) {
                    current += item["text"].as_string();
                }
            }
        }
        current = MapNavigator::normalizeServerName(current);
    }
    if (current.empty()) {
        return false;
    }

    bool hit = current.find(target) != std::string::npos;
    if (hit) {
        LogUtils::info(fmt("已切换到服务器:[%1]", current));
    }

    return hit;
}

namespace {

MaaBool findTargetServerThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                              const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                              void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return ServerModule::findTargetServer(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool GetServerListActThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                              const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return ServerModule::GetServerListAct(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool ServerRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                          const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return ServerModule::ServerRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool targetServerCheckThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                               const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                               void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return ServerModule::targetServerCheck(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void ServerModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "FindTargetServer", findTargetServerThunk, userData);
    registerCustomAction(res, "GetServerListAct", GetServerListActThunk, userData);
    registerCustomAction(res, "ServerRecord", ServerRecordThunk, userData);
    registerCustomRecognition(res, "TargetServerCheck", targetServerCheckThunk, userData);
}
