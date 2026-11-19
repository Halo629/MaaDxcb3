#pragma once

#include "MaaFramework/MaaAPI.h"

#include <string>
#include <unordered_set>
#include <vector>

class RecipeModule
{
public:
    static MaaBool recordLockedRecipe(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool cookAllUnlockedRecipes(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customActionName, const char* customActionParam,
                                          MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool cookSpecifiedFood(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customActionName, const char* customActionParam,
                                     MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

private:
    static std::unordered_set<std::string> lockedRecipes_;
    static std::vector<std::string> lockedRecipeOrder_;
    static std::unordered_set<std::string> cookedThisSession_;
};
