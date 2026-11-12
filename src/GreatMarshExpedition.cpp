#include "Comm.h"

namespace
{
// ──── 数据模型 ────────────────────────────────────────────────────
// 顺序固定，与 pipeline 节点命名 {Color}{Type}{Pos|Count} 保持一致。
constexpr int kTypeCount = 6;
constexpr int kColorCount = 4; // 灰/蓝/紫/金，从低到高

constexpr int kGray = 0;
constexpr int kBlue = 1;
constexpr int kPurple = 2;
constexpr int kGold = 3;

// 合成目标：15 个金色；3 个低级合成 1 个高级。
constexpr int kTargetGold = 15;
constexpr int kCombineRatio = 3;

// ──── 全局变量 ────────────────────────────────────────────────────
// 祈愿页（已装备）与背包（未装备）分开记录，便于判断需要卸下哪些祝福。
std::vector<std::vector<int>> g_blessingPray(kTypeCount, std::vector<int>(kColorCount, 0));
std::vector<std::vector<int>> g_blessingBag(kTypeCount, std::vector<int>(kColorCount, 0));

// 装备决策结果，供后续 task 读取。
struct EquipPlan
{
    int unload[kTypeCount][kColorCount] = {}; // 需要从祈愿页卸下的数量
    int equip[kTypeCount][kColorCount] = {};  // 需要装备的数量
};
EquipPlan g_equipPlan;

// 当前目标祝福类型（枭兽=0 / 月痕=1 ...），由 TargetBlessing 动作设置，其余自定义统一读取。
int g_targetType = -1;

// 本次要装备的祝福类型/颜色，由 Equip 动作设置，TargetBlessingSelected 识别读取。
int g_equipTargetType = -1;
int g_equipTargetColor = -1;
// 装备优先级顺序（按加成降序），Equip 决策时生成，Advance 时用于找下一个。
std::vector<std::pair<int, int>> g_equipOrder;

// 清空所有全局状态（init 时调用）。
void resetGlobalState()
{
    g_blessingPray.assign(kTypeCount, std::vector<int>(kColorCount, 0));
    g_blessingBag.assign(kTypeCount, std::vector<int>(kColorCount, 0));
    g_equipPlan = EquipPlan {};
}

constexpr const char* kTypeNamesCn[kTypeCount] = { "枭兽", "月痕", "石肤", "橡木", "沙果", "绿藤" };
constexpr const char* kColorNamesCn[kColorCount] = { "灰色", "蓝色", "紫色", "金色" };

// 每个祝福的力量/魔力加成 [type][color]（灰/蓝/紫/金），用于装备排序。
// 枭兽（力量系）看力量加成，月痕（魔力系）看魔力加成。
constexpr int kStrengthBonus[kTypeCount][kColorCount] = {
    // 灰    蓝    紫    金
    { 3000, 3000, 3000, 12000 }, // 枭兽
    { 0,    0,    0,    0     }, // 月痕
    { 0,    9000, 9000, 9000  }, // 石肤
    { 0,    0,    0,    8100  }, // 橡木
    { 0,    0,    9000, 9000  }, // 沙果
    { 8000, 8000, 8000, 8000  }, // 绿藤
};
constexpr int kMagicBonus[kTypeCount][kColorCount] = {
    // 灰    蓝    紫    金
    { 0,    0,    0,    0     }, // 枭兽
    { 3000, 3000, 3000, 12000 }, // 月痕
    { 0,    0,    9000, 9000  }, // 石肤
    { 0,    0,    8100, 8100  }, // 橡木
    { 0,    9000, 9000, 9000  }, // 沙果
    { 0,    0,    8000, 8000  }, // 绿藤
};

// 生命加成 [type][color]，作为力量/魔力相同时的第二排序键。
constexpr int kHpBonus[kTypeCount][kColorCount] = {
    // 灰    蓝    紫    金
    { 0,    0,    6600, 6600 }, // 枭兽
    { 0,    0,    6600, 6600 }, // 月痕
    { 8000, 8000, 8000, 8000 }, // 石肤
    { 0,    0,    0,    0    }, // 橡木
    { 8000, 8000, 8000, 8000 }, // 沙果
    { 0,    3600, 3600, 9600 }, // 绿藤
};

// 属性条数 [type][color]，作为力量/魔力、生命都相同时的第三排序键。
constexpr int kAttrCount[kTypeCount][kColorCount] = {
    // 灰 蓝 紫 金
    { 1, 2, 3, 3 }, // 枭兽
    { 1, 2, 3, 3 }, // 月痕
    { 1, 2, 3, 4 }, // 石肤
    { 1, 2, 3, 4 }, // 橡木
    { 1, 2, 3, 4 }, // 沙果
    { 1, 2, 3, 3 }, // 绿藤
};

// 祈愿页装备槽数量。
constexpr int kEquipSlots = 15;

// 背包页：首屏识别到的种类数达到该值时，列表下方可能还有祝福，需上滑一次。
constexpr int kBagPageSwipeThreshold = 16;
constexpr MaaRect kBagPageArea { 8, 148, 712, 971 };

// ──── 通用小工具 ──────────────────────────────────────────────────
// 每个祝福对应的 Pos 任务名、穿戴选择页 Pos 任务名与背包 Count 任务名（与 pipeline 任务名一一对应）。
// 改名后全局搜任务名可定位到这里。
struct BlessingTaskNames
{
    const char* pos;               // 祈愿页/背包页（模板1）
    const char* posInBlessingPage; // 穿戴选择页（模板2，模板1在该页识别不到）
    const char* count;
};

constexpr BlessingTaskNames kBlessingTaskNames[kTypeCount][kColorCount] = {
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
    for (int t = 0; t < kTypeCount; ++t) {
        if (name == kTypeNamesCn[t]) {
            return t;
        }
    }
    return -1;
}

// 从 OCR 文本中查找祝福类型（包含匹配，兼容 OCR 前后多余字符）。
int findTypeInText(const std::string& text)
{
    for (int t = 0; t < kTypeCount; ++t) {
        if (text.find(kTypeNamesCn[t]) != std::string::npos) {
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

// ──── 统计 ────────────────────────────────────────────────────────
// 祈愿页：执行 xxxPos 节点，统计其内部 ColorMatch 的 filtered 个数（同一祝福可装载多个）。
// 模板可能命中不同颜色的祝福，只有 ColorMatch 通过颜色过滤后才是对应颜色的祝福。
// 每个祝福会命中多个模板变体（如 Gold/Gold2），产生重叠框，按重叠合并去重，同一祝福的多个实例各自保留。
int countPrayInstances(MaaContext* context, const MaaImageBuffer* img, const char* posTask)
{
    auto sub = getAndSubDetail(context, img, posTask, "BlessingColor");
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
int recognizeBagScreen(MaaContext* context, bool counted[kTypeCount][kColorCount])
{
    ScreenCap cap(context);
    int newlyRecognized = 0;
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            if (isTaskStopping(context)) {
                return newlyRecognized;
            }
            if (counted[t][c]) {
                continue;
            }
            int count = 0;
            if (!readNumberFromNodeImage(context, cap.img, kBlessingTaskNames[t][c].count, "BlessingCount", count)) {
                continue;
            }
            g_blessingBag[t][c] += count;
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
    postWaitFreezes(context, kBagPageArea, 500, 3000);
}

void logStatisticSummary()
{
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            LogUtils::debug(fmt("已有祝福数量统计: %1%2 祈愿页：%3，背包页：%4，总计：%5", kColorNamesCn[c], kTypeNamesCn[t], 
                g_blessingPray[t][c], g_blessingBag[t][c], g_blessingPray[t][c] + g_blessingBag[t][c]));
        }
    }
}

// 在祈愿页统计（先导航到祈愿页，累加实例数）。
void statisticPrayPage(MaaContext* context)
{
    ScreenCap cap(context);
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            if (isTaskStopping(context)) {
                return;
            }
            g_blessingPray[t][c] += countPrayInstances(context, cap.img, kBlessingTaskNames[t][c].pos);
        }
    }
    logStatisticSummary();
}

// 在背包页统计（先导航到背包页，读数量徽标，满屏上滑补扫）。
void statisticBagPage(MaaContext* context)
{
    bool counted[kTypeCount][kColorCount] = {};
    int firstScreenRecognized = recognizeBagScreen(context, counted);
    if (firstScreenRecognized >= kBagPageSwipeThreshold) {
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
    return g_blessingPray[type][color] + g_blessingBag[type][color];
}

// 某类型把所有低级祝福级联合成后，最多能得到的金色数量（含现有金色）。
int maxGoldOfType(int type)
{
    int gray = totalCount(type, kGray);
    int blue = totalCount(type, kBlue);
    int purple = totalCount(type, kPurple);
    int gold = totalCount(type, kGold);

    int totalBlue = blue + gray / kCombineRatio;
    int totalPurple = purple + totalBlue / kCombineRatio;
    return gold + totalPurple / kCombineRatio;
}

int currentGoldTotal()
{
    int sum = 0;
    for (int t = 0; t < kTypeCount; ++t) {
        sum += totalCount(t, kGold);
    }
    return sum;
}

int maxGoldTotal()
{
    int sum = 0;
    for (int t = 0; t < kTypeCount; ++t) {
        sum += maxGoldOfType(t);
    }
    return sum;
}

// 是否有任意类型的低级祝福 >= 3 个（可合成一个更高级）。
bool canCraftFromLower(int lowerLevel)
{
    for (int t = 0; t < kTypeCount; ++t) {
        if (totalCount(t, lowerLevel) >= kCombineRatio) {
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
    for (int level = kGray; level <= kPurple; ++level) {
        for (int t = 0; t < kTypeCount; ++t) {
            if (countPrayInstances(context, cap.img, kBlessingTaskNames[t][level].pos) > 0) {
                outType = t;
                outLevel = level;
                return true;
            }
        }
    }
    return false;
}

// 按 unload 列表逐个卸下祝福（先导航到祈愿页）。
void unloadBlessings(MaaContext* context, const int unload[kTypeCount][kColorCount])
{
    bool hasUnload = false;
    for (int t = 0; t < kTypeCount && !hasUnload; ++t) {
        for (int c = 0; c < kColorCount && !hasUnload; ++c) {
            hasUnload = unload[t][c] > 0;
        }
    }
    if (!hasUnload) {
        return;
    }

    runTaskAndWait(context, "SceneEnterGreatMarshExpeditionBlessingPrayPage");

    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            for (int i = 0; i < unload[t][c]; ++i) {
                if (isTaskStopping(context)) {
                    return;
                }
                unloadOneBlessing(context, kBlessingTaskNames[t][c].pos);
                g_blessingPray[t][c]--;
                g_blessingBag[t][c]++;
            }
        }
    }
}
}

// ──── 自定义动作：数量统计 ────────────────────────────────────────
// 全面统计：清零全局计数后依次扫祈愿页、背包页。
MaaBool greatMarshExpeditionBlessingStatistic(
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
MaaBool greatMarshExpeditionTargetBlessing(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = getNodeAttach(context, nodeName);
    std::string target = attach.get("target", json::value("")).as_string();
    int targetType = findTypeByName(target);
    if (targetType < 0) {
        LogUtils::error(fmt("记录目标祝福: 未知目标 %1", target));
        return false;
    }
    g_targetType = targetType;
    LogUtils::debug(fmt("目标祝福: %1", kTypeNamesCn[targetType]));
    return true;
}

// ──── 自定义动作：卸下祈愿页所有低级祝福 ─────────────────────────
// 先导航到祈愿页，循环重新识别低级（灰/蓝/紫）祝福并逐个卸下，直到没有要卸的。
// 靠 countPrayInstances 重新识别当前页，不依赖全局计数。
MaaBool greatMarshExpeditionUnloadAllLowBlessings(
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
        unloadOneBlessing(context, kBlessingTaskNames[type][level].pos);
        g_blessingPray[type][level]--;
        g_blessingBag[type][level]++;
    }
    return true;
}

// ──── 自定义动作：装备祝福 ────────────────────────────────────────
// attach.target：目标类型（"枭兽" 力量系 / "月痕" 魔力系）。
// 按目标属性（枭兽→力量加成，月痕→魔力加成）降序填满 15 个装备槽，
// 对比当前已装备的祝福，算出需要卸下与需要装备的数量。
MaaBool greatMarshExpeditionBlessingEquip(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    int targetType = g_targetType;
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
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            order.emplace_back(t, c);
        }
    }
    std::sort(order.begin(), order.end(), [useStrength](const auto& a, const auto& b) {
        int ba = useStrength ? kStrengthBonus[a.first][a.second] : kMagicBonus[a.first][a.second];
        int bb = useStrength ? kStrengthBonus[b.first][b.second] : kMagicBonus[b.first][b.second];
        if (ba != bb) {
            return ba > bb;
        }
        int ha = kHpBonus[a.first][a.second];
        int hb = kHpBonus[b.first][b.second];
        if (ha != hb) {
            return ha > hb; // 生命高优先
        }
        int na = kAttrCount[a.first][a.second];
        int nb = kAttrCount[b.first][b.second];
        if (na != nb) {
            return na > nb; // 属性条数多优先
        }
        if (a.second != b.second) {
            return a.second > b.second; // 等级高优先（兜底）
        }
        return a.first < b.first;
    });

    // 贪心填充装备槽，得到理想装备方案（以拥有总数为上限）。
    g_equipPlan = EquipPlan {};
    int ideal[kTypeCount][kColorCount] = {};
    int remaining = kEquipSlots;
    for (auto [t, c] : order) {
        int take = (std::min)(totalCount(t, c), remaining);
        ideal[t][c] = take;
        remaining -= take;
        if (remaining <= 0) {
            break;
        }
    }

    // 对比当前已装备，算出卸下与装备数量。
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            g_equipPlan.unload[t][c] = (std::max)(0, g_blessingPray[t][c] - ideal[t][c]);
            g_equipPlan.equip[t][c] = (std::max)(0, ideal[t][c] - g_blessingPray[t][c]);
        }
    }

    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            if (g_equipPlan.unload[t][c] > 0) {
                LogUtils::debug(fmt("  卸下: %1%2 %3 个", kColorNamesCn[c], kTypeNamesCn[t], g_equipPlan.unload[t][c]));
            }
            if (g_equipPlan.equip[t][c] > 0) {
                LogUtils::debug(fmt("  装备: %1%2 %3 个", kColorNamesCn[c], kTypeNamesCn[t], g_equipPlan.equip[t][c]));
            }
        }
    }

    // 先卸下不满足优先级的祝福。
    unloadBlessings(context, g_equipPlan.unload);

    // 覆盖 ClickTargetBlessing 为第一个要装备的祝福（按优先级顺序）。
    g_equipOrder = order;
    for (auto [t, c] : order) {
        if (isTaskStopping(context)) {
            return false;
        }
        if (g_equipPlan.equip[t][c] > 0) {
            overrideNodeRecognition(context, "ClickTargetBlessing", kBlessingTaskNames[t][c].posInBlessingPage);
            g_equipTargetType = t;
            g_equipTargetColor = c;
            LogUtils::debug(fmt("准备装备: %1%2", kColorNamesCn[c], kTypeNamesCn[t]));
            return true;
        }
    }

    g_equipTargetType = -1;
    g_equipTargetColor = -1;
    LogUtils::debug("装备完成: 没有需要装备的祝福");
    return true;
}

// ──── 自定义动作：祈愿结果记录 ──────────────────────────────────
// 依次尝试三个颜色的祈愿结果 task（And 节点，含对应颜色 ColorMatch + OCR），
// 命中的那个就是本次祈愿颜色；读取其中的祝福名，按颜色累加进背包计数。
static bool prayResultRecord(MaaContext* context)
{
    ScreenCap cap(context);

    static const struct
    {
        const char* task;
        int color;
    } kColorTasks[] = {
        { "GreatMarshExpeditionBlessingGray", kGray },
        { "GreatMarshExpeditionBlessingBlue", kBlue },
        { "GreatMarshExpeditionBlessingPurple", kPurple },
    };

    for (const auto& t : kColorTasks) {
        if (isTaskStopping(context)) {
            return false;
        }
        std::string name = readTextFromNode(context, cap.img, t.task, "PrayResult");
        if (name.empty()) {
            continue;
        }
        int type = findTypeInText(name);
        if (type < 0) {
            LogUtils::warn(fmt("祈愿结果: 未识别到祝福名（%1）", name));
            return false;
        }
        g_blessingBag[type][t.color] += 1;
        LogUtils::debug(fmt("祈愿获得: %1%2，当前背包 %3 个", kColorNamesCn[t.color], kTypeNamesCn[type], g_blessingBag[type][t.color]));
        return true;
    }

    LogUtils::warn("祈愿结果: 未识别到祈愿颜色");
    return false;
}

MaaBool greatMarshExpeditionPrayResult(
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
MaaBool greatMarshExpeditionBlessingCraftCheck(
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
    return canCraftFromLower(kGray) || canCraftFromLower(kBlue) || canCraftFromLower(kPurple);
}

// ──── 自定义识别：判断当前祝福能否合成任意高一级祝福 ─────────────
MaaBool greatMarshExpeditionBlessingCanCraftHigher(
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
    return canCraftFromLower(kBlue) || canCraftFromLower(kGray);
}

// ──── 自定义识别：判断当前页面是否有能合成的祝福 ─────────────
// 任意类型的低级（灰/蓝/紫）祝福数量 >= 3 即可合成，金色不统计（金色是最高级）。
MaaBool greatMarshExpeditionBlessingCanCraftOnPage(
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
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = kGray; c <= kPurple; ++c) {
            int count = 0;
            if (readNumberFromNode(context, kBlessingTaskNames[t][c].count, "BlessingCount", count) && count >= kCombineRatio) {
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
MaaBool greatMarshExpeditionBlessingNeedReshapeOnPage(
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
    int targetType = g_targetType;
    if (targetType < 0) {
        return false;
    }

    // 枭兽（力量系）看力量加成，月痕（魔力系）看魔力加成；选加成最低的非目标金色先重塑。
    bool useStrength = (targetType == 0);
    int sourceType = -1;
    int sourceBonus = 0;
    for (int t = 0; t < kTypeCount; ++t) {
        if (t == targetType) {
            continue;
        }
        // 祈愿页没有数量徽标，只有图标；背包页每种祝福占一个格子。用 xxxPos 数实例两页都适用。
        if (countPrayInstances(context, image, kBlessingTaskNames[t][kGold].pos) <= 0) {
            continue;
        }
        int bonus = useStrength ? kStrengthBonus[t][kGold] : kMagicBonus[t][kGold];
        if (sourceType < 0 || bonus < sourceBonus) {
            sourceBonus = bonus;
            sourceType = t;
        }
    }

    if (sourceType < 0) {
        return false;
    }
    overrideNodeRecognition(context, "ClickTargetBlessing", kBlessingTaskNames[sourceType][kGold].pos);
    LogUtils::debug(fmt("准备重塑: %1金色 -> %2", kTypeNamesCn[sourceType], kTypeNamesCn[targetType]));
    return true;
}

// ──── 自定义动作：在当前页确定要点击的合成祝福 ─────────────────
// 在当前页（背包）找到第一个低级祝福数量 >=3 的，覆盖 ClickTargetBlessing。
// 与 CanCraftOnPage 识别保持一致的遍历顺序，保证点到的就是当前页可合成的那个。
MaaBool greatMarshExpeditionBlessingCraftOnPage(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = kGray; c <= kPurple; ++c) {
            int count = 0;
            if (readNumberFromNode(context, kBlessingTaskNames[t][c].count, "BlessingCount", count) && count >= kCombineRatio) {
                overrideNodeRecognition(context, "ClickTargetBlessing", kBlessingTaskNames[t][c].pos);
                g_blessingBag[t][c] -= kCombineRatio;
                g_blessingBag[t][c + 1] += 1;
                LogUtils::debug(fmt("准备合成(当前页): %1%2 x3", kColorNamesCn[c], kTypeNamesCn[t]));
                return true;
            }
        }
    }
    return false;
}

// ──── 自定义识别：流程分流 ─────────────────────────────────────
// 在 next 中按「重塑 → 合成 → 祈愿」顺序依次加，命中即走对应流程。

// 1. 是否进入重塑：金色祝福总数 >= 15。
MaaBool greatMarshExpeditionBlessingNeedReshape(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return currentGoldTotal() >= kTargetGold;
}

// 2. 是否能够合成15个金色：级联合成后 >= 15。
// 已有 >= 15 个金色时无需再合成，返回 false。
MaaBool greatMarshExpeditionBlessingCanCraftGold(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    if (currentGoldTotal() >= kTargetGold) {
        return false;
    }
    return maxGoldTotal() >= kTargetGold;
}

// 3. 是否需要祈愿：还不能合成15个金色。
MaaBool greatMarshExpeditionBlessingNeedPray(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return maxGoldTotal() < kTargetGold;
}

// ──── 自定义识别：目标金色数量是否已满足 ────────────────────────
// 判断目标类型（全局 g_targetType）的金色祝福数是否 >= 15。
MaaBool greatMarshExpeditionBlessingTargetGoldEnough(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int targetType = g_targetType;
    if (targetType < 0) {
        return false;
    }
    return totalCount(targetType, kGold) >= kTargetGold;
}

// ──── 自定义识别：重塑结果是否是目标祝福 ─────────────────────
// 读取重塑结果（GreatMarshExpeditionBlessingGold 的 OCR），判断祝福类型是否等于目标类型。
MaaBool greatMarshExpeditionIsTargetBlessing(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int targetType = g_targetType;
    if (targetType < 0) {
        return false;
    }
    std::string name = readTextFromNode(context, image, "GreatMarshExpeditionBlessingGold", "ReshapeResult");
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
MaaBool greatMarshExpeditionBlessingPageCountEnough(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int count = 0;
    for (int t = 0; t < kTypeCount; ++t) {
        for (int c = 0; c < kColorCount; ++c) {
            int n = 0;
            if (readNumberFromNode(context, kBlessingTaskNames[t][c].count, "BlessingCount", n) && n > 0) {
                ++count;
                if (count >= kBagPageSwipeThreshold) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ──── 自定义识别：选择页选中的祝福是否是本次要装备的 ─────────────────────
// 读取 TargetEquipBlessingSelected 的 OCR 祝福名，判断类型是否等于本次装备目标类型。
MaaBool greatMarshExpeditionTargetBlessingSelected(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    int targetType = g_equipTargetType;
    if (targetType < 0) {
        return false;
    }
    std::string name = readTextFromNode(context, image, "TargetEquipBlessingSelected", "");
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
// 没有下一个时把 g_equipTargetType 置 -1（供 EquipDone 识别判断）。
MaaBool greatMarshExpeditionBlessingEquipAdvance(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customActionName, const char* customActionParam,
    MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    int t = g_equipTargetType;
    int c = g_equipTargetColor;
    if (t >= 0 && c >= 0 && g_equipPlan.equip[t][c] > 0) {
        g_equipPlan.equip[t][c]--;
        g_blessingBag[t][c]--;
        g_blessingPray[t][c]++;
    }

    for (auto [nt, nc] : g_equipOrder) {
        if (g_equipPlan.equip[nt][nc] > 0) {
            overrideNodeRecognition(context, "ClickTargetBlessing", kBlessingTaskNames[nt][nc].posInBlessingPage);
            g_equipTargetType = nt;
            g_equipTargetColor = nc;
            LogUtils::debug(fmt("准备装备: %1%2", kColorNamesCn[nc], kTypeNamesCn[nt]));
            return true;
        }
    }

    g_equipTargetType = -1;
    g_equipTargetColor = -1;
    return true;
}

// ──── 自定义识别：是否还有要装备的祝福 ─────────────────────
// g_equipTargetType < 0 表示已装备完，命中。
MaaBool greatMarshExpeditionBlessingEquipDone(
    MaaContext* context, MaaTaskId taskId, const char* nodeName,
    const char* customRecognitionName, const char* customRecognitionParam,
    const MaaImageBuffer* image, const MaaRect* roi,
    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return g_equipTargetType < 0;
}

void registerCustomGreatMarshExpedition(MaaResource* res, void* userData)
{
    registerCustomAction(res, "GreatMarshExpeditionBlessingStatistic", greatMarshExpeditionBlessingStatistic, userData);
    registerCustomAction(res, "TargetBlessing", greatMarshExpeditionTargetBlessing, userData);
    registerCustomAction(res, "GreatMarshExpeditionUnloadAllLowBlessings", greatMarshExpeditionUnloadAllLowBlessings, userData);
    registerCustomAction(res, "GreatMarshExpeditionBlessingEquip", greatMarshExpeditionBlessingEquip, userData);
    registerCustomAction(res, "GreatMarshExpeditionPrayResult", greatMarshExpeditionPrayResult, userData);
    registerCustomAction(res, "GreatMarshExpeditionBlessingCraftOnPage", greatMarshExpeditionBlessingCraftOnPage, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingNeedReshapeOnPage", greatMarshExpeditionBlessingNeedReshapeOnPage, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCraftCheck", greatMarshExpeditionBlessingCraftCheck, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCanCraftHigher", greatMarshExpeditionBlessingCanCraftHigher, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCanCraftOnPage", greatMarshExpeditionBlessingCanCraftOnPage, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingNeedReshape", greatMarshExpeditionBlessingNeedReshape, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingCanCraftGold", greatMarshExpeditionBlessingCanCraftGold, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingNeedPray", greatMarshExpeditionBlessingNeedPray, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingTargetGoldEnough", greatMarshExpeditionBlessingTargetGoldEnough, userData);
    registerCustomRecognition(res, "IsTargetBlessing", greatMarshExpeditionIsTargetBlessing, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingPageCountEnough", greatMarshExpeditionBlessingPageCountEnough, userData);
    registerCustomRecognition(res, "TargetBlessingSelected", greatMarshExpeditionTargetBlessingSelected, userData);
    registerCustomAction(res, "GreatMarshExpeditionBlessingEquipAdvance", greatMarshExpeditionBlessingEquipAdvance, userData);
    registerCustomRecognition(res, "GreatMarshExpeditionBlessingEquipDone", greatMarshExpeditionBlessingEquipDone, userData);
}
