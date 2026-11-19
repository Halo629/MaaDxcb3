#include "GuildModule.h"

#include "Registry.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"

#include <windows.h>

namespace {

bool isInGreatMarshExpeditionPage(MaaContext* context)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    auto id = MaaContextRunRecognition(context, "InGreatMarshExpeditionPage", "{}", cap.img);
    if (id == MaaInvalidId) {
        return false;
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, nullptr, nullptr, nullptr, nullptr);
    return hit;
}

bool findEnterButton(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    return recog::run(
        context,
        cap.img,
        "enter",
        R"({"enter":{"recognition":"OCR","expected":"进入","roi":[273,928,184,138]}})",
        outBox,
        nullptr);
}

} // namespace

MaaBool GuildModule::greatMarshExpeditionClickMapEntry(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto* tasker = MaaContextGetTasker(context);

    // 1. 一次性匹配出地图上所有可点击的入口
    ScreenCap cap(context);
    MaaBuffer detailBuf;
    MaaRect dummy;
    const char* recoJson =
        R"({"entry":{"recognition":"TemplateMatch","template":"guild/GreatMarshExpeditionMapEntry.png","roi":[5,87,713,1157],"order_by":"Score"}})";
    if (!recog::run(context, cap.img, "entry", recoJson, &dummy, detailBuf.get())) {
        return false;
    }
    std::string detail = detailBuf.str();

    auto j = jsonutil::parse(detail);
    auto results = j["all"].as_array();
    if (results.empty()) {
        return false;
    }

    // 2. 逐个点击入口：点入口 → OCR"进入"并点击 → 校验是否真正进入，没进就再点"进入"
    for (auto& item : results) {
        if (MaaTaskerStopping(tasker)) {
            return false;
        }
        auto b = item["box"];
        MaaRect entryBox { b[0].as_integer(), b[1].as_integer(), b[2].as_integer(), b[3].as_integer() };
        auto* ctrl = MaaTaskerGetController(tasker);
        auto cid = MaaControllerPostClick(ctrl, entryBox.x + entryBox.width / 2, entryBox.y + entryBox.height / 2);
        MaaControllerWait(ctrl, cid);
        screen::waitFreezes(context);

        bool entered = false;
        for (int attempt = 0; attempt < 3 && !entered; ++attempt) {
            if (MaaTaskerStopping(tasker)) {
                return false;
            }
            MaaRect enterBox;
            if (!findEnterButton(context, &enterBox)) {
                break; // 没识别到"进入"按钮，跳到 ESC 换下一个
            }
            ctrl = MaaTaskerGetController(tasker);
            auto enterId = MaaControllerPostClick(ctrl, enterBox.x + enterBox.width / 2, enterBox.y + enterBox.height / 2);
            MaaControllerWait(ctrl, enterId);
            screen::waitFreezes(context);
            if (isInGreatMarshExpeditionPage(context)) {
                entered = true;
            }
        }
        if (entered) {
            return true;
        }

        ctrl = MaaTaskerGetController(tasker);
        auto escId = MaaControllerPostClickKey(ctrl, 111);
        MaaControllerWait(ctrl, escId);
        Sleep(500);
    }

    return false;
}

namespace {

MaaBool greatMarshExpeditionClickMapEntryThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                               const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GuildModule::greatMarshExpeditionClickMapEntry(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void GuildModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "GreatMarshExpeditionClickMapEntry", greatMarshExpeditionClickMapEntryThunk, userData);
}
