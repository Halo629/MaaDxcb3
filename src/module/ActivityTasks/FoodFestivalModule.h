#pragma once

#include "MaaFramework/MaaAPI.h"

class FoodFestivalModule
{
public:
    static MaaBool donate(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                          const char* customActionName, const char* customActionParam,
                          MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool canDonateFood(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customRecognitionName, const char* customRecognitionParam,
                                 const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                 MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool specifyCookingFood(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);
};
