#include "Comm.h"
#include <string>
#include <unordered_set>

namespace
{
static std::unordered_set<std::string> g_lockedRecipes;
static std::vector<std::string> g_lockedRecipeOrder;
static std::unordered_set<std::string> g_cookedThisSession;
}

// ──── 辅助 ────

// ColorMatch 黄色 + OCR 精炼食物名，失败返回空串
static std::string refineFoodName(MaaContext* context, const MaaImageBuffer* img, MaaRect box)
{
    char colorJson[256];
    snprintf(
        colorJson,
        sizeof(colorJson),
        R"({"cc":{"recognition":"ColorMatch","roi":[%d,%d,%d,%d],"method":40,)"
        R"("lower":[15,90,100],"upper":[30,100,200],"count":3}})",
        box.x,
        box.y,
        box.width,
        box.height);
    MaaRect colorBox;
    if (!doRecognition(context, img, "cc", colorJson, &colorBox, nullptr)) {
        return { };
    }

    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"refine":{"recognition":"OCR","roi":[%d,%d,%d,%d],"only_rec":true}})",
        colorBox.x - 2,
        colorBox.y - 2,
        colorBox.width + 4,
        colorBox.height + 4);
    auto buf = MaaStringBufferCreate();
    MaaRect dummy;
    std::string result;
    if (doRecognition(context, img, "refine", ocrJson, &dummy, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
        for (auto& ri : j["all"].as_array()) {
            std::string rt = ri["text"].as_string();
            if (!rt.empty()) {
                result = rt;
            }
        }
    }
    MaaStringBufferDestroy(buf);
    return result;
}

// 扫描一个 cell 的 OCR 结果，返回是否发现 ? 和是否有新食谱
static void scanCell(MaaContext* context, const MaaImageBuffer* img, const MaaRect& roi, bool& hasUnlockedRecipe, bool& hasNew)
{
    char json[256];
    snprintf(json, sizeof(json), R"({"cell":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})", roi.x, roi.y, roi.width, roi.height);

    auto buf = MaaStringBufferCreate();
    MaaRect dummy;
    if (!doRecognition(context, img, "cell", json, &dummy, buf)) {
        MaaStringBufferDestroy(buf);
        return;
    }
    std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);
    auto j = json::parse(detail).value_or(json::value { });

    for (auto& item : j["all"].as_array()) {
        std::string text = item["text"].as_string();
        if (text.empty()) {
            continue;
        }

        if (text.find('?') != std::string::npos || text.find("？") != std::string::npos) {
            hasUnlockedRecipe = true;
            return;
        }

        loadRecipeConfig();
        if (!getRecipeNames().count(text)) {
            MaaRect itemBox = {
                item["box"][0].as_integer(),
                item["box"][1].as_integer(),
                item["box"][2].as_integer(),
                item["box"][3].as_integer(),
            };
            std::string refined = refineFoodName(context, img, itemBox);
            if (!refined.empty()) {
                text = refined;
            }
        }

        if (getRecipeNames().count(text) && g_lockedRecipes.insert(text).second) {
            g_lockedRecipeOrder.push_back(text);
            hasNew = true;
        }
    }
}

// ──── RecordLockedRecipe: OCR扫描并记录已解锁食谱 ────

MaaBool recordLockedRecipe(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_lockedRecipes.clear();
    g_lockedRecipeOrder.clear();

    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));

    static const MaaRect kCellRois[] = {
        { 30, 330, 165, 210 }, { 195, 330, 165, 210 }, { 360, 330, 165, 210 }, { 525, 330, 165, 210 },
        { 30, 525, 165, 210 }, { 195, 525, 165, 210 }, { 360, 525, 165, 210 }, { 525, 525, 165, 210 },
        { 30, 720, 165, 210 }, { 195, 720, 165, 210 }, { 360, 720, 165, 210 }, { 525, 720, 165, 210 },
        { 30, 915, 165, 210 }, { 195, 915, 165, 210 }, { 360, 915, 165, 210 }, { 525, 915, 165, 210 },
    };

    for (int round = 0; round < 5; ++round) {
        ScreenCap cap(context);
        bool hasUnlockedRecipe = false, hasNew = false;

        for (auto& roi : kCellRois) {
            scanCell(context, cap.img, roi, hasUnlockedRecipe, hasNew);
            if (hasUnlockedRecipe) {
                break;
            }
        }

        if (hasUnlockedRecipe || !hasNew) {
            break;
        }

        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto swipeId = MaaControllerPostSwipe(ctrl, 350, 900, 350, 700, 100);
        MaaControllerWait(ctrl, swipeId);
        postWaitFreezes(context);
    }

    std::vector<std::string> list;
    for (auto& r : g_lockedRecipeOrder) {
        list.push_back(r);
    }
    LogUtils::log(std::string("当前已解锁食谱：") + join(list, ", "), "#22c55e");
    return true;
}

// 等三个添加食材按钮全部出现且没有烹饪背景，否则点击缺失按钮
static bool clickAddIngredientsBtn(MaaContext* context, MaaController* ctrl)
{
    static const MaaRect kAddBtnRois[] = {
        { 150, 585, 93, 83 },
        { 314, 583, 91, 83 },
        { 482, 585, 87, 79 },
    };

    for (int attempt = 0; attempt < 10; ++attempt) {
        ScreenCap cap(context);

        // 检查是否已满足条件：三个按钮全在 + 没有烹饪背景
        bool allBtns = true;
        for (auto& roi : kAddBtnRois) {
            char json[256];
            snprintf(
                json,
                sizeof(json),
                R"({"btn":{"recognition":"TemplateMatch","template":"recipe/AddFoodIngredients.png","roi":[%d,%d,%d,%d]}})",
                roi.x,
                roi.y,
                roi.width,
                roi.height);
            MaaRect dummy;
            if (!doRecognition(context, cap.img, "btn", json, &dummy, nullptr)) {
                allBtns = false;
            }
        }

        MaaRect bgBox;
        bool hasBg = doRecognition(
            context,
            cap.img,
            "bg",
            R"({"bg":{"recognition":"TemplateMatch","template":"recipe/CookingBackground.png","roi":[50,700,619,384]}})",
            &bgBox,
            nullptr);

        if (allBtns && !hasBg) {
            return true;
        }

        // 有背景说明需要重新打开食材面板，点第一个识别到的按钮
        if (hasBg) {
            for (auto& roi : kAddBtnRois) {
                char json[256];
                snprintf(
                    json,
                    sizeof(json),
                    R"({"btn":{"recognition":"TemplateMatch","template":"recipe/AddFoodIngredients.png","roi":[%d,%d,%d,%d]}})",
                    roi.x,
                    roi.y,
                    roi.width,
                    roi.height);
                MaaRect dummy;
                if (doRecognition(context, cap.img, "btn", json, &dummy, nullptr)) {
                    ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
                    auto cid = MaaControllerPostClick(ctrl, roi.x + roi.width / 2, roi.y + roi.height / 2);
                    MaaControllerWait(ctrl, cid);
                    break;
                }
            }
            postWaitFreezes(context);
            continue;
        }

        for (auto& roi : kAddBtnRois) {
            char json[256];
            snprintf(
                json,
                sizeof(json),
                R"({"btn":{"recognition":"TemplateMatch","template":"recipe/AddFoodIngredients.png","roi":[%d,%d,%d,%d]}})",
                roi.x,
                roi.y,
                roi.width,
                roi.height);
            MaaRect dummy;
            if (!doRecognition(context, cap.img, "btn", json, &dummy, nullptr)) {
                ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
                auto cid = MaaControllerPostClick(ctrl, roi.x + roi.width / 2, roi.y + roi.height / 2);
                MaaControllerWait(ctrl, cid);
            }
        }
        postWaitFreezes(context);
    }
    return false;
}

// 选择指定食材并点击确认，3页覆盖
static bool selectOneIngredient(MaaContext* context, MaaController* ctrl, const std::string& need)
{
    MaaRect ingBox;
    bool found = false;

    static const int kPageSwipes[][4] = {
        { 420, 900, 420, 760 },
        { 420, 760, 420, 900 },
    };
    for (int page = 0; page < 3 && !found; ++page) {
        if (page > 0) {
            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto swId = MaaControllerPostSwipe(
                ctrl,
                kPageSwipes[page - 1][0],
                kPageSwipes[page - 1][1],
                kPageSwipes[page - 1][2],
                kPageSwipes[page - 1][3],
                100);
            MaaControllerWait(ctrl, swId);
            Sleep(500);
        }
        ScreenCap cap(context);
        auto buf = MaaStringBufferCreate();
        MaaRect dummy;
        if (doRecognition(context, cap.img, "IngredientsOcrArea", "{}", &dummy, buf)) {
            std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
            auto j = json::parse(detail).value_or(json::value { });
            for (auto& item : j["all"].as_array()) {
                if (item["text"].as_string().find(need) != std::string::npos) {
                    ingBox.x = item["box"][0].as_integer();
                    ingBox.y = item["box"][1].as_integer();
                    ingBox.width = item["box"][2].as_integer();
                    ingBox.height = item["box"][3].as_integer();
                    found = true;
                    break;
                }
            }
        }
        MaaStringBufferDestroy(buf);
    }
    if (!found) {
        return false;
    }

    ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto cid = MaaControllerPostClick(ctrl, ingBox.x + ingBox.width / 2, ingBox.y + ingBox.height / 2 - 50);
    MaaControllerWait(ctrl, cid);
    char addedJson[256];
    snprintf(addedJson, sizeof(addedJson), R"({"added":{"recognition":"OCR","roi":[130,555,462,159],"expected":["%s"]}})", need.c_str());
    waitUntilRecognitionSuccess(context, "added", addedJson, nullptr, nullptr);
    return true;
}

// 选择食谱的全部食材，全部找到返回 true
static bool
    selectAllIngredients(MaaContext* context, MaaController* ctrl, const std::vector<std::string>& ings, const std::string& recipeName)
{
    for (auto& ing : ings) {
        std::string need = ing;
        if (!selectOneIngredient(context, ctrl, need)) {
            LogUtils::log(fmt("食谱: 未找到食材 [%1]，跳过 [%2]", need, recipeName), "#f59e0b");
            return false;
        }
    }
    return true;
}

// ──── CookAllUnlockedRecipes: 一次烹饪所有未解锁食谱 ────

MaaBool cookAllUnlockedRecipes(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    const auto& recipes = getRecipeConfig();

    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto* tasker = MaaContextGetTasker(context);
    int cooked = 0, skipped = 0;

    for (auto& [name, ings] : recipes) {
        if (MaaTaskerStopping(tasker)) {
            return false;
        }

        std::string recipeName = name;
        if (g_lockedRecipes.count(recipeName)) {
            continue;
        }

        LogUtils::log(fmt("开始解锁食谱:[%1]", recipeName), "#3b82f6");

        if (MaaTaskerStopping(tasker)) {
            return false;
        }
        if (!clickAddIngredientsBtn(context, ctrl)) {
            break;
        }

        if (MaaTaskerStopping(tasker)) {
            return false;
        }
        if (selectAllIngredients(context, ctrl, ings, recipeName)) {
            MaaContextRunTask(context, "CookingBtn", "{}");
            ++cooked;
            LogUtils::log(fmt("食谱:[%1]解锁完成", recipeName), "#22c55e");
        }
        else {
            ++skipped;
        }
    }

    LogUtils::log(fmt("解锁食谱完成: 解锁 %1 个，跳过 %2 个", cooked, skipped), "#22c55e");
    return true;
}

// ──── 辅助函数 ────

static bool findRecipeInList(MaaContext* context, MaaController* ctrl, const std::string& name, MaaRect* outBox)
{
    for (int swipe = 0; swipe < 5; ++swipe) {
        ScreenCap cap(context);
        auto buf = MaaStringBufferCreate();
        MaaRect dummy;
        if (doRecognition(context, cap.img, "RecipeOcrArea", "{}", &dummy, buf)) {
            std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
            auto j = json::parse(detail).value_or(json::value { });
            for (auto& item : j["all"].as_array()) {
                if (item["text"].as_string().find(name) != std::string::npos) {
                    outBox->x = item["box"][0].as_integer();
                    outBox->y = item["box"][1].as_integer();
                    outBox->width = item["box"][2].as_integer();
                    outBox->height = item["box"][3].as_integer();
                    MaaStringBufferDestroy(buf);
                    return true;
                }
            }
        }
        MaaStringBufferDestroy(buf);
        if (swipe < 4) {
            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto swId = MaaControllerPostSwipe(ctrl, 350, 900, 350, 700, 100);
            MaaControllerWait(ctrl, swId);
            Sleep(2000);
        }
    }
    return false;
}

static int ocrQuantity(MaaContext* context)
{
    ScreenCap cap(context);
    auto buf = MaaStringBufferCreate();
    MaaRect dummy;
    int count = 0;
    if (doRecognition(context, cap.img, "RecipeQuantityOcr", "{}", &dummy, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
        std::string text;
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }
        count = std::atoi(text.c_str());
    }
    MaaStringBufferDestroy(buf);
    return count;
}

static void adjustQuantity(MaaContext* context, int target)
{
    while (true) {
        int cur = ocrQuantity(context);
        if (cur == target) {
            break;
        }
        MaaContextRunTask(context, (cur < target) ? "RecipeQuantityPlus" : "RecipeQuantityMinus", "{}");
        Sleep(200);
    }
}

// ──── CookSpecifiedFood: 烹饪指定食物 ────
// custom_action_param: {"name":"面包","count":5}

MaaBool cookSpecifiedFood(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto param = json::parse(customActionParam).value_or(json::value { });
    std::string recipeName = param.get("name", "");
    int targetCount = param.get("count", 1);
    if (recipeName.empty()) {
        LogUtils::log("烹饪食物:未指定食物名称", "#ef4444");
        return false;
    }

    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    // 1. 在食谱页找指定食物
    MaaRect foodBox;
    if (findRecipeInList(context, ctrl, recipeName, &foodBox)) {
        // 点击食谱 → 调数量 → 烹饪
        ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto cid = MaaControllerPostClick(ctrl, foodBox.x + foodBox.width / 2, foodBox.y + foodBox.height / 2 - 50);
        MaaControllerWait(ctrl, cid);
        postWaitFreezes(context);
        adjustQuantity(context, targetCount);
        MaaContextRunTask(context, "CookingBtn", "{}");
        LogUtils::log(fmt("烹饪食物: [%1] 烹饪完成", recipeName), "#22c55e");
        return true;
    }

    // 2. 未解锁，切烹饪页解锁
    LogUtils::log(fmt("烹饪食物: [%1] 食谱未解锁,前往解锁食谱", recipeName), "#f59e0b");
    const auto& recipes = getRecipeConfig();
    auto ingIt = recipes.find(recipeName);
    if (ingIt == recipes.end()) {
        LogUtils::log(fmt("烹饪食物: 配置中未找到 [%1]", recipeName), "#ef4444");
        return false;
    }

    MaaRect tabBox;
    waitUntilRecognitionSuccess(context, "CookingTab", "{}", &tabBox, nullptr);
    ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto tabCid = MaaControllerPostClick(ctrl, tabBox.x + tabBox.width / 2, tabBox.y + tabBox.height / 2);
    MaaControllerWait(ctrl, tabCid);
    postWaitFreezes(context);

    if (!clickAddIngredientsBtn(context, ctrl)) {
        return false;
    }

    selectAllIngredients(context, ctrl, ingIt->second, recipeName);
    MaaContextRunTask(context, "CookingBtn", "{}");
    LogUtils::log(fmt("烹饪食物: [%1] 已解锁", recipeName), "#22c55e");

    if (targetCount <= 1) {
        return true;
    }

    // count > 1: 解锁算1次，用 next 重定向再跑
    json::value newParam = json::object { { "name", recipeName }, { "count", targetCount - 1 } };
    json::value overrideVal = json::object { { "CookSpecifiedFood", json::object { { "custom_action_param", newParam } } } };
    MaaContextOverridePipeline(context, overrideVal.to_string().c_str());
    setNextNode(context, nodeName, "CookingSpecifiedFoodStart");
    return true;
}

void registerCustomRecipe(MaaResource* resource, void* userData)
{
    registerCustomAction(resource, "RecordLockedRecipe", recordLockedRecipe, userData);
    registerCustomAction(resource, "CookAllUnlockedRecipes", cookAllUnlockedRecipes, userData);
    registerCustomAction(resource, "CookSpecifiedFood", cookSpecifiedFood, userData);
}
