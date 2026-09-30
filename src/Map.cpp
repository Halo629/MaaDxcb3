#include "Comm.h"
constexpr int kMaxMapDist = 350;

struct MapNode
{
    std::string type;
    MaaRect box;
    std::vector<int> edges;
};

const char* kMapTemplates[] = { "harwood_battle", "harwood_event", "harwood_final" };

static void findNodesByTemplate(
    MaaContext* context,
    const MaaImageBuffer* img,
    const MaaRect* roi,
    const char* tmplName,
    std::vector<MapNode>& out,
    double threshold = 0.8)
{
    auto* tasker = MaaContextGetTasker(context);
    char pJson[512];
    snprintf(
        pJson,
        sizeof(pJson),
        R"({"%s":{"recognition":"TemplateMatch","template":"%s.png","threshold":%g,"roi":[%d,%d,%d,%d]}})",
        tmplName,
        tmplName,
        threshold,
        roi->x,
        roi->y,
        roi->width,
        roi->height);
    MaaRect unused;
    auto detailBuf = MaaStringBufferCreate();
    if (!doRecognition(context, img, tmplName, pJson, &unused, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return;
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto j = json::parse(detail).value_or(json::value { });
    for (auto& item : j["filtered"].as_array()) {
        MapNode n;
        n.type = tmplName;
        n.box.x = item["box"][0].as_integer();
        n.box.y = item["box"][1].as_integer();
        n.box.width = item["box"][2].as_integer();
        n.box.height = item["box"][3].as_integer();
        out.push_back(n);
    }
}

static bool detectYellowLine(MaaContext* context, const MaaImageBuffer* img, const MapNode& a, const MapNode& b)
{
    int cx1 = a.box.x + a.box.width / 2, cy1 = a.box.y + a.box.height / 2;
    int cx2 = b.box.x + b.box.width / 2, cy2 = b.box.y + b.box.height / 2;
    int dist = (int)std::hypot(cx2 - cx1, cy2 - cy1);
    if (dist > kMaxMapDist) {
        return false;
    }
    constexpr int kIconMargin = 25;
    if (dist <= kIconMargin * 2 + 10) {
        int mx = (cx1 + cx2) / 2, my = (cy1 + cy2) / 2;
        char pJson[200];
        snprintf(
            pJson,
            sizeof(pJson),
            R"({"yl":{"recognition":"ColorMatch","method":40,"lower":[22,140,120],"upper":[36,255,255],"roi":[%d,%d,8,8],"connected":true,"count":3}})",
            mx - 4,
            my - 4);
        return doRecognition(context, img, "yl", pJson, nullptr, nullptr);
    }
    int hit = 0;
    for (int i = 1; i <= 2; ++i) {
        int sx = cx1 + (cx2 - cx1) * i / 3, sy = cy1 + (cy2 - cy1) * i / 3;
        char pJson[200];
        snprintf(
            pJson,
            sizeof(pJson),
            R"({"yl":{"recognition":"ColorMatch","method":40,"lower":[22,140,120],"upper":[36,255,255],"roi":[%d,%d,8,8],"connected":true,"count":4}})",
            sx - 4,
            sy - 4);
        if (doRecognition(context, img, "yl", pJson, nullptr, nullptr)) {
            hit++;
        }
    }
    return hit >= 1;
}

// ======================== DifficultySelectBox ========================
MaaBool difficultySelectBox(
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
    int target = attach.get("difficulty", json::value(1)).as_integer();
    int cx = roi->x + roi->width / 2, cy = roi->y + roi->height / 2;

    while (true) {
        ScreenCap cap(context);
        char ocrJson[256];
        snprintf(ocrJson, sizeof(ocrJson), R"({"diff_ocr":{"recognition":"OCR","roi":[%d,%d,100,60]}})", cx - 50, cy - 30);
        auto detailBuf = MaaStringBufferCreate();
        MaaRect ocrBox;
        if (!doRecognition(context, cap.img, "diff_ocr", ocrJson, &ocrBox, detailBuf)) {
            MaaStringBufferDestroy(detailBuf);
            Sleep(500);
            continue;
        }
        std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
        MaaStringBufferDestroy(detailBuf);
        auto j = json::parse(detail).value_or(json::value { });
        int current = 0;
        if (auto& best = j["best"]; best.is_object()) {
            current = std::atoi(best["text"].as_string().c_str());
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            current = std::atoi(all[0]["text"].as_string().c_str());
        }
        if (current == target) {
            return true;
        }
        int diff = target - current;
        bool useTen = std::abs(diff) >= 10;
        const char* entry = diff > 0 ? (useTen ? "add_ten_btn" : "add_btn") : (useTen ? "sub_ten_btn" : "sub_btn");
        char pJson[512];
        snprintf(
            pJson,
            sizeof(pJson),
            R"({"%s":{"recognition":"TemplateMatch","template":"%s.png","roi":[%d,%d,%d,%d],"order_by":"Score"}})",
            entry,
            entry,
            roi->x,
            roi->y,
            roi->width,
            roi->height);
        MaaRect btnBox;
        if (doRecognition(context, cap.img, entry, pJson, &btnBox, nullptr)) {
            auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto cid = MaaControllerPostClick(ctrl, btnBox.x + btnBox.width / 2, btnBox.y + btnBox.height / 2);
            MaaControllerWait(ctrl, cid);
            Sleep(one_second_delay);
        }
    }
    return false;
}

// ======================== MapNavigator ========================
MaaBool mapNavigator(
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
    auto param = json::parse(custom_recognition_param).value_or(json::value { });
    std::string target = param.get("target", std::string("boss"));
    int tmplCount = sizeof(kMapTemplates) / sizeof(kMapTemplates[0]);
    std::vector<MapNode> nodes;
    for (int i = 0; i < tmplCount; ++i) {
        findNodesByTemplate(context, image, roi, kMapTemplates[i], nodes);
    }
    int n = (int)nodes.size();
    if (n == 0) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (detectYellowLine(context, image, nodes[i], nodes[j])) {
                nodes[i].edges.push_back(j);
                nodes[j].edges.push_back(i);
            }
        }
    }
    int start = 0, goal = -1;
    for (int i = 0; i < n; ++i) {
        if (nodes[i].type == target) {
            goal = i;
            break;
        }
    }
    if (goal == -1) {
        return false;
    }
    std::vector<int> prev(n, -1), visited(n, false);
    std::queue<int> q;
    q.push(start);
    visited[start] = true;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        if (u == goal) {
            break;
        }
        for (int v : nodes[u].edges) {
            if (!visited[v]) {
                visited[v] = true;
                prev[v] = u;
                q.push(v);
            }
        }
    }
    if (prev[goal] == -1 && start != goal) {
        return false;
    }
    std::vector<int> path;
    for (int at = goal; at != -1; at = prev[at]) {
        path.push_back(at);
    }
    std::reverse(path.begin(), path.end());
    int nextIdx = (path.size() > 1) ? path[1] : path[0];
    auto& next = nodes[nextIdx];
    MaaRectSet(out_box, next.box.x, next.box.y, next.box.width, next.box.height);
    return true;
}

// ======================== HarwoodMapHandler ========================
static void findAllNodes(MaaContext* context, const MaaImageBuffer* img, const MaaRect* roi, std::vector<MapNode>& out)
{
    int n = sizeof(kMapTemplates) / sizeof(kMapTemplates[0]);
    for (int i = 0; i < n; ++i) {
        findNodesByTemplate(context, img, roi, kMapTemplates[i], out);
    }
}

static bool isWrongClick(MaaContext* context, MaaRect* popupBox)
{
    ScreenCap cap(context);
    if (doRecognition(context, cap.img, "cb", R"({"cb":{"recognition":"TemplateMatch","template":"close_btn.png"}})", popupBox, nullptr)) {
        return true;
    }
    if (doRecognition(
            context,
            cap.img,
            "cf",
            R"({"cf":{"recognition":"TemplateMatch","template":"confirm_btn.png"}})",
            popupBox,
            nullptr)) {
        return true;
    }
    return false;
}

static void dismissPopup(MaaContext* context, const MaaRect& popupBox)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, popupBox.x + popupBox.width / 2, popupBox.y + popupBox.height / 2);
    MaaControllerWait(ctrl, id);
    Sleep(one_second_delay);
}

MaaBool harwoodMapHandler(
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
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    constexpr int kProximity = 350, kMaxSteps = 30;

    auto checkEntrance = [&](const MaaImageBuffer* img) {
        MaaRect box;
        char pJson[512];
        snprintf(
            pJson,
            sizeof(pJson),
            R"({"ec":{"recognition":"TemplateMatch","template":"harwood_entrance.png","threshold":0.8,"roi":[%d,%d,%d,%d]}})",
            roi->x,
            roi->y,
            roi->width,
            roi->height);
        return doRecognition(context, img, "ec", pJson, &box, nullptr);
    };

    bool entranceFound = false;
    MaaRect entranceBox { };
    {
        ScreenCap cap(context);
        entranceFound = checkEntrance(cap.img);
    }

    int curX = 0, curY = 0;
    if (entranceFound) {
        curX = entranceBox.x + entranceBox.width / 2;
        curY = entranceBox.y + entranceBox.height / 2;
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto cId = MaaControllerPostClick(ctrl, curX, curY);
        MaaControllerWait(ctrl, cId);
        Sleep(one_second_delay);
    }
    else {
        ScreenCap cap(context);
        std::vector<MapNode> completed;
        findNodesByTemplate(context, cap.img, roi, "harwood_complete", completed);
        if (completed.empty()) {
            return false;
        }
        int bestIdx = 0;
        for (size_t i = 1; i < completed.size(); ++i) {
            if (completed[i].box.y < completed[bestIdx].box.y) {
                bestIdx = (int)i;
            }
        }
        curX = completed[bestIdx].box.x + completed[bestIdx].box.width / 2;
        curY = completed[bestIdx].box.y + completed[bestIdx].box.height / 2;
    }

    for (int step = 0; step < kMaxSteps; ++step) {
        ScreenCap cap(context);
        std::vector<MapNode> allNodes;
        findAllNodes(context, cap.img, roi, allNodes);
        std::vector<MapNode> finals;
        findNodesByTemplate(context, cap.img, roi, "harwood_final", finals, 0.5);

        struct Candidate
        {
            int cx, cy, dist;
            std::string type;
            bool isFinal = false;
        };

        std::vector<Candidate> candidates;
        for (auto& n : allNodes) {
            int nx = n.box.x + n.box.width / 2, ny = n.box.y + n.box.height / 2;
            if (ny >= curY) {
                continue;
            }
            int d = (int)std::hypot(nx - curX, ny - curY);
            if (d <= kProximity) {
                candidates.push_back({ nx, ny, d, n.type, false });
            }
        }
        for (auto& f : finals) {
            int fx = f.box.x + f.box.width / 2, fy = f.box.y + f.box.height / 2;
            int d = (int)std::hypot(fx - curX, fy - curY);
            if (d <= kProximity) {
                candidates.insert(candidates.begin(), { fx, fy, d, "", true });
            }
        }
        if (candidates.empty()) {
            break;
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

        bool moved = false;
        for (auto& c : candidates) {
            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto clickId = MaaControllerPostClick(ctrl, c.cx, c.cy);
            MaaControllerWait(ctrl, clickId);
            Sleep(one_second_delay);
            MaaRect popupBox;
            if (!isWrongClick(context, &popupBox)) {
                if (c.type == "harwood_battle") {
                    MaaRect battleBox;
                    const char* bj = R"({"bt":{"recognition":"TemplateMatch","template":"harwood_battle.png"}})";
                    if (doRecognition(context, nullptr, "bt", bj, &battleBox, nullptr)) {
                        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
                        auto bId = MaaControllerPostClick(ctrl, battleBox.x + battleBox.width / 2, battleBox.y + battleBox.height / 2);
                        MaaControllerWait(ctrl, bId);
                        Sleep(3000);
                        auto fId = MaaControllerPostClick(ctrl, 356 + 51, 1095 + 32);
                        MaaControllerWait(ctrl, fId);
                        Sleep(one_second_delay);
                    }
                }
                else if (c.type == "harwood_event") {
                    for (int retry = 0; retry < 10; ++retry) {
                        ScreenCap ec(context);
                        MaaRect rb;
                        const char* bJ = R"({"bt":{"recognition":"TemplateMatch","template":"battle_btn.png"}})";
                        const char* cJ = R"({"cf":{"recognition":"TemplateMatch","template":"button/confirm_btn.png"}})";
                        const char* lJ = R"({"lv":{"recognition":"TemplateMatch","template":"button/leave_btn.png"}})";
                        const char* tJ = R"({"tk":{"recognition":"TemplateMatch","template":"harwood_talk.png"}})";
                        if (doRecognition(context, ec.img, "bt", bJ, &rb, nullptr) || doRecognition(context, ec.img, "cf", cJ, &rb, nullptr)
                            || doRecognition(context, ec.img, "lv", lJ, &rb, nullptr)
                            || doRecognition(context, ec.img, "tk", tJ, &rb, nullptr)) {
                            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
                            auto rId = MaaControllerPostClick(ctrl, rb.x + rb.width / 2, rb.y + rb.height / 2);
                            MaaControllerWait(ctrl, rId);
                            Sleep(one_second_delay);
                        }
                        auto fxId = MaaControllerPostClick(ctrl, 370, 1071);
                        MaaControllerWait(ctrl, fxId);
                        Sleep(500);
                        MaaRect localRoi { c.cx - 50, c.cy - 50, 100, 100 };
                        std::vector<MapNode> comps;
                        findNodesByTemplate(context, ec.img, &localRoi, "harwood_complete", comps);
                        if (!comps.empty()) {
                            break;
                        }
                        Sleep(one_second_delay);
                    }
                }
                if (c.type == "harwood_final") {
                    {
                        ScreenCap fc(context);
                        MaaRect tb;
                        if (doRecognition(
                                context,
                                fc.img,
                                "tk",
                                R"({"tk":{"recognition":"TemplateMatch","template":"harwood_talk.png"}})",
                                &tb,
                                nullptr)) {
                            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
                            auto tkId = MaaControllerPostClick(ctrl, tb.x + tb.width / 2, tb.y + tb.height / 2);
                            MaaControllerWait(ctrl, tkId);
                        }
                    }
                    Sleep(2000);
                    auto fId = MaaControllerPostClick(ctrl, 612, 1196);
                    MaaControllerWait(ctrl, fId);
                    Sleep(one_second_delay);
                    {
                        ScreenCap sc(context);
                        MaaRect cb;
                        char cj[300];
                        snprintf(
                            cj,
                            sizeof(cj),
                            R"({"sc":{"recognition":"OCR","expected":"开始挑战","roi":[%d,%d,%d,%d]}})",
                            222,
                            921,
                            271,
                            154);
                        if (doRecognition(context, sc.img, "sc", cj, &cb, nullptr)) {
                            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
                            auto cId = MaaControllerPostClick(ctrl, cb.x + cb.width / 2, cb.y + cb.height / 2);
                            MaaControllerWait(ctrl, cId);
                        }
                    }
                    return true;
                }
                curX = c.cx;
                curY = c.cy;
                moved = true;
                break;
            }
            dismissPopup(context, popupBox);
        }
        if (!moved) {
            break;
        }
    }
    return true;
}

void registerCustomMap(MaaResource* res, void* user_data)
{
    registerCustomRecognition(res, "DifficultySelectBox", difficultySelectBox, user_data);
    registerCustomRecognition(res, "MapNavigator", mapNavigator, user_data);
    registerCustomRecognition(res, "HarwoodMapHandler", harwoodMapHandler, user_data);
}

