#include "Comm.h"

static bool isInGreatMarshExpeditionPage(MaaContext* context)
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

static bool findEnterButton(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    return doRecognition(
        context,
        cap.img,
        "enter",
        R"({"enter":{"recognition":"OCR","expected":"进入","roi":[273,928,184,138]}})",
        outBox,
        nullptr);
}

MaaBool greatMarshExpeditionClickMapEntry(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto* tasker = MaaContextGetTasker(context);

    // 1. 一次性匹配出地图上所有可点击的入口
    ScreenCap cap(context);
    auto detailBuf = MaaStringBufferCreate();
    MaaRect dummy;
    const char* recoJson =
        R"({"entry":{"recognition":"TemplateMatch","template":"guild/GreatMarshExpeditionMapEntry.png","roi":[5,87,713,1157],"order_by":"Score"}})";
    if (!doRecognition(context, cap.img, "entry", recoJson, &dummy, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return false;
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);

    auto j = json::parse(detail).value_or(json::value { });
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
        postWaitFreezes(context);

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
            postWaitFreezes(context);
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

void registerCustomGuild(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "GreatMarshExpeditionClickMapEntry", greatMarshExpeditionClickMapEntry, user_data);
}
