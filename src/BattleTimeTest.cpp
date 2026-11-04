#include "Comm.h"

// 测试战斗时间识别：执行 BattleTimeRec 识别，打印识别到的时间与识别框。
MaaBool battleTimeTestAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    ScreenCap cap(context);
    MaaRect timeBox;
    std::string time = ocrText(context, cap.img, "BattleTimeRec", "{}", &timeBox);
    if (time.empty()) {
        LogUtils::warn("BattleTimeTest: 未识别到战斗剩余时间");
    }
    else {
        LogUtils::info(fmt("BattleTimeTest: 战斗剩余时间:[%1] ", time));
    }
    return true;
}

void registerCustomBattleTimeTest(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "BattleTimeTest", battleTimeTestAct, user_data);
}
