#include "Comm.h"

namespace
{

static int readCount(MaaContext* context, MaaRect roi)
{
    ScreenCap cap(context);
    char json[256];
    snprintf(
        json,
        sizeof(json),
        R"({"count":{"recognition":"OCR","roi":[%d,%d,%d,%d],"expected":["\\d+"],"only_rec":true}})",
        roi.x,
        roi.y,
        roi.width,
        roi.height);
    auto buf = MaaStringBufferCreate();
    MaaRect dummy;
    int val = 0;
    if (doRecognition(context, cap.img, "count", json, &dummy, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
        std::string text;
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }
        val = std::atoi(text.c_str());
    }
    MaaStringBufferDestroy(buf);
    return val;
}

static bool findDifficulty(MaaContext* context, const std::string& name, MaaRect* outBox)
{
    ScreenCap cap(context);
    char json[256];
    snprintf(json, sizeof(json), R"({"label":{"recognition":"OCR","expected":"%s","roi":[112,437,507,300]}})", name.c_str());
    return doRecognition(context, cap.img, "label", json, outBox, nullptr);
}

static bool clickTmpl(MaaContext* context, const char* tmpl, MaaRect roi)
{
    ScreenCap cap(context);
    char json[512];
    snprintf(
        json,
        sizeof(json),
        R"({"adj":{"recognition":"TemplateMatch","template":"%s","roi":[%d,%d,%d,%d],"order_by":"Score"}})",
        tmpl,
        roi.x,
        roi.y,
        roi.width,
        roi.height);
    auto buf = MaaStringBufferCreate();
    MaaRect bestBox;
    if (!doRecognition(context, cap.img, "adj", json, &bestBox, buf)) {
        MaaStringBufferDestroy(buf);
        return false;
    }
    std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);
    auto j = json::parse(detail).value_or(json::value { });
    auto& all = j["all"];
    if (all.is_array() && !all.as_array().empty()) {
        auto& first = all.as_array()[0];
        bestBox.x = first["box"][0].as_integer();
        bestBox.y = first["box"][1].as_integer();
        bestBox.width = first["box"][2].as_integer();
        bestBox.height = first["box"][3].as_integer();
    }
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto cid = MaaControllerPostClick(ctrl, bestBox.x + bestBox.width / 2, bestBox.y + bestBox.height / 2);
    MaaControllerWait(ctrl, cid);
    postWaitFreezes(context, { 175, 407, 444, 261 });
    return true;
}

} // namespace

MaaBool endlessWealthIsleAssignChallengeTimes(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    int total = readCount(context, { 477, 388, 32, 50 });
    LogUtils::log(fmt("财富岛: 本期剩余未分配挑战次数：%1", total), "#3b82f6");
    if (total != 42) {
        LogUtils::log("财富岛: 本期挑战次数不满42，请手动执行", "#ef4444");
        return true;
    }

    static const char* kNames[] = { "普通", "困难", "噩梦" };
    static const int kTargets[] = { 9, 5, 28 };

    for (int i = 0; i < 3; ++i) {
        MaaRect labelBox;
        if (!findDifficulty(context, kNames[i], &labelBox)) {
            continue;
        }
        MaaRect btnRoi = { labelBox.x, labelBox.y - 20, labelBox.width + 460, labelBox.height + 40 };
        MaaRect dispRoi = { labelBox.x + 250, labelBox.y - 20, 100, labelBox.height + 40 };

        int clicks = 0;
        int cur = readCount(context, dispRoi);
        while (cur != kTargets[i]) {
            int diff = kTargets[i] - cur;
            bool plus = diff > 0;
            int delta = (std::abs(diff) >= 10) ? 10 : 1;
            const char* tmpl = plus ? (delta == 1 ? "EndlessWealthIsle/AddOne.png" : "EndlessWealthIsle/AddTen.png")
                                    : (delta == 1 ? "EndlessWealthIsle/SubOne.png" : "EndlessWealthIsle/SubTen.png");
            if (!clickTmpl(context, tmpl, btnRoi)) {
                break;
            }
            if (clicks == 0) {
                MaaRect newBox;
                if (findDifficulty(context, kNames[i], &newBox)) {
                    btnRoi = { newBox.x, newBox.y - 20, newBox.width + 460, newBox.height + 40 };
                    dispRoi = { newBox.x + 260, newBox.y - 10, 50, newBox.height + 20 };
                }
            }
            ++clicks;
            cur = readCount(context, dispRoi);
        }
    }
    return true;
}

// ──── 封印石奖励兑换 ────

namespace
{

static bool checkSealReward(MaaContext* context, int num)
{
    ScreenCap cap(context);
    char name[64], json[512];
    snprintf(name, sizeof(name), "SealStone%dReward", num);
    snprintf(json, sizeof(json), R"({"%s":{"recognition":"TemplateMatch","template":"EndlessWealthIsle/%s.png"}})", name, name);
    MaaRect dummy;
    return doRecognition(context, cap.img, name, json, &dummy, nullptr);
}

static bool findSealStone(MaaContext* context, int num, MaaRect* outBox)
{
    ScreenCap cap(context);
    char name[64], json[512];
    snprintf(name, sizeof(name), "SealStone%d", num);
    snprintf(json, sizeof(json), R"({"%s":{"recognition":"TemplateMatch","template":"EndlessWealthIsle/%s.png"}})", name, name);
    MaaRect tmplBox;
    if (!doRecognition(context, cap.img, name, json, &tmplBox, nullptr)) {
        return false;
    }

    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"confirm":{"recognition":"OCR","expected":"封印石|封印|印石|石","roi":[%d,%d,%d,%d]}})",
        tmplBox.x,
        tmplBox.y,
        tmplBox.width,
        tmplBox.height);
    if (!doRecognition(context, cap.img, "confirm", ocrJson, nullptr, nullptr)) {
        return false;
    }

    outBox->x = tmplBox.x + tmplBox.width / 2;
    outBox->y = tmplBox.y + tmplBox.height - 50;
    outBox->width = tmplBox.width;
    outBox->height = tmplBox.height;
    return true;
}

} // namespace

MaaBool endlessWealthIsleExchangeRewards(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    bool claimed[15] = { };

    for (int num = 1; num <= 14; ++num) {
        if (claimed[num]) {
            continue;
        }
        if (num == 5) {
            auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto swId = MaaControllerPostSwipe(ctrl, 200, 400, 400, 400, 200);
            MaaControllerWait(ctrl, swId);
            postWaitFreezes(context);
        }
        MaaRect stoneBox;
        if (!findSealStone(context, num, &stoneBox)) {
            continue;
        }

        for (int retry = 0; retry < 5; ++retry) {
            auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto cid = MaaControllerPostClick(ctrl, stoneBox.x, stoneBox.y);
            MaaControllerWait(ctrl, cid);
            if (waitUntilRecognitionSuccess(
                    context,
                    "rewardBox",
                    R"({"rewardBox":{"recognition":"OCR","expected":"奖励","roi":[331,345,59,44]}})",
                    nullptr,
                    nullptr,
                    3000)) {
                break;
            }
        }
        if (!checkSealReward(context, num)) {
            auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto escId = MaaControllerPostClickKey(ctrl, 111);
            MaaControllerWait(ctrl, escId);
            Sleep(300);
            continue;
        }
        MaaRect openBox;
        if (waitUntilRecognitionSuccess(
                context,
                "openBtn",
                R"({"openBtn":{"recognition":"OCR","expected":"开启","roi":[311,860,100,74]}})",
                &openBox,
                nullptr)) {
            auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto openCid = MaaControllerPostClick(ctrl, openBox.x + openBox.width / 2, openBox.y + openBox.height / 2);
            MaaControllerWait(ctrl, openCid);
            MaaContextRunTask(context, "HandleRewardDialog", "{}");
        }
        claimed[num] = true;
    }

    LogUtils::log("财富岛: 奖励兑换完成", "#22c55e");
    return true;
}

void registerCustomEndlessWealthIsle(MaaResource* resource, void* userData)
{
    registerCustomAction(resource, "EndlessWealthIsleAssignChallengeTimes", endlessWealthIsleAssignChallengeTimes, userData);
    registerCustomAction(resource, "EndlessWealthIsleExchangeRewards", endlessWealthIsleExchangeRewards, userData);
}
