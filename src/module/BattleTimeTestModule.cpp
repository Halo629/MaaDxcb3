#include "BattleTimeTestModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "util/Recognition.h"
#include "util/StringUtil.h"

MaaBool BattleTimeTestModule::battleTimeTestAct(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    ScreenCap cap(context);
    MaaRect timeBox;
    std::string time = recog::readTextViaEntry(context, cap.img, "BattleTimeRec", "{}", &timeBox);
    if (time.empty()) {
        LogUtils::warn("BattleTimeTest: 未识别到战斗剩余时间");
    }
    else {
        LogUtils::info(fmt("BattleTimeTest: 战斗剩余时间:[%1] ", time));
    }
    return true;
}

namespace {

MaaBool battleTimeTestActThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                               const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return BattleTimeTestModule::battleTimeTestAct(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void BattleTimeTestModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "BattleTimeTest", battleTimeTestActThunk, userData);
}
