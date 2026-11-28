# MaaDxcb3 整体架构

## 一、项目定位

基于 MaaFramework 的游戏自动化插件，作为 `MaaAgentServer` 子进程运行。核心职责：向框架注册**自定义识别（Recognition）**和**自定义动作（Action）**，由 pipeline 的 JSON 配置驱动，完成「截图 → 图像识别/OCR → 决策 → 点击/滑动」的自动化链路。

## 二、分层架构

```
┌─────────────────────────────────────────────────────────┐
│  注册中心 core/Registrar                                │
│  统一收集所有模块 registerAll，main 一次性调用            │
├─────────────────────────────────────────────────────────┤
│  业务模块层 module/（24 个模块类）                        │
│  Battle · Server · GreatMarshExpedition · RedeemCode …   │
├─────────────────────────────────────────────────────────┤
│  基础设施层 config/ · map/ · movement/ · core/Common     │
│  配置解析 · 地图导航 · 走位穿墙 · 通用动作                │
├─────────────────────────────────────────────────────────┤
│  工具层 util/（无状态，RAII + 命名空间）                  │
│  MaaBuffer · Recognition · ScreenUtil · OcrUtil · …      │
└─────────────────────────────────────────────────────────┘
```

依赖方向：**自上而下单向**，工具层不依赖任何业务代码。

## 三、目录结构

```
src/
├── main.cpp              # 面向过程入口（cpp-service）
├── LogUtils.h/.cpp       # 日志（双写 stdout + 文件）
├── ServerConfig.h        # 配置结构体定义
├── Registry.h            # registerCustomRecognition/Action → MaaAgentServer 重定向
│
├── util/                 # 工具层：无状态，RAII + 命名空间
│   ├── MaaBuffer.h       # MaaStringBuffer 的 RAII 包装
│   ├── JsonUtil.h        # JSON 解析（jsonutil::parse / readIntArray）
│   ├── StringUtil.h      # fmt / split / join / trim
│   ├── OcrUtil.h/.cpp    # OCR 文本与数值解析（ocr 命名空间）
│   ├── ExprParser.h      # 浮点表达式求值器
│   ├── DirectionUtil.h   # 罗盘方位 / 坐标解析
│   ├── Recognition.h/.cpp# 识别工具 + ScreenCap 截图 RAII（recog 命名空间）
│   └── ScreenUtil.h/.cpp # 可点击区域 / 滑动 / 点击（screen 命名空间）
│
├── config/               # 配置中心
│   └── Config.h/.cpp     # 节点 attach + 用户配置 + 食谱（替代裸全局变量）
│
├── map/                  # 地图导航
│   └── MapNavigator.h/.cpp
│
├── movement/             # 走位
│   └── Movement.h/.cpp   # 走位 + 穿墙 + findTargetPoint/arrivedAtTargetPoint
│
├── core/                 # 通用与注册
│   ├── TestMain.cpp      # 优化代码入口（cpp-service-test）
│   ├── CommonActions.h/.cpp  # 通用动作：SaveConfig / SubTask / AdjustPosition 等
│   └── Registrar.h/.cpp  # 注册中心
│
├── recognizer/           # 通用识别器
│   └── ExpressionRecognizer.h/.cpp  # 表达式识别（浮点数值组合成布尔表达式）
│
└── module/               # 业务模块（24 个，每个一个类）
    ├── BattleModule.h/.cpp
    ├── ServerModule.h/.cpp
    ├── RedeemCodeModule.h/.cpp
    └── ...
```

## 四、各层职责

| 层 | 目录 | 职责 |
|---|---|---|
| 工具层 | `util/` | `MaaBuffer`(RAII 缓冲)、`Recognition`(识别工具+截图)、`ScreenUtil`(可点击区域/滑动/罗盘)、`OcrUtil`(OCR 解析)、`ExprParser`(表达式求值)、`StringUtil`、`JsonUtil`、`DirectionUtil` |
| 基础设施 | `config/` `map/` `movement/` `core/` | `Config`(节点/用户配置)、`MapNavigator`(地图)、`Movement`(走位)、`CommonActions`(通用动作) |
| 模块层 | `module/` | 24 个游戏功能，每个一个类 |
| 注册中心 | `core/Registrar` | 汇总注册 |

## 五、模块类统一范式

每个业务模块严格遵循同一结构：

```cpp
class BattleModule {
public:
    static MaaBool battleAi(...);        // 识别/动作方法（camelCase）
    static void registerAll(MaaResource* res, void* userData);
private:
    static int m_consecutiveFails;       // 状态 → static 成员（m_ 前缀）
};
```

配套三件套：
1. **thunk**（匿名命名空间 C 函数，桥接 MaaFramework C 回调到类静态方法）
2. **registerAll**（注册该模块所有识别/动作）
3. **状态收敛**（原 `static` 全局变量 → 类 `static` 成员）

原 `.cpp` 里的 `static` 辅助函数 → 移到 `.cpp` 匿名命名空间。

## 六、数据流

```
pipeline JSON 配置
   ↓ 触发识别节点
自定义识别（thunk → 模块类方法）
   ↓ recog::run / screen::* / ocr::*
图像识别 / OCR / 颜色匹配
   ↓ 命中判定
自定义动作（thunk → 模块类方法）
   ↓ screen::clickPoint / swipeToward
MaaController 执行点击/滑动
```

## 七、命名规范

| 类别 | 规则 | 例子 |
|---|---|---|
| 类成员 | `m_` 前缀 | `m_codes` |
| 全局变量/数组/常量 | `g_` 前缀 | `g_screenAreaBox` |
| 类方法 | 小写 camelCase | `getServerList` |
| 注册字符串 | 保持原名 | `"GetServerListAct"` |

## 八、构建系统（双轨隔离）

```
src/*.cpp（面向过程，稳定版）   →  cpp-service.exe        （正式用）
src/子目录（优化代码）          →  cpp-service-test.exe    （-test 后缀，灰度验证）
tests/                         →  unit-tests              （单元测试）
```

两个 target 源文件互不重叠，优化版不影响稳定版，可灰度验证后再正式切换。

## 九、关键设计决策

1. **工具用命名空间而非类**：`recog`/`screen`/`ocr`/`jsonutil` 是无状态纯函数，命名空间比"全静态方法类"更直接。
2. **不引入识别/动作接口继承**：MaaFramework 的 `MaaCustomRecognitionCallback`/`MaaCustomActionCallback` 已是契约，再包一层接口是冗余。模块类以「一个游戏功能」为聚合单元。
3. **RAII 消除手动资源管理**：`MaaBuffer` 包装 `MaaStringBuffer`、`ScreenCap` 包装截图，消灭手写 create/destroy 及泄漏风险。
4. **消除重复代码**：`readIntArray`（原定义两次）、OCR 文本收集、`json::parse().value_or()` 统一收编进工具层。
5. **注册名与代码解耦**：方法名 camelCase，注册字符串保持原名（pipeline JSON 引用注册名，改了会破坏配置）。

## 十、模块清单（24 个）

| 模块类 | 原文件 | 回调 |
|---|---|---|
| BattleModule | Battle.cpp | 1 动作 |
| ServerModule | Server.cpp | 2 识别 + 2 动作 |
| ArcaneCellModule | ArcaneCell.cpp | 1 识别 + 2 动作 |
| FerdinandModule | Ferdinand.cpp | 1 动作 |
| GuildModule | Guild.cpp | 1 动作 |
| WeeklyScheduleModule | WeeklySchedule.cpp | 1 识别 |
| BattleTimeTestModule | BattleTimeTest.cpp | 1 动作 |
| CelestialIsleModule | CelestialIsle.cpp | 6 识别 + 7 动作 |
| FoodFestivalModule | FoodFestival.cpp | 1 识别 + 2 动作 |
| RecipeModule | Recipe.cpp | 3 动作 |
| MonsterSoulUpgradeModule | MonsterSoulUpgrade.cpp | 2 识别 + 3 动作 |
| HuntingGroundFeastModule | HuntingGroundFeast.cpp | 4 识别 + 5 动作 |
| GreatMarshExpeditionModule | GreatMarshExpedition.cpp | 12 识别 + 7 动作 |
| EndlessWealthIsleModule | EndlessWealthIsle.cpp | 1 动作 |
| DarkSecretTreasureModule | DarkSecretTreasure.cpp | 2 动作 |
| RuneWorkshopModule | RuneWorkshop.cpp | 2 识别 |
| ShadowMirrorModule | ShadowMirror.cpp | 1 识别 + 1 动作 |
| SoulTombModule | SoulTomb.cpp | 1 识别 + 3 动作 |
| StewardModule | Steward.cpp | 2 动作 |
| TreasureSageModule | TreasureSage.cpp | 1 动作 |
| ShopModule | Comm.cpp（拆出） | 2 识别 + 2 动作 |
| RedeemCodeModule | RedeemCode.cpp | 1 识别 + 2 动作 |
| RuneUpgradeModule | RuneUpgrade.cpp | 2 识别 |
| SilverFarmModule | SilverFarm.cpp | 3 动作 |

## 十一、构建命令

```
cmake -S . -B build
cmake --build build --config Release --target cpp-service        # 稳定版
cmake --build build --config Release --target cpp-service-test   # 优化版
cmake --build build --config Release --target unit-tests         # 单元测试
```

---

**一句话概括**：把「无状态工具、有状态基础设施、业务模块、注册」四层分离，业务模块用统一范式封装，工具层用 RAII 和命名空间消除重复，构建双轨保证稳定版与优化版互不干扰。
