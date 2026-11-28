#include "GreatMarshExpeditionModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "config/Config.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

std::vector<std::vector<int>> GreatMarshExpeditionModule::m_blessingPray(g_typeCount, std::vector<int>(g_colorCount, 0));
std::vector<std::vector<int>> GreatMarshExpeditionModule::m_blessingBag(g_typeCount, std::vector<int>(g_colorCount, 0));
EquipPlan GreatMarshExpeditionModule::m_equipPlan = {};
int GreatMarshExpeditionModule::m_targetType = -1;
int GreatMarshExpeditionModule::m_equipTargetType = -1;
int GreatMarshExpeditionModule::m_equipTargetColor = -1;
std::vector<std::pair<int, int>> GreatMarshExpeditionModule::m_equipOrder;

namespace
{
// ──── 数据模型 ────────────────────────────────────────────────────
// 顺序固定，与 pipeline 节点命名 {Color}{Type}{Pos|Count} 保持一致。
// g_typeCount / g_colorCount 在头文件定义。

constexpr int g_gray = 0;
constexpr int g_blue = 1;
constexpr int g_purple = 2;
constexpr int g_gold = 3;

// 合成目标：15 个金色；3 个低级合成 1 个高级。
constexpr int g_targetGold = 15;
constexpr int g_combineRatio = 3;

// 转发共享状态，供匿名命名空间辅助函数读写（等价原 g_xxx 全局变量）。
auto& blessingPray() { return GreatMarshExpeditionModule::blessingPray(); }
auto& blessingBag() { return GreatMarshExpeditionModule::blessingBag(); }
auto& equipPlan() { return GreatMarshExpeditionModule::equipPlan(); }

// 清空所有全局状态（init 时调用）。
void resetGlobalState()
{
    blessingPray().assign(g_typeCount, std::vector<int>(g_colorCount, 0));
    blessingBag().assign(g_typeCount, std::vector<int>(g_colorCount, 0));
    equipPlan() = EquipPlan {};
}

constexpr const char* g_typeNamesCn[g_typeCount] = { "枭兽", "月痕", "石肤", "橡木", "沙果", "绿藤" };
constexpr const char* g_colorNamesCn[g_colorCount] = { "灰色", "蓝色", "紫色", "金色" };

// 每个祝福的力量/魔力加成 [type][color]（灰/蓝/紫/金），用于装备排序。
// 枭兽（力量系）看力量加成，月痕（魔力系）看魔力加成。
constexpr int g_strengthBonus[g_typeCount][g_colorCount] = {
    // 灰    蓝    紫    金
    { 3000, 3000, 3000, 12000 }, // 枭兽
    { 0,    0,    0,    0     }, // 月痕
    { 0,    9000, 9000, 9000  }, // 石肤
    { 0,    0,    0,    8100  }, // 橡木
    { 0,    0,    9000, 9000  }, // 沙果
    { 8000, 8000, 8000, 8000  }, // 绿藤
};
constexpr int g_magicBonus[g_typeCount][g_colorCount] = {
    // 灰    蓝    紫    金
    { 0,    0,    0,    0     }, // 枭兽
    { 3000, 3000, 3000, 12000 }, // 月痕
    { 0,    0,    9000, 9000  }, // 石肤
    { 0,    0,    8100, 8100  }, // 橡木
    { 0,    9000, 9000, 9000  }, // 沙果
    { 0,    0,    8000, 8000  }, // 绿藤
};

// 生命加成 [type][color]，作为力量/魔力相同时的第二排序键。
constexpr int g_hpBonus[g_typeCount][g_colorCount] = {
    // 灰    蓝    紫    金
    { 0,    0,    6600, 6600 }, // 枭兽
    { 0,    0,    6600, 6600 }, // 月痕
    { 8000, 8000, 8000, 8000 }, // 石肤
    { 0,    0,    0,    0    }, // 橡木
    { 8000, 8000, 8000, 8000 }, // 沙果
    { 0,    3600, 3600, 9600 }, // 绿藤
};

// 属性条数 [type][color]，作为力量/魔力、生命都相同时的第三排序键。
constexpr int g_attrCount[g_typeCount][g_colorCount] = {
    // 灰 蓝 紫 金
    { 1, 2, 3, 3 }, // 枭兽
    { 1, 2, 3, 3 }, // 月痕
    { 1, 2, 3, 4 }, // 石肤
    { 1, 2, 3, 4 }, // 橡木
    { 1, 2, 3, 4 }, // 沙果
    { 1, 2, 3, 3 }, // 绿藤
};

// 祈愿页装备槽数量。
constexpr int g_equipSlots = 15;

// 背包页：首屏识别到的种类数达到该值时，列表下方可能还有祝福，需上滑一次。
constexpr int g_bagPageSwipeThreshold = 16;
constexpr MaaRect g_bagPageArea { 8, 148, 712, 971 };

// ──── 通用小工具 ──────────────────────────────────────────────────
// 每个祝福对应的 Pos 任务名、穿戴选择页 Pos 任务名与背包 Count 任务名（与 pipeline 任务名一一对应）。
// 改名后全局搜任务名可定位到这里。
struct BlessingTaskNames
{
    const char* pos;               // 祈愿页/背包页（模板1）
    const char* posInBlessingPage; // 穿戴选择页（模板2，模板1在该页识别不到）
    const char* count;
};

constexpr BlessingTaskNames g_blessingTaskNames[g_typeCount][g_colorCount] = {
    // 枭兽
    { { "GrayXiaoshouPos", "GrayXiaoshouPosInBlessingPage", "GrayXiaoshouCount" },
      { "BlueXiaoshouPos", "BlueXiaoshouPosInBlessingPage", "BlueXiaoshouCount" },
      { "PurpleXiaoshouPos", "PurpleXiaoshouPosInBlessingPage", "PurpleXiaoshouCount" },
      { "GoldXiaoshouPos", "GoldXiaoshouPosInBlessingPage", "GoldXiaoshouCount" } },
    // 月痕
    { { "GrayYuehenPos", "GrayYuehenPosInBlessingPage", "GrayYuehenCount" },
      { "BlueYuehenPos", "BlueYuehenPosInBlessingPage", "BlueYuehenCount" },
      { "PurpleYuehenPos", "PurpleYuehenPosInBlessingPage", "PurpleYuehenCount" },
      { "GoldYuehenPos", "GoldYuehenPosInBlessingPage", "GoldYuehenCount" } },
    // 石肤
    { { "GrayShifuPos", "GrayShifuPosInBlessingPage", "GrayShifuCount" },
      { "BlueShifuPos", "BlueShifuPosInBlessingPage", "BlueShifuCount" },
      { "PurpleShifuPos", "PurpleShifuPosInBlessingPage", "PurpleShifuCount" },
      { "GoldShifuPos", "GoldShifuPosInBlessingPage", "GoldShifuCount" } },
    // 橡木
    { { "GrayXiangmuPos", "GrayXiangmuPosInBlessingPage", "GrayXiangmuCount" },
      { "BlueXiangmuPos", "BlueXiangmuPosInBlessingPage", "BlueXiangmuCount" },
      { "PurpleXiangmuPos", "PurpleXiangmuPosInBlessingPage", "PurpleXiangmuCount" },
      { "GoldXiangmuPos", "GoldXiangmuPosInBlessingPage", "GoldXiangmuCount" } },
    // 沙果
    { { "GrayShaguoPos", "GrayShaguoPosInBlessingPage", "GrayShaguoCount" },
      { "BlueShaguoPos", "BlueShaguoPosInBlessingPage", "BlueShaguoCount" },
      { "PurpleShaguoPos", "PurpleShaguoPosInBlessingPage", "PurpleShaguoCount" },
      { "GoldShaguoPos", "GoldShaguoPosInBlessingPage", "GoldShaguoCount" } },
    // 绿藤
    { { "GrayLvtengPos", "GrayLvtengPosInBlessingPage", "GrayLvtengCount" },
      { "BlueLvtengPos", "BlueLvtengPosInBlessingPage", "BlueLvtengCount" },
      { "PurpleLvtengPos", "PurpleLvtengPosInBlessingPage", "PurpleLvtengCount" },
      { "GoldLvtengPos", "GoldLvtengPosInBlessingPage", "GoldLvtengCount" } },
};

int findTypeByName(const std::string& name)
{
    for (int t = 0; t < g_typeCount; ++t) {
        if (name == g_typeNamesCn[t]) {
            return t;
        }
    }
    return -1;
}

// 从 OCR 文本中查找祝福类型（包含匹配，兼容 OCR 前后多余字符）。
int findTypeInText(const std::string& text)
{
    for (int t = 0; t < g_typeCount; ++t) {
        if (text.find(g_typeNamesCn[t]) != std::string::npos) {
            return t;
        }
    }
    return -1;
}

// 任务是否正在停止（循环中提前退出用）。
bool isTaskStopping(MaaContext* context)
{
    return MaaTaskerStopping(MaaContextGetTasker(context));
}

// 运行一个 pipeline 任务并等待其完成。
void runTaskAndWait(MaaContext* context, const std::string& task)
{
    auto* tasker = MaaContextGetTasker(context);
    auto taskId = MaaContextRunTask(context, task.c_str(), "{}");
    if (taskId != MaaInvalidId) {
        MaaTaskerWait(tasker, taskId);
    }
}

// ──── 统计 ────────────────────────────────────────────────────────
// 祈愿页：执行 xxxPos 节点，统计其内部 ColorMatch 的 filtered 个数（同一祝福可装载多个）。
// 模板可能命中不同颜色的祝福，只有 ColorMatch 通过颜色过滤后才是对应颜色的祝福。
// 每个祝福会命中多个模板变体（如 Gold/Gold2），产生重叠框，按重叠合并去重，同一祝福的多个实例各自保留。
int countPrayInstances(MaaContext* context, const MaaImageBuffer* img, const char* posTask)
{
    auto sub = recog::getAndSubDetail(context, img, posTask, "BlessingColor");
    if (!sub.is_object()) {
        return 0;
    }
    auto& filtered = sub["filtered"];
    if (!filtered.is_array()) {
        return 0;
    }

    std::vector<MaaRect> boxes;
    for (auto& item : filtered.as_array()) {
        if (!item.is_object()) {
            continue;
        }
        auto& b = item["box"];
        if (!b.is_array() || b.as_array().size() < 4) {
            continue;
        }
        MaaRect r {
            static_cast<int>(b.as_array()[0].as_integer()),
            static_cast<int>(b.as_array()[1].as_integer()),
            static_cast<int>(b.as_array()[2].as_integer()),
            static_cast<int>(b.as_array()[3].as_integer()),
        };
        if (r.width <= 0 || r.height <= 0) {
            continue;
        }
        bool dup = false;
        for (const auto& e : boxes) {
            int left = (std::max)(e.x, r.x);
            int top = (std::max)(e.y, r.y);
            int right = (std::min)(e.x + e.width, r.x + r.width);
            int bottom = (std::min)(e.y + e.height, r.y + r.height);
            if (right <= left || bottom <= top) {
                continue;
            }
            int inter = (right - left) * (bottom - top);
            int minArea = (std::min)(e.width * e.height, r.width * r.height);
            if (minArea > 0 && inter * 2 >= minArea) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            boxes.push_back(r);
        }
    }
    return static_cast<int>(boxes.size());
}

// 背包页：统计一整屏，累加进背包计数；返回本屏新识别到的种类数。
// 已识别过的（counted 为 true）直接跳过，避免滑动后重复统计（一种祝福只占一个格子）。
// 整屏只截一次图，复用同一张 image，避免每个祝福各截一次。
int recognizeBagScreen(MaaContext* context, bool counted[g_typeCount][g_colorCount])
{
    ScreenCap cap(context);
    int newlyRecognized = 0;
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            if (isTaskStopping(context)) {
                return newlyRecognized;
            }
            if (counted[t][c]) {
                continue;
            }
            int count = 0;
            if (!recog::readNumberImage(context, cap.img, g_blessingTaskNames[t][c].count, "BlessingCount", count)) {
                continue;
            }
            blessingBag()[t][c] += count;
            counted[t][c] = true;
            ++newlyRecognized;
        }
    }
    return newlyRecognized;
}

// 背包页上滑一次，露出下方祝福。
void swipeBagUp(MaaContext* context)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostSwipe(ctrl, 350, 800, 350, 250, 200);
    MaaControllerWait(ctrl, id);
    screen::waitFreezes(context, g_bagPageArea, 500, 3000);
}

void logStatisticSummary()
{
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            LogUtils::debug(fmt("已有祝福数量统计: %1%2 祈愿页：%3，背包页：%4，总计：%5", g_colorNamesCn[c], g_typeNamesCn[t],
                blessingPray()[t][c], blessingBag()[t][c], blessingPray()[t][c] + blessingBag()[t][c]));
        }
    }
}

// 在祈愿页统计（先导航到祈愿页，累加实例数）。
void statisticPrayPage(MaaContext* context)
{
    ScreenCap cap(context);
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            if (isTaskStopping(context)) {
                return;
            }
            blessingPray()[t][c] += countPrayInstances(context, cap.img, g_blessingTaskNames[t][c].pos);
        }
    }
    logStatisticSummary();
}

// 在背包页统计（先导航到背包页，读数量徽标，满屏上滑补扫）。
void statisticBagPage(MaaContext* context)
{
    bool counted[g_typeCount][g_colorCount] = {};
    int firstScreenRecognized = recognizeBagScreen(context, counted);
    if (firstScreenRecognized >= g_bagPageSwipeThreshold) {
        swipeBagUp(context);
        recognizeBagScreen(context, counted);
    }
    logStatisticSummary();
}

// 全面重新统计：清零后依次扫祈愿页、背包页，得到最新计数。
void doFullStatistic(MaaContext* context)
{
    resetGlobalState();
    runTaskAndWait(context, "SceneEnterGreatMarshExpeditionBlessingPrayPage");
    statisticPrayPage(context);
    runTaskAndWait(context, "SceneEnterGreatMarshExpeditionBlessingBagPage");
    statisticBagPage(context);
}

// ──── 合成数学 ────────────────────────────────────────────────────
int totalCount(int type, int color)
{
    return blessingPray()[type][color] + blessingBag()[type][color];
}

// 某类型把所有低级祝福级联合成后，最多能得到的金色数量（含现有金色）。
int maxGoldOfType(int type)
{
    int gray = totalCount(type, g_gray);
    int blue = totalCount(type, g_blue);
    int purple = totalCount(type, g_purple);
    int gold = totalCount(type, g_gold);

    int totalBlue = blue + gray / g_combineRatio;
    int totalPurple = purple + totalBlue / g_combineRatio;
    return gold + totalPurple / g_combineRatio;
}

int currentGoldTotal()
{
    int sum = 0;
    for (int t = 0; t < g_typeCount; ++t) {
        sum += totalCount(t, g_gold);
    }
    return sum;
}

int maxGoldTotal()
{
    int sum = 0;
    for (int t = 0; t < g_typeCount; ++t) {
        sum += maxGoldOfType(t);
    }
    return sum;
}

// 是否有任意类型的低级祝福 >= 3 个（可合成一个更高级）。
bool canCraftFromLower(int lowerLevel)
{
    for (int t = 0; t < g_typeCount; ++t) {
        if (totalCount(t, lowerLevel) >= g_combineRatio) {
            return true;
        }
    }
    return false;
}

// ──── 卸下祝福 ────────────────────────────────────────────────────
// 覆盖指定节点的识别为「And + all_of=[posTask]」，用于点击指定祝福。
void overrideNodeRecognition(MaaContext* context, const char* nodeName, const char* posTask)
{
    json::value overrideVal = json::object {
        { std::string(nodeName), json::object {
            { "recognition", "And" },
            { "all_of", json::array { std::string(posTask) } },
        } },
    };
    if (!MaaContextOverridePipeline(context, overrideVal.to_string().c_str())) {
        LogUtils::error(fmt("覆盖识别失败: %1 -> %2", nodeName, posTask));
    }
}

// 在祈愿页卸下一个指定祝福：点目标祝福打开详情，再点卸下按钮。
void unloadOneBlessing(MaaContext* context, const char* posTask)
{
    overrideNodeRecognition(context, "ClickTargetBlessing", posTask);
    runTaskAndWait(context, "UnloadBlessingStart");
}

// 重新识别祈愿页，找第一个低级（灰/蓝/紫）祝福；找到返回 true。
bool findFirstLowBlessing(MaaContext* context, int& outType, int& outLevel)
{
    ScreenCap cap(context);
    for (int level = g_gray; level <= g_purple; ++level) {
        for (int t = 0; t < g_typeCount; ++t) {
            if (countPrayInstances(context, cap.img, g_blessingTaskNames[t][level].pos) > 0) {
                outType = t;
                outLevel = level;
                return true;
            }
        }
    }
    return false;
}

// 按 unload 列表逐个卸下祝福（先导航到祈愿页）。
void unloadBlessings(MaaContext* context, const int unload[g_typeCount][g_colorCount])
{
    bool hasUnload = false;
    for (int t = 0; t < g_typeCount && !hasUnload; ++t) {
        for (int c = 0; c < g_colorCount && !hasUnload; ++c) {
            hasUnload = unload[t][c] > 0;
        }
    }
    if (!hasUnload) {
        return;
    }

    runTaskAndWait(context, "SceneEnterGreatMarshExpeditionBlessingPrayPage");

    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            for (int i = 0; i < unload[t][c]; ++i) {
                if (isTaskStopping(context)) {
                    return;
                }
                unloadOneBlessing(context, g_blessingTaskNames[t][c].pos);
                blessingPray()[t][c]--;
                blessingBag()[t][c]++;
            }
        }
    }
}

// ──── 祈愿结果记录 ──────────────────────────────────────────────
// 依次尝试三个颜色的祈愿结果 task（And 节点，含对应颜色 ColorMatch + OCR），
// 命中的那个就是本次祈愿颜色；读取其中的祝福名，按颜色累加进背包计数。
bool prayResultRecord(MaaContext* context)
{
    ScreenCap cap(context);

    static const struct
    {
        const char* task;
        int color;
    } g_colorTasks[] = {
        { "GreatMarshExpeditionBlessingGray", g_gray },
        { "GreatMarshExpeditionBlessingBlue", g_blue },
        { "GreatMarshExpeditionBlessingPurple", g_purple },
    };

    for (const auto& t : g_colorTasks) {
        if (isTaskStopping(context)) {
            return false;
        }
        std::string name = recog::readText(context, cap.img, t.task, "PrayResult");
        if (name.empty()) {
            continue;
        }
        int type = findTypeInText(name);
        if (type < 0) {
            LogUtils::warn(fmt("祈愿结果: 未识别到祝福名（%1）", name));
            return false;
        }
        blessingBag()[type][t.color] += 1;
        LogUtils::debug(fmt("祈愿获得: %1%2，当前背包 %3 个", g_colorNamesCn[t.color], g_typeNamesCn[type], blessingBag()[type][t.color]));
        return true;
    }

    LogUtils::warn("祈愿结果: 未识别到祈愿颜色");
    return false;
}
} // namespace

// ──── 自定义动作：数量统计 ────────────────────────────────────────
// 全面统计：清零全局计数后依次扫祈愿页、背包页。
MaaBool GreatMarshExpeditionModule::blessingStatistic(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    doFullStatistic(context);
    return true;
}

// ──── 自定义动作：记录目标祝福 ─────────────────────────────────
// attach.target：目标类型（枭兽/月痕）。写入全局变量，其余自定义统一读取，不再传参。
MaaBool GreatMarshExpeditionModule::targetBlessing(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = Config::instance().nodeAttach(context, nodeName);
    std::string target = attach.get("target", json::value("")).as_string();
    int targetType = findTypeByName(target);
    if (targetType < 0) {
        LogUtils::error(fmt("记录目标祝福: 未知目标 %1", target));
        return false;
    }
    m_targetType = targetType;
    LogUtils::debug(fmt("目标祝福: %1", g_typeNamesCn[targetType]));
    return true;
}

// ──── 自定义动作：卸下祈愿页所有低级祝福 ─────────────────────────
// 先导航到祈愿页，循环重新识别低级（灰/蓝/紫）祝福并逐个卸下，直到没有要卸的。
// 靠 countPrayInstances 重新识别当前页，不依赖全局计数。
MaaBool GreatMarshExpeditionModule::unloadAllLowBlessings(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    runTaskAndWait(context, "SceneEnterGreatMarshExpeditionBlessingPrayPage");

    int type = -1;
    int level = -1;
    while (findFirstLowBlessing(context, type, level)) {
        if (isTaskStopping(context)) {
            return false;
        }
        unloadOneBlessing(context, g_blessingTaskNames[type][level].pos);
        m_blessingPray[type][level]--;
        m_blessingBag[type][level]++;
    }
    return true;
}

// ──── 自定义动作：装备祝福 ────────────────────────────────────────
// attach.target：目标类型（"枭兽" 力量系 / "月痕" 魔力系）。
// 按目标属性（枭兽→力量加成，月痕→魔力加成）降序填满 15 个装备槽，
// 对比当前已装备的祝福，算出需要卸下与需要装备的数量。
MaaBool GreatMarshExpeditionModule::blessingEquip(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    int targetType = m_targetType;
    if (targetType < 0) {
        LogUtils::error("祝福装备: 目标祝福未设置");
        return false;
    }

    // 装备决策前全面重新统计，保证计数与实际一致（前面合成/重塑可能已改变祝福分布）。
    doFullStatistic(context);

    // 枭兽（力量系）按力量加成排序，其余（月痕等魔力系）按魔力加成排序。
    bool useStrength = (targetType == 0);

    // 生成所有祝福，按加成降序排序（同加成时等级高优先）。
    std::vector<std::pair<int, int>> order;
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            order.emplace_back(t, c);
        }
    }
    std::sort(order.begin(), order.end(), [useStrength](const auto& a, const auto& b) {
        int ba = useStrength ? g_strengthBonus[a.first][a.second] : g_magicBonus[a.first][a.second];
        int bb = useStrength ? g_strengthBonus[b.first][b.second] : g_magicBonus[b.first][b.second];
        if (ba != bb) {
            return ba > bb;
        }
        int ha = g_hpBonus[a.first][a.second];
        int hb = g_hpBonus[b.first][b.second];
        if (ha != hb) {
            return ha > hb; // 生命高优先
        }
        int na = g_attrCount[a.first][a.second];
        int nb = g_attrCount[b.first][b.second];
        if (na != nb) {
            return na > nb; // 属性条数多优先
        }
        if (a.second != b.second) {
            return a.second > b.second; // 等级高优先（兜底）
        }
        return a.first < b.first;
    });

    // 贪心填充装备槽，得到理想装备方案（以拥有总数为上限）。
    m_equipPlan = EquipPlan {};
    int ideal[g_typeCount][g_colorCount] = {};
    int remaining = g_equipSlots;
    for (auto [t, c] : order) {
        int take = (std::min)(totalCount(t, c), remaining);
        ideal[t][c] = take;
        remaining -= take;
        if (remaining <= 0) {
            break;
        }
    }

    // 对比当前已装备，算出卸下与装备数量。
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            m_equipPlan.unload[t][c] = (std::max)(0, m_blessingPray[t][c] - ideal[t][c]);
            m_equipPlan.equip[t][c] = (std::max)(0, ideal[t][c] - m_blessingPray[t][c]);
        }
    }

    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            if (m_equipPlan.unload[t][c] > 0) {
                LogUtils::debug(fmt("  卸下: %1%2 %3 个", g_colorNamesCn[c], g_typeNamesCn[t], m_equipPlan.unload[t][c]));
            }
            if (m_equipPlan.equip[t][c] > 0) {
                LogUtils::debug(fmt("  装备: %1%2 %3 个", g_colorNamesCn[c], g_typeNamesCn[t], m_equipPlan.equip[t][c]));
            }
        }
    }

    // 先卸下不满足优先级的祝福。
    unloadBlessings(context, m_equipPlan.unload);

    // 覆盖 ClickTargetBlessing 为第一个要装备的祝福（按优先级顺序）。
    m_equipOrder = order;
    for (auto [t, c] : order) {
        if (isTaskStopping(context)) {
            return false;
        }
        if (m_equipPlan.equip[t][c] > 0) {
            overrideNodeRecognition(context, "ClickTargetBlessing", g_blessingTaskNames[t][c].posInBlessingPage);
            m_equipTargetType = t;
            m_equipTargetColor = c;
            LogUtils::debug(fmt("准备装备: %1%2", g_colorNamesCn[c], g_typeNamesCn[t]));
            return true;
        }
    }

    m_equipTargetType = -1;
    m_equipTargetColor = -1;
    LogUtils::debug("装备完成: 没有需要装备的祝福");
    return true;
}

// ──── 自定义动作：祈愿结果记录 ──────────────────────────────────
MaaBool GreatMarshExpeditionModule::prayResult(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    return prayResultRecord(context);
}

// ──── 自定义识别：判断能否合成精良/史诗祝福 ─────────────────────
// 任意类型有 >=3 个灰色（可合成精良/蓝）或 >=3 个蓝色（可合成史诗/紫）即命中。
MaaBool GreatMarshExpeditionModule::blessingCraftCheck(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    return canCraftFromLower(g_gray) || canCraftFromLower(g_blue) || canCraftFromLower(g_purple);
}

// ──── 自定义识别：判断当前祝福能否合成任意高一级祝福 ─────────────
MaaBool GreatMarshExpeditionModule::blessingCanCraftHigher(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    return canCraftFromLower(g_blue) || canCraftFromLower(g_gray);
}

// ──── 自定义识别：判断当前页面是否有能合成的祝福 ─────────────
// 任意类型的低级（灰/蓝/紫）祝福数量 >= 3 即可合成，金色不统计（金色是最高级）。
MaaBool GreatMarshExpeditionModule::blessingCanCraftOnPage(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = g_gray; c <= g_purple; ++c) {
            int count = 0;
            if (recog::readNumber(context, g_blessingTaskNames[t][c].count, "BlessingCount", count) && count >= g_combineRatio) {
                return true;
            }
        }
    }
    return false;
}

// ──── 自定义识别：判断当前页是否需要重塑 ─────────────────────
// custom_recognition_param.target：目标类型（枭兽/月痕）。
// 重新识别当前页，若存在非目标类型的金色祝福，则选择加成最低（与目标相差最大）的那个，
// 覆盖 ClickTargetBlessing 后命中。
MaaBool GreatMarshExpeditionModule::blessingNeedReshapeOnPage(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    int targetType = m_targetType;
    if (targetType < 0) {
        return false;
    }

    // 枭兽（力量系）看力量加成，月痕（魔力系）看魔力加成；选加成最低的非目标金色先重塑。
    bool useStrength = (targetType == 0);
    int sourceType = -1;
    int sourceBonus = 0;
    for (int t = 0; t < g_typeCount; ++t) {
        if (t == targetType) {
            continue;
        }
        // 祈愿页没有数量徽标，只有图标；背包页每种祝福占一个格子。用 xxxPos 数实例两页都适用。
        if (countPrayInstances(context, image, g_blessingTaskNames[t][g_gold].pos) <= 0) {
            continue;
        }
        int bonus = useStrength ? g_strengthBonus[t][g_gold] : g_magicBonus[t][g_gold];
        if (sourceType < 0 || bonus < sourceBonus) {
            sourceBonus = bonus;
            sourceType = t;
        }
    }

    if (sourceType < 0) {
        return false;
    }
    overrideNodeRecognition(context, "ClickTargetBlessing", g_blessingTaskNames[sourceType][g_gold].pos);
    LogUtils::debug(fmt("准备重塑: %1金色 -> %2", g_typeNamesCn[sourceType], g_typeNamesCn[targetType]));
    return true;
}

// ──── 自定义动作：在当前页确定要点击的合成祝福 ─────────────────
// 在当前页（背包）找到第一个低级祝福数量 >=3 的，覆盖 ClickTargetBlessing。
// 与 CanCraftOnPage 识别保持一致的遍历顺序，保证点到的就是当前页可合成的那个。
MaaBool GreatMarshExpeditionModule::blessingCraftOnPage(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = g_gray; c <= g_purple; ++c) {
            int count = 0;
            if (recog::readNumber(context, g_blessingTaskNames[t][c].count, "BlessingCount", count) && count >= g_combineRatio) {
                overrideNodeRecognition(context, "ClickTargetBlessing", g_blessingTaskNames[t][c].pos);
                m_blessingBag[t][c] -= g_combineRatio;
                m_blessingBag[t][c + 1] += 1;
                LogUtils::debug(fmt("准备合成(当前页): %1%2 x3", g_colorNamesCn[c], g_typeNamesCn[t]));
                return true;
            }
        }
    }
    return false;
}

// ──── 自定义识别：流程分流 ─────────────────────────────────────
// 在 next 中按「重塑 → 合成 → 祈愿」顺序依次加，命中即走对应流程。

// 1. 是否进入重塑：金色祝福总数 >= 15。
MaaBool GreatMarshExpeditionModule::blessingNeedReshape(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return currentGoldTotal() >= g_targetGold;
}

// 2. 是否能够合成15个金色：级联合成后 >= 15。
// 已有 >= 15 个金色时无需再合成，返回 false。
MaaBool GreatMarshExpeditionModule::blessingCanCraftGold(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    if (currentGoldTotal() >= g_targetGold) {
        return false;
    }
    return maxGoldTotal() >= g_targetGold;
}

// 3. 是否需要祈愿：还不能合成15个金色。
MaaBool GreatMarshExpeditionModule::blessingNeedPray(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return maxGoldTotal() < g_targetGold;
}

// ──── 自定义识别：目标金色数量是否已满足 ────────────────────────
// 判断目标类型（全局 m_targetType）的金色祝福数是否 >= 15。
MaaBool GreatMarshExpeditionModule::blessingTargetGoldEnough(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int targetType = m_targetType;
    if (targetType < 0) {
        return false;
    }
    return totalCount(targetType, g_gold) >= g_targetGold;
}

// ──── 自定义识别：重塑结果是否是目标祝福 ─────────────────────
// 读取重塑结果（GreatMarshExpeditionBlessingGold 的 OCR），判断祝福类型是否等于目标类型。
MaaBool GreatMarshExpeditionModule::isTargetBlessing(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int targetType = m_targetType;
    if (targetType < 0) {
        return false;
    }
    std::string name = recog::readText(context, image, "GreatMarshExpeditionBlessingGold", "ReshapeResult");
    if (name.empty()) {
        return false;
    }
    int type = findTypeInText(name);
    if (type < 0) {
        return false;
    }
    return type == targetType;
}

// ──── 自定义识别：当前页祝福数量是否 >= 16 ─────────────────
// 用于上滑前判断：当前页（背包）祝福种类数 >= 16 才上滑，避免下方没内容可滑。
MaaBool GreatMarshExpeditionModule::blessingPageCountEnough(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int count = 0;
    for (int t = 0; t < g_typeCount; ++t) {
        for (int c = 0; c < g_colorCount; ++c) {
            int n = 0;
            if (recog::readNumber(context, g_blessingTaskNames[t][c].count, "BlessingCount", n) && n > 0) {
                ++count;
                if (count >= g_bagPageSwipeThreshold) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ──── 自定义识别：选择页选中的祝福是否是本次要装备的 ─────────────────────
// 读取 TargetEquipBlessingSelected 的 OCR 祝福名，判断类型是否等于本次装备目标类型。
MaaBool GreatMarshExpeditionModule::targetBlessingSelected(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int targetType = m_equipTargetType;
    if (targetType < 0) {
        return false;
    }
    std::string name = recog::readText(context, image, "TargetEquipBlessingSelected", "");
    if (name.empty()) {
        return false;
    }
    int type = findTypeInText(name);
    if (type < 0) {
        return false;
    }
    return type == targetType;
}

// ──── 自定义动作：装备下一个祝福 ─────────────────────────────────
// 每次装备后调用：同步刚装备的祝福、递减计划、找下一个要装备的祝福并覆盖 ClickTargetBlessing。
// 没有下一个时把 m_equipTargetType 置 -1（供 EquipDone 识别判断）。
MaaBool GreatMarshExpeditionModule::blessingEquipAdvance(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customActionName, const char* customActionParam,
    MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    int t = m_equipTargetType;
    int c = m_equipTargetColor;
    if (t >= 0 && c >= 0 && m_equipPlan.equip[t][c] > 0) {
        m_equipPlan.equip[t][c]--;
        m_blessingBag[t][c]--;
        m_blessingPray[t][c]++;
    }

    for (auto [nt, nc] : m_equipOrder) {
        if (m_equipPlan.equip[nt][nc] > 0) {
            overrideNodeRecognition(context, "ClickTargetBlessing", g_blessingTaskNames[nt][nc].posInBlessingPage);
            m_equipTargetType = nt;
            m_equipTargetColor = nc;
            LogUtils::debug(fmt("准备装备: %1%2", g_colorNamesCn[nc], g_typeNamesCn[nt]));
            return true;
        }
    }

    m_equipTargetType = -1;
    m_equipTargetColor = -1;
    return true;
}

// ──── 自定义识别：是否还有要装备的祝福 ─────────────────────
// m_equipTargetType < 0 表示已装备完，命中。
MaaBool GreatMarshExpeditionModule::blessingEquipDone(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return m_equipTargetType < 0;
}

namespace {

MaaBool blessingStatisticThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                               const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GreatMarshExpeditionModule::blessingStatistic(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool targetBlessingThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                            const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GreatMarshExpeditionModule::targetBlessing(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool unloadAllLowBlessingsThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                   const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GreatMarshExpeditionModule::unloadAllLowBlessings(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool blessingEquipThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                           const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GreatMarshExpeditionModule::blessingEquip(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool prayResultThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GreatMarshExpeditionModule::prayResult(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool blessingCraftOnPageThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                 const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GreatMarshExpeditionModule::blessingCraftOnPage(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool blessingEquipAdvanceThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return GreatMarshExpeditionModule::blessingEquipAdvance(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool blessingNeedReshapeOnPageThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                       const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                       void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingNeedReshapeOnPage(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingCraftCheckThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingCraftCheck(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingCanCraftHigherThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                    const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingCanCraftHigher(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingCanCraftOnPageThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                    const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingCanCraftOnPage(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingNeedReshapeThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                 const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                 void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingNeedReshape(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingCanCraftGoldThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                  void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingCanCraftGold(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingNeedPrayThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                              const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                              void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingNeedPray(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingTargetGoldEnoughThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                      const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                      void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingTargetGoldEnough(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool isTargetBlessingThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                              const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                              void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::isTargetBlessing(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingPageCountEnoughThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                     const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                     void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingPageCountEnough(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool targetBlessingSelectedThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                    const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::targetBlessingSelected(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool blessingEquipDoneThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                               const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                               void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return GreatMarshExpeditionModule::blessingEquipDone(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void GreatMarshExpeditionModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "GreatMarshExpeditionBlessingStatistic", blessingStatisticThunk, userData);
    registerCustomAction(res, "TargetBlessing", targetBlessingThunk, userData);
    registerCustomAction(res, "GreatMarshExpeditionUnloadAllLowBlessings", unloadAllLowBlessingsThunk, userData);
    registerCustomAction(res, "GreatMarshExpeditionBlessingEquip", blessingEquipThunk, userData);
    registerCustomAction(res, "GreatMarshExpeditionPrayResult", prayResultThunk, userData);
    registerCustomAction(res, "GreatMarshExpeditionBlessingCraftOnPage", blessingCraftOnPageThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingNeedReshapeOnPage", blessingNeedReshapeOnPageThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCraftCheck", blessingCraftCheckThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCanCraftHigher", blessingCanCraftHigherThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCanCraftOnPage", blessingCanCraftOnPageThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingNeedReshape", blessingNeedReshapeThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCanCraftGold", blessingCanCraftGoldThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingNeedPray", blessingNeedPrayThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingTargetGoldEnough", blessingTargetGoldEnoughThunk, userData);
    registerCustomRecognition(res, "IsTargetBlessing", isTargetBlessingThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingPageCountEnough", blessingPageCountEnoughThunk, userData);
    registerCustomRecognition(res, "TargetBlessingSelected", targetBlessingSelectedThunk, userData);
    registerCustomAction(res, "GreatMarshExpeditionBlessingEquipAdvance", blessingEquipAdvanceThunk, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingEquipDone", blessingEquipDoneThunk, userData);
}
