#include "Registrar.h"

#include "CommonActions.h"
#include "module/OtherTasks/ArcaneCellModule.h"
#include "module/Common/BattleModule.h"
#include "module/OtherTasks/BattleTimeTestModule.h"
#include "module/ActivityTasks/CelestialIsleModule.h"
#include "module/ActivityTasks/DarkSecretTreasureModule.h"
#include "module/DailyTasks/EndlessWealthIsleModule.h"
#include "module/ActivityTasks/FerdinandModule.h"
#include "module/ActivityTasks/FoodFestivalModule.h"
#include "module/ActivityTasks/GreatMarshExpeditionModule.h"
#include "module/ActivityTasks/GuildModule.h"
#include "module/ActivityTasks/HuntingGroundFeastModule.h"
#include "module/OtherTasks/MonsterSoulUpgradeModule.h"
#include "module/OtherTasks/RecipeModule.h"
#include "module/OtherTasks/RedeemCodeModule.h"
#include "module/OtherTasks/RuneUpgradeModule.h"
#include "module/OtherTasks/RuneWorkshopModule.h"
#include "module/OtherTasks/ServerModule.h"
#include "module/ActivityTasks/ShadowMirrorModule.h"
#include "module/DailyTasks/ShopModule.h"
#include "module/OtherTasks/SilverFarmModule.h"
#include "module/OtherTasks/SoulTombModule.h"
#include "module/DailyTasks/StewardModule.h"
#include "module/ActivityTasks/TreasureSageModule.h"
#include "module/WeeklyTasks/WeeklyScheduleModule.h"
#include "movement/Movement.h"
#include "recognizer/ExpressionRecognizer.h"

void registerAllCustomRecognition(MaaResource* res, void* userData)
{
    CommonActions::registerAll(res, userData);
    Movement::registerAll(res, userData);
    ShopModule::registerAll(res, userData);
    ExpressionRecognizer::registerAll(res, userData);

    ArcaneCellModule::registerAll(res, userData);
    BattleModule::registerAll(res, userData);
    BattleTimeTestModule::registerAll(res, userData);
    CelestialIsleModule::registerAll(res, userData);
    DarkSecretTreasureModule::registerAll(res, userData);
    EndlessWealthIsleModule::registerAll(res, userData);
    FerdinandModule::registerAll(res, userData);
    FoodFestivalModule::registerAll(res, userData);
    GreatMarshExpeditionModule::registerAll(res, userData);
    GuildModule::registerAll(res, userData);
    HuntingGroundFeastModule::registerAll(res, userData);
    MonsterSoulUpgradeModule::registerAll(res, userData);
    RecipeModule::registerAll(res, userData);
    RedeemCodeModule::registerAll(res, userData);
    RuneUpgradeModule::registerAll(res, userData);
    RuneWorkshopModule::registerAll(res, userData);
    ServerModule::registerAll(res, userData);
    ShadowMirrorModule::registerAll(res, userData);
    SilverFarmModule::registerAll(res, userData);
    SoulTombModule::registerAll(res, userData);
    StewardModule::registerAll(res, userData);
    TreasureSageModule::registerAll(res, userData);
    WeeklyScheduleModule::registerAll(res, userData);
}
