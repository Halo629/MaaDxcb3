#include "Comm.h"

namespace
{
static bool g_advancedMonthlyCard = false;
}

MaaBool advancedMonthlyCardRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_advancedMonthlyCard = true;
    return true;
}

void registerCustomMonthlyCard(MaaResource* resource, void* userData)
{
    registerCustomAction(resource, "AdvancedMonthlyCardRecord", advancedMonthlyCardRecord, userData);
}
