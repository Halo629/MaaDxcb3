#include "Comm.h"

static std::vector<std::string> g_server;            // 保持识别顺序
static std::unordered_set<std::string> g_serverSeen;  // 去重
// 在服务器列表中查找目标服务器
MaaBool findTargetServer(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_recognition_name,
    const char* custom_recognition_param,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* trans_arg,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    auto attach = getNodeAttach(context, node_name);
    std::string name = normalizeServerName(attach.get("target_server_name", json::value("")).as_string());
    std::string line = normalizeServerName(attach.get("target_server_line", json::value("")).as_string());
    if (name.empty()) {
        LogUtils::error("切换服务器失败: 未指定目标服务器名称");
        return false;
    }
    std::string target = name + line;

    auto sub = getAndSubDetail(context, image, "CustomServerList", "ServerName");
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
        std::string text = normalizeServerName(item["text"].as_string());
        if (text.empty()) {
            continue;
        }
        if (g_serverSeen.insert(text).second) {
            g_server.push_back(text); // 记录识别到的服务器（按识别顺序去重）
        }
        if (text.find(target) == std::string::npos) {
            continue;
        }

        LogUtils::info(fmt("找到服务器:[%1],切换中...", text));

        out_box->x = item["box"][0].as_integer();
        out_box->y = item["box"][1].as_integer();
        out_box->width = item["box"][2].as_integer();
        out_box->height = item["box"][3].as_integer();

        return true;
    }
    return false;
}

// 滚动扫描服务器列表：OCR → 上滑 → OCR → 直到没有新服务器
MaaBool GetServerListAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    std::vector<std::string> ordered; // 保持插入顺序
    std::unordered_set<std::string> seen;

    auto doOCR = [&]() -> std::vector<std::string> {
        ScreenCap cap(context);
        auto detailBuf = MaaStringBufferCreate();
        MaaRect outBox;
        if (!doRecognition(
                context,
                cap.img,
                "scan_list",
                R"({"scan_list":{"recognition":"OCR","roi":[104,300,350,663],"order":"Vertical"}})",
                &outBox,
                detailBuf)) {
            MaaStringBufferDestroy(detailBuf);
            return { };
        }
        std::string detailStr(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
        MaaStringBufferDestroy(detailBuf);

        auto j = json::parse(detailStr);
        if (!j || !j->contains("all")) {
            return { };
        }

        std::vector<std::string> result;
        for (auto& item : (*j)["all"].as_array()) {
            std::string text = normalizeServerName(item["text"].as_string());
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

MaaBool ServerRecord(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto attach = getNodeAttach(context, node_name);
    bool init = attach.get("init", json::value(false)).as_boolean();
    bool output = attach.get("output", json::value(false)).as_boolean();

    if (init) {
        g_server.clear();
        g_serverSeen.clear();
        return true;
    }

    if (output) {
        if (g_server.empty()) {
            LogUtils::error("未识别到任何服务器");
        }
        else {
            std::string servers;
            for (auto& name : g_server) {
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
MaaBool targetServerCheck(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_recognition_name,
    const char* custom_recognition_param,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* trans_arg,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    auto attach = getNodeAttach(context, node_name);
    std::string name = normalizeServerName(attach.get("target_server_name", json::value("")).as_string());
    std::string line = normalizeServerName(attach.get("target_server_line", json::value("")).as_string());
    if (name.empty()) {
        return false;
    }
    std::string target = name + line;

    if (!roi) {
        return false;
    }

    auto recoParam = json::parse(custom_recognition_param ? custom_recognition_param : "{}").value_or(json::value { });
    bool isInGame = false;
    if (recoParam.is_object()) {
        isInGame = recoParam.get("isInGame", json::value(false)).as_boolean();
    }
    std::string current;
    if (isInGame) {
        current = normalizeServerName(ocrText(context, image, "CustomGetCurrentServerNameInGame", "{}", nullptr));
    }
    else {
        // CustomCurrentServerName 是 And 节点，取 LoginPageServerName 子识别的 OCR 结果
        auto sub = getAndSubDetail(context, image, "CustomCurrentServerName", "LoginPageServerName");
        if (sub.is_object()) {
            auto& all = sub["all"];
            if (all.is_array()) {
                for (auto& item : all.as_array()) {
                    current += item["text"].as_string();
                }
            }
        }
        current = normalizeServerName(current);
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

void registerCustomServer(MaaResource* res, void* user_data)
{
    registerCustomRecognition(res, "FindTargetServer", findTargetServer, user_data);
    registerCustomAction(res, "GetServerListAct", GetServerListAct, user_data);
    registerCustomAction(res, "ServerRecord", ServerRecord, user_data);
    registerCustomRecognition(res, "TargetServerCheck", targetServerCheck, user_data);
}
