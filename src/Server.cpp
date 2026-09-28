#include "Comm.h"

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
        LogUtils::log("切换服务器失败: 未指定目标服务器名称", "#ef4444");
        return false;
    }
    std::string target = name + line;

    MaaRect outBox;
    auto detailBuf = MaaStringBufferCreate();
    if (!doRecognition(context, image, "CustomServerList", "{}", &outBox, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        LogUtils::log("读取服务器列表失败", "#ef4444");
        return false;
    }

    std::string detailStr(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);

    auto result = json::parse(detailStr);
    if (!result || !result->contains("all")) {
        LogUtils::log("读取服务器列表失败: OCR 结果为空", "#f59e0b");
        return false;
    }

    // 归一化后做 contains 匹配（去掉冒号空格，避免符号差异）
    for (auto& item : (*result)["all"].as_array()) {
        std::string text = normalizeServerName(item["text"].as_string());
        if (text.empty() || text.find(target) == std::string::npos) {
            continue;
        }

        LogUtils::log(fmt("找到服务器:[%1],切换中...", text), "#22c55e");

        out_box->x = item["box"][0].as_integer();
        out_box->y = item["box"][1].as_integer();
        out_box->width = item["box"][2].as_integer();
        out_box->height = item["box"][3].as_integer();

        return true;
    }

    LogUtils::log(fmt("切换服务器失败,未找到目标服务器:[%1]", target), "#f59e0b");
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

        LogUtils::log(fmt("扫描第%1轮: +%2个新服务器 (累计%3)", round + 1, added, seen.size()), added > 0 ? "#22c55e" : "#6b7280");

        if (added == 0) {
            break; // 本轮无新增，到底了
        }

        // 上滑露出更多
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto sid = MaaControllerPostSwipe(ctrl, 360, 900, 360, 200, 200);
        MaaControllerWait(ctrl, sid);
        Sleep(500);
    }

    LogUtils::log(fmt("服务器列表扫描完成，共 %1 个", ordered.size()), "#22c55e");
    return true;
}

// OCR 服务器名（roi 通过 custom_action_param 传入）。
// 调用方无需再按节点名反查识别结果，节点改名/合并不影响。
MaaBool ReadServerNameAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto param = json::parse(custom_action_param ? custom_action_param : "{}").value_or(json::value { });
    auto roiArr = param.get("roi", json::value { }).as_array();
    if (roiArr.size() != 4) {
        LogUtils::log("ReadServerNameAct: custom_action_param.roi 缺失或非法", "#ef4444");
        return false;
    }

    ScreenCap cap(context);
    char detailJson[256];
    snprintf(
        detailJson,
        sizeof(detailJson),
        R"({"serverName":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})",
        roiArr[0].as_integer(),
        roiArr[1].as_integer(),
        roiArr[2].as_integer(),
        roiArr[3].as_integer());

    std::string name = ocrText(context, cap.img, "serverName", detailJson, nullptr);
    std::string server = normalizeServerName(name);
    LogUtils::log(fmt("ReadServerNameAct: 当前服务器 [%1]", server), "#22c55e");
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
    const char* serverNameNode = isInGame ? "CustomGetCurrentServerNameInGame" : "CustomCurrentServerName";
    std::string current = normalizeServerName(ocrText(context, image, serverNameNode, "{}", nullptr));
    if (current.empty()) {
        return false;
    }

    bool hit = current.find(target) != std::string::npos;
    if (hit) {
        LogUtils::log(fmt("已切换到服务器:[%1]", current), "#22c55e");
    }

    return hit;
}

void registerCustomServer(MaaResource* res, void* user_data)
{
    registerCustomRecognition(res, "FindTargetServer", findTargetServer, user_data);
    registerCustomAction(res, "GetServerListAct", GetServerListAct, user_data);
    registerCustomAction(res, "ReadServerNameAct", ReadServerNameAct, user_data);
    registerCustomRecognition(res, "TargetServerCheck", targetServerCheck, user_data);
}
