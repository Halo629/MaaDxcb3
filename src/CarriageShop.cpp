#include "Comm.h"

namespace
{

using namespace std::string_literals;

enum class SceneType
{
    LoginGameNotice,
    LoginEntryGame,
    LoginPressAnyKey,
    LoginConfirmBtn,
    GameReturnBtn,
    GameConfirmBtn,
    GameContinueBtn,
    GameReturnTown,
    GameEndExplore,
    GameQuitBattle,
    GameQuitLab,
};

struct SceneCheck
{
    SceneType type;
    const char* entry;
};

// ======================== 马车商店 ========================
bool findAvailableSilverItem(MaaContext* context, MaaRect* box)
{
    auto* tasker = MaaContextGetTasker(context);

    ScreenCap cap(context);
    const char* findCoinsJson =
        R"({"find_coins":{"recognition":"TemplateMatch","template":"carriage_shop/silver_coin.png","roi":[75, 407, 574, 686]}})";
    MaaRect unused;
    auto detailBuf = MaaStringBufferCreate();
    if (!doRecognition(context, cap.img, "find_coins", findCoinsJson, &unused, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return false;
    }

    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);

    auto detailJson = json::parse(detail).value_or(json::value { });
    auto& allResults = detailJson["filtered"].as_array();
    if (allResults.empty()) {
        return false;
    }

    for (size_t i = 0; i < allResults.size(); ++i) {
        auto& item = allResults[i];
        int x = item["box"][0].as_integer(), y = item["box"][1].as_integer();
        int w = item["box"][2].as_integer(), h = item["box"][3].as_integer();

        constexpr int kCoinRoiTop = 400;
        int soldRoiTop = y - 180;
        if (soldRoiTop < kCoinRoiTop) {
            auto* ctrl = MaaTaskerGetController(tasker);
            auto s = MaaControllerPostSwipe(ctrl, 450, 760, 450, 850, 100);
            MaaControllerWait(ctrl, s);
            return findAvailableSilverItem(context, box);
        }

        char soldJson[256];
        snprintf(
            soldJson,
            sizeof(soldJson),
            R"({"sold":{"recognition":"TemplateMatch","template":"carriage_shop/sold_out.png","roi":[%d,%d,%d,%d]}})",
            x - 20,
            y - 180,
            w + 100,
            h + 180);
        MaaBool soldOut = doRecognition(context, cap.img, "sold", soldJson, nullptr, nullptr);
        if (soldOut) {
            continue;
        }

        box->x = x;
        box->y = y;
        box->width = w;
        box->height = h;
        break;
    }
    return true;
}

bool confirmPurchase(MaaContext* context, MaaTaskId task_id)
{
    auto* tasker = MaaContextGetTasker(context);

    {
        ScreenCap cap(context);
        MaaRect tipsBox;
        const char* tipBoxJson = R"({"check_tips":{"recognition":"OCR","expected":"今日不再提示","roi":[114,482,488,218]}})";
        if (doRecognition(context, cap.img, "check_tips", tipBoxJson, &tipsBox)) {
            auto* ctrl = MaaTaskerGetController(tasker);
            auto tipsClickId = MaaControllerPostClick(ctrl, tipsBox.x + tipsBox.width / 2, tipsBox.y + tipsBox.height / 2);
            MaaControllerWait(ctrl, tipsClickId);
            postWaitFreezes(context);

            MaaRect confirmBox;
            const char* confirmBtnJson =
                R"({"check_confirm":{"recognition":"TemplateMatch","template":"button/confirm_btn.png","roi":[148,702,431,152]}})";
            if (!doRecognition(context, cap.img, "check_confirm", confirmBtnJson, &confirmBox)) {
                return false;
            }
            auto confirmClickId = MaaControllerPostClick(ctrl, confirmBox.x + confirmBox.width / 2, confirmBox.y + confirmBox.height / 2);
            MaaControllerWait(ctrl, confirmClickId);
            postWaitFreezes(context);
        }
    }

    ScreenCap cap(context);
    MaaRect cancelBox;
    const char* cancelBtnJson = R"({"check_cancel":{"recognition":"TemplateMatch","template":"button/cancel_btn.png"}})";
    if (doRecognition(context, cap.img, "check_cancel", cancelBtnJson, &cancelBox)) {
        auto* ctrl = MaaTaskerGetController(tasker);
        auto buyClickId = MaaControllerPostClick(ctrl, cancelBox.x + cancelBox.width + 300, cancelBox.y + cancelBox.height / 2);
        MaaControllerWait(ctrl, buyClickId);
        postWaitFreezes(context, ScreenArea_MidCenter, 500, 3000);
        auto dismissClickId = MaaControllerPostClick(ctrl, 680, 1150);
        MaaControllerWait(ctrl, dismissClickId);
        postWaitFreezes(context);
    }
    return true;
}

MaaBool buySilverAct(
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
    int swipCount = 0;
    while (true) {
        MaaRect silverItemBox { };
        if (!findAvailableSilverItem(context, &silverItemBox)) {
            auto* ctrl = MaaTaskerGetController(tasker);
            auto id = MaaControllerPostSwipe(ctrl, 450, 850, 450, 720, 100);
            MaaControllerWait(ctrl, id);
            postWaitFreezes(context);
            if (swipCount++ == 4) {
                return false;
            }
            continue;
        }
        if (silverItemBox.x == 0) {
            return true;
        }
        auto* ctrl = MaaTaskerGetController(tasker);
        auto clickId = MaaControllerPostClick(ctrl, silverItemBox.x + silverItemBox.width / 2, silverItemBox.y + silverItemBox.height / 2);
        MaaControllerWait(ctrl, clickId);
        postWaitFreezes(context);
        if (!confirmPurchase(context, task_id)) {
            return false;
        }
    }
    return false;
}

} // namespace

void registerCustomScene(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "BuySilverAct", buySilverAct, user_data);
}
