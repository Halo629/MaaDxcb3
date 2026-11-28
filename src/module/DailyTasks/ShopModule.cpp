#include "ShopModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "config/Config.h"
#include "util/JsonUtil.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

#include <algorithm>

#include <windows.h>

const std::array<ShopItemConfig, 6> g_shopItems = { {
    { "timeBondFragmentFound", "timeBondFragmentEnabled", 0U },
    { "starsprigTwigFound", "starsprigTwigEnabled", 1U },
    { "ouroborosRingFound", "ouroborosRingEnabled", 2U },
    { "heartbeatRubbingFound", "heartbeatRubbingEnabled", 3U },
    { "godsWeightsFound", "godsWeightsEnabled", 4U },
    { "divineShadeSoulFound", "divineShadeSoulEnabled", 5U },
} };

uint32_t ShopModule::m_shopMask = 0;
uint32_t ShopModule::m_shopEnabledMask = 0;

namespace {

bool isStopping(MaaContext* context)
{
    return MaaTaskerStopping(MaaContextGetTasker(context));
}

bool runTaskSuccess(MaaContext* context, const char* taskName)
{
    auto* tasker = MaaContextGetTasker(context);
    MaaTaskId id = MaaContextRunTask(context, taskName, "{}");
    if (id == MaaInvalidId) {
        return false;
    }
    MaaStatus status = MaaStatus_Invalid;
    MaaSize nodeCount = 0;
    MaaTaskerGetTaskDetail(tasker, id, nullptr, nullptr, &nodeCount, &status);
    return status == MaaStatus_Succeeded;
}

void swipeTo(MaaContext* context, int x1, int y1, int x2, int y2, int duration)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostSwipe(ctrl, x1, y1, x2, y2, duration);
    MaaControllerWait(ctrl, id);
    screen::waitFreezes(context, ScreenArea_MidCenter, 300, 1000);
}

bool findNodeBox(MaaContext* context, const char* nodeName, MaaRect& outBox)
{
    ScreenCap cap(context);
    return recog::run(context, cap.img, nodeName, "{}", &outBox, nullptr);
}

bool readQuantityAtHandle(MaaContext* context, const MaaRect& handleBox, const std::vector<int>& qOffset, int& out)
{
    MaaRect qb;
    qb.x = handleBox.x + qOffset[0];
    qb.y = handleBox.y + qOffset[1];
    qb.width = handleBox.width + qOffset[2];
    qb.height = handleBox.height + qOffset[3];
    return recog::readNumberFromRoi(context, qb, out);
}

bool findAndClickButton(MaaContext* context, const char* buttonNode)
{
    MaaRect btnBox;
    if (!findNodeBox(context, buttonNode, btnBox)) {
        return false;
    }
    screen::clickPoint(context, btnBox.x + btnBox.width / 2, btnBox.y + btnBox.height / 2);
    return true;
}

} // namespace

MaaBool ShopModule::assistant(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customActionName, const char* customActionParam,
                              MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    auto attach = Config::instance().nodeAttach(context, nodeName);
    bool initFlag = attach.get("initFlag", json::value(false)).as_boolean();

    if (initFlag) {
        m_shopMask = 0;
        m_shopEnabledMask = 0;
        for (auto& item : g_shopItems) {
            if (attach.get(item.enabledKey, json::value(false)).as_boolean()) {
                m_shopEnabledMask |= (1U << item.bit);
            }
        }
    }
    for (auto& item : g_shopItems) {
        if (attach.get(item.foundKey, json::value(false)).as_boolean()) {
            m_shopMask |= (1U << item.bit);
        }
    }
    return true;
}

MaaBool ShopModule::finishCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                const char* customRecognitionName, const char* customRecognitionParam,
                                const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                MaaRect* outBox, MaaStringBuffer* outDetail)
{
    const uint32_t targetMask = m_shopEnabledMask;
    if (targetMask == 0) {
        return true;
    }
    return (m_shopMask & targetMask) == targetMask;
}

MaaBool ShopModule::itemNotBought(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customRecognitionName, const char* customRecognitionParam,
                                  const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                  MaaRect* outBox, MaaStringBuffer* outDetail)
{
    auto param = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    std::string foundKey = param.get("foundKey", json::value("")).as_string();
    if (foundKey.empty()) {
        LogUtils::error("ItemNotBought: 缺少 custom_recognition_param.foundKey");
        return false;
    }
    for (auto& item : g_shopItems) {
        if (foundKey == item.foundKey) {
            return (m_shopMask & (1U << item.bit)) == 0;
        }
    }
    LogUtils::error(fmt("ItemNotBought: 未找到商品 %1", foundKey));
    return false;
}

MaaBool ShopModule::carriageShopSlideBuy(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                         const char* customActionName, const char* customActionParam,
                                         MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    auto param = jsonutil::parse(customActionParam ? customActionParam : "{}");

    std::string sliderNode = param.get("slider", json::value("SliderFound")).as_string();
    std::string currencyNode = param.get("currencyCount", json::value("")).as_string();
    std::string currencyNodeSub = param.get("currencyCountSub", json::value("")).as_string();
    std::string unitPriceNode = param.get("unitPrice", json::value("CarriageShopSlideBuyUnitPrice")).as_string();
    std::string increaseBtn = param.get("increaseButton", json::value("CarriageShopIncreaseButton")).as_string();
    std::string decreaseBtn = param.get("decreaseButton", json::value("CarriageShopDecreaseButton")).as_string();
    std::string canBuyCheck = param.get("canBuyCheck", json::value("CarriageShopCanBuyAfterSlide")).as_string();
    auto qOffset = jsonutil::readIntArray(param.get("quantityOffset", json::value()), { -15, 35, 25, 0 });

    MaaRect thumbBox;
    MaaRect purchaseButtonBox;
    bool slideFound = findNodeBox(context, sliderNode.c_str(), thumbBox);
    bool purchaseFound = findNodeBox(context, "PurchaseButton", purchaseButtonBox);

    if (!slideFound && !purchaseFound) {
        return false;
    }
    if (!slideFound) {
        return true;
    }
    if (currencyNode.empty()) {
        LogUtils::error("CarriageShopSlideBuy: 缺少 currencyCount 配置");
        return false;
    }

    int currency = 0;
    if (!recog::readNumber(context, currencyNode.c_str(), currencyNodeSub.c_str(), currency)) {
        return true;
    }

    int unitPrice = 0;
    if (!recog::readNumber(context, unitPriceNode.c_str(), "UnitPrice", unitPrice) || unitPrice <= 0) {
        LogUtils::error("CarriageShopSlideBuy: 读取单价失败");
        return false;
    }

    MaaRect startBox = thumbBox;
    int startX = startBox.x + startBox.width / 2;
    int startY = startBox.y + startBox.height / 2;
    swipeTo(context, startX, startY, startX + 600, startY, 200);

    MaaRect endBox;
    if (!findNodeBox(context, sliderNode.c_str(), endBox)) {
        endBox = startBox;
    }
    int maxQuantity = 0;
    if (!readQuantityAtHandle(context, endBox, qOffset, maxQuantity) || maxQuantity < 1) {
        LogUtils::error("CarriageShopSlideBuy: 读取最大数量失败");
        return false;
    }

    int buyCount = (std::min)(maxQuantity, currency / unitPrice);
    if (buyCount <= 0 || buyCount >= maxQuantity) {
        return true;
    }

    int endX = endBox.x + endBox.width / 2;
    int endY = endBox.y + endBox.height / 2;
    int clickX = startX + (endX - startX) * (buyCount - 1) / (maxQuantity - 1);
    int clickY = startY + (endY - startY) * (buyCount - 1) / (maxQuantity - 1);
    screen::clickPoint(context, clickX, clickY);
    Sleep(500);

    const int maxRetry = 10;
    for (int attempt = 0; attempt < maxRetry; ++attempt) {
        if (isStopping(context)) {
            return true;
        }
        MaaRect curBox;
        if (!findNodeBox(context, sliderNode.c_str(), curBox)) {
            curBox = endBox;
        }
        int actual = 0;
        if (!readQuantityAtHandle(context, curBox, qOffset, actual)) {
            screen::clickPoint(context, clickX, clickY);
            Sleep(500);
            continue;
        }
        int diff = buyCount - actual;
        if (diff == 0) {
            break;
        }
        const char* btnNode = diff > 0 ? increaseBtn.c_str() : decreaseBtn.c_str();
        int steps = diff > 0 ? diff : -diff;
        if (steps > 30) {
            steps = 30;
        }
        for (int i = 0; i < steps; ++i) {
            findAndClickButton(context, btnNode);
            Sleep(200);
        }
        Sleep(300);
    }

    if (!canBuyCheck.empty()) {
        for (int attempt = 0; attempt < maxRetry; ++attempt) {
            if (isStopping(context)) {
                return true;
            }
            if (runTaskSuccess(context, canBuyCheck.c_str())) {
                return true;
            }
            MaaRect curBox;
            if (!findNodeBox(context, sliderNode.c_str(), curBox)) {
                curBox = endBox;
            }
            int cur = 0;
            if (readQuantityAtHandle(context, curBox, qOffset, cur) && cur <= 1) {
                return true;
            }
            findAndClickButton(context, decreaseBtn.c_str());
            Sleep(300);
        }
    }
    return true;
}

namespace {

MaaBool assistantThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                       const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return ShopModule::assistant(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool finishCheckThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                         const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                         void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return ShopModule::finishCheck(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool itemNotBoughtThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                           const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                           void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return ShopModule::itemNotBought(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool carriageShopSlideBuyThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return ShopModule::carriageShopSlideBuy(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void ShopModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "ShopAssistant", assistantThunk, userData);
    registerCustomRecognition(res, "ShopFinishCheck", finishCheckThunk, userData);
    registerCustomRecognition(res, "ItemNotBought", itemNotBoughtThunk, userData);
    registerCustomAction(res, "CarriageShopSlideBuy", carriageShopSlideBuyThunk, userData);
}
