#include "EndlessWealthIsleModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

#include <cstdio>
#include <cstdlib>
#include <windows.h>

namespace
{

int readCount(MaaContext* context, MaaRect roi)
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
    MaaBuffer buf;
    MaaRect dummy;
    int val = 0;
    if (recog::run(context, cap.img, "count", json, &dummy, buf.get())) {
        std::string detail = buf.str();
        auto j = jsonutil::parse(detail);
        std::string text;
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }
        val = std::atoi(text.c_str());
    }
    return val;
}

bool findDifficulty(MaaContext* context, const std::string& name, MaaRect* outBox)
{
    ScreenCap cap(context);
    char json[256];
    snprintf(json, sizeof(json), R"({"label":{"recognition":"OCR","expected":"%s","roi":[112,437,507,300]}})", name.c_str());
    return recog::run(context, cap.img, "label", json, outBox, nullptr);
}

bool clickTmpl(MaaContext* context, const char* tmpl, MaaRect roi)
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
    MaaBuffer buf;
    MaaRect bestBox;
    if (!recog::run(context, cap.img, "adj", json, &bestBox, buf.get())) {
        return false;
    }
    std::string detail = buf.str();
    auto j = jsonutil::parse(detail);
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
    screen::waitFreezes(context, { 175, 407, 444, 261 });
    return true;
}

} // namespace

MaaBool EndlessWealthIsleModule::assignChallengeTimes(
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
    LogUtils::info(fmt("财富岛: 本期剩余未分配挑战次数：%1", total));
    if (total != 42) {
        LogUtils::error("财富岛: 本期挑战次数不满42，请手动执行");
        return true;
    }

    static const char* g_names[] = { "普通", "困难", "噩梦" };
    static const int g_targets[] = { 9, 5, 28 };

    for (int i = 0; i < 3; ++i) {
        MaaRect labelBox;
        if (!findDifficulty(context, g_names[i], &labelBox)) {
            continue;
        }
        MaaRect btnRoi = { labelBox.x, labelBox.y - 20, labelBox.width + 460, labelBox.height + 40 };
        MaaRect dispRoi = { labelBox.x + 250, labelBox.y - 20, 100, labelBox.height + 40 };

        int clicks = 0;
        int cur = readCount(context, dispRoi);
        while (cur != g_targets[i]) {
            int diff = g_targets[i] - cur;
            bool plus = diff > 0;
            int delta = (std::abs(diff) >= 10) ? 10 : 1;
            const char* tmpl = plus ? (delta == 1 ? "EndlessWealthIsle/AddOne.png" : "EndlessWealthIsle/AddTen.png")
                                    : (delta == 1 ? "EndlessWealthIsle/SubOne.png" : "EndlessWealthIsle/SubTen.png");
            if (!clickTmpl(context, tmpl, btnRoi)) {
                break;
            }
            if (clicks == 0) {
                MaaRect newBox;
                if (findDifficulty(context, g_names[i], &newBox)) {
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

bool checkSealReward(MaaContext* context, int num)
{
    ScreenCap cap(context);
    char name[64], json[512];
    snprintf(name, sizeof(name), "SealStone%dReward", num);
    snprintf(json, sizeof(json), R"({"%s":{"recognition":"TemplateMatch","template":"EndlessWealthIsle/%s.png"}})", name, name);
    MaaRect dummy;
    return recog::run(context, cap.img, name, json, &dummy, nullptr);
}

bool findSealStone(MaaContext* context, int num, MaaRect* outBox)
{
    ScreenCap cap(context);
    char name[64], json[512];
    snprintf(name, sizeof(name), "SealStone%d", num);
    snprintf(json, sizeof(json), R"({"%s":{"recognition":"TemplateMatch","template":"EndlessWealthIsle/%s.png"}})", name, name);
    MaaRect tmplBox;
    if (!recog::run(context, cap.img, name, json, &tmplBox, nullptr)) {
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
    if (!recog::run(context, cap.img, "confirm", ocrJson, nullptr, nullptr)) {
        return false;
    }

    outBox->x = tmplBox.x + tmplBox.width / 2;
    outBox->y = tmplBox.y + tmplBox.height - 50;
    outBox->width = tmplBox.width;
    outBox->height = tmplBox.height;
    return true;
}

} // namespace

namespace
{

MaaBool assignChallengeTimesThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return EndlessWealthIsleModule::assignChallengeTimes(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void EndlessWealthIsleModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "EndlessWealthIsleAssignChallengeTimes", assignChallengeTimesThunk, userData);
}
