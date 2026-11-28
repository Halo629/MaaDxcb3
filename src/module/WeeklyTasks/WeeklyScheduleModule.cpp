#include "WeeklyScheduleModule.h"

#include "Registry.h"
#include "config/Config.h"

#include <ctime>

// 通用周任务日期检查：只在该节点 attach 里勾选的星期几执行。
// 供多个周任务复用：pipeline 节点 recognition=Custom, custom_recognition=WeeklyScheduleRecognition
// 节点 attach 由任务的「执行周期」checkbox 通过 pipeline_override 写入（monday~sunday bool）。
MaaBool WeeklyScheduleModule::weeklyScheduleRecognition(
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
    auto attach = Config::instance().nodeAttach(context, nodeName);

    // 今天星期几：tm_wday 0=周日 1=周一 ... 6=周六
    std::time_t tt = std::time(nullptr);
    std::tm t { };
    localtime_s(&t, &tt);
    static const char* g_weekKeys[] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
    return attach.get(g_weekKeys[t.tm_wday], json::value(false)).as_boolean();
}

namespace {

MaaBool weeklyScheduleRecognitionThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                       const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                       void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return WeeklyScheduleModule::weeklyScheduleRecognition(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void WeeklyScheduleModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "WeeklyScheduleRecognition", weeklyScheduleRecognitionThunk, userData);
}
