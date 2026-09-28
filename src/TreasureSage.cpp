#include "Comm.h"

namespace
{
// 可提交的食物名称（用 | 分隔）
const char* kFoodNames =
    "黏糊糊的食物|面包|混合蔬菜浓汤|果汁|腌菜|炸鱼|蘑菇浓汤|炖肉|虾饼|腌制鱼肉香肠|鱼汤|野菜沙拉|鸟肉串|肉丸子|风干香肠|饼干|蒸虾|肉酱|鱼肉沙拉|水果布丁|发酵饮品|炒野菜|水果酿|荧果肉酱|炸鸡|胡萝卜面包|胡萝卜汁|水果馅饼|鸟肉馅饼|牛奶面包|煎肉排|乳酪馅饼|鲜虾野菜汤|烤鸟肉沙拉|海鲜浓汤|鱼肉馅饼|海陆烤肉拼盘|乱炖";

// 可提交的物品名称（现在只有银币，后续可继续加）
const char* kItemNames = "银币";

const std::unordered_set<std::string>& submitNames()
{
    static const std::unordered_set<std::string> s = [] {
        std::unordered_set<std::string> result;
        auto add = [&result](const char* raw) {
            std::string cur;
            for (const char* p = raw; *p; ++p) {
                if (*p == '|') {
                    if (!cur.empty()) {
                        result.insert(cur);
                    }
                    cur.clear();
                }
                else {
                    cur += *p;
                }
            }
            if (!cur.empty()) {
                result.insert(cur);
            }
        };
        add(kFoodNames);
        add(kItemNames);
        return result;
    }();
    return s;
}
} // namespace

// ──── TreasureSage 供奉提交 ────
// 网格 4 列 × 3 行：
//   名字 ROI     (100 + col*135, 425 + row*255, 125, 35)
//   数量颜色 ROI (160 + col*135, 400 + row*255, 55, 30)  —— 只匹配白色（有物品）
//   提交按钮 ROI (135 + col*135, 505 + row*250, 52, 35)
// 识别到名字在「食物 + 物品」列表中，就点击对应提交按钮。
MaaBool TreasureSageSubmit(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto trim = [](const std::string& s) {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) {
            return std::string { };
        }
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };

    ScreenCap cap(context);
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    const auto& names = submitNames();

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            int nameX = 100 + col * 135, nameY = 425 + row * 255;
            int countX = 160 + col * 135, countY = 400 + row * 255;
            int btnX = 135 + col * 135, btnY = 505 + row * 250;

            // 1. 数量颜色：只匹配白色（有物品），非白色跳过
            char colorJson[128];
            snprintf(
                colorJson,
                sizeof(colorJson),
                R"({"TreasureSageItemCountColor":{"roi":[%d,%d,%d,%d]}})",
                countX, countY, 55, 30);
            if (!doRecognition(context, cap.img, "TreasureSageItemCountColor", colorJson, nullptr, nullptr)) {
                continue;
            }

            // 2. 名字
            char nameJson[128];
            snprintf(
                nameJson,
                sizeof(nameJson),
                R"({"TreasureSageItemName":{"roi":[%d,%d,%d,%d]}})",
                nameX, nameY, 125, 35);
            std::string name = trim(ocrText(context, cap.img, "TreasureSageItemName", nameJson, nullptr));
            if (name.empty()) {
                continue;
            }

            // 3. 在提交列表中
            if (names.count(name) == 0) {
                continue;
            }

            // 4. 提交按钮：OCR 识别「提交」
            char btnJson[128];
            snprintf(
                btnJson,
                sizeof(btnJson),
                R"({"TreasureSageSubmitButton":{"roi":[%d,%d,%d,%d]}})",
                btnX, btnY, 52, 35);
            if (!doRecognition(context, cap.img, "TreasureSageSubmitButton", btnJson, nullptr, nullptr)) {
                continue;
            }

            // 5. 点击提交
            LogUtils::log(fmt("TreasureSage: 提交「%1」", name), "#22c55e");
            auto id = MaaControllerPostClick(ctrl, btnX + 52 / 2, btnY + 35 / 2);
            MaaControllerWait(ctrl, id);
            Sleep(500);
        }
    }
    return true;
}

void registerCustomTreasureSage(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "TreasureSageSubmit", TreasureSageSubmit, user_data);
}
