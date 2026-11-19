# 自定义识别/动作代码 —— 面向对象重构设计

> 目标：把当前散落各文件、以自由函数为主的自定义识别/动作代码，重构成**有清晰接口、有 RAII、职责分离**的面向对象结构，让代码经得起面试官阅读。
> 原则：**功能不变**，只改结构；分阶段推进，每阶段可独立验收。

---

## 一、现状诊断

| 问题 | 现状 | 影响 |
|---|---|---|
| 面向过程 | 各模块 `.cpp` 里大量 `static` 自由函数，无类封装 | 看不出设计、职责不清 |
| 巨无霸文件 | `Comm.cpp` 97KB，塞满工具函数 + 15 个注册 | 难以维护，面试观感差 |
| 裸资源管理 | `MaaStringBufferCreate/Destroy` 到处手动调用 | 泄漏风险，代码冗长 |
| JSON 解析重复 | 每个函数都手写 `json::parse(...).value_or(...)` + 字段遍历 | 大量重复代码 |
| 注册分散 | `Reco.cpp` 手写 20+ 行注册列表，易漏 | 不优雅，无统一机制 |
| 全局状态 | `g_user_battle`、`g_user_soul` 等裸全局变量 | 状态无处安放 |

---

## 二、目标架构（分四层）

```
┌─────────────────────────────────────────────────┐
│ 注册中心  RecognitionRegistry / ActionRegistry   │  统一收集并注册
├─────────────────────────────────────────────────┤
│ 接口层    ICustomRecognizer / ICustomAction      │  抽象基类
├─────────────────────────────────────────────────┤
│ 实现层    ExpressionRecognizer / BattleModule…   │  具体类
├─────────────────────────────────────────────────┤
│ 工具层    MaaBuffer / OcrHelper / JsonHelper…    │  RAII + 通用工具
└─────────────────────────────────────────────────┘
```

---

## 三、核心类设计

### 3.1 工具层：RAII 封装（最高性价比，先做）

**`MaaBuffer`** —— 消灭所有手动 `MaaStringBufferCreate/Destroy`：

```cpp
// src/util/MaaBuffer.h
#pragma once
#include "MaaFramework/MaaAPI.h"
#include <string>

// RAII 封装 MaaStringBuffer，避免手动 create/destroy 造成的泄漏
class MaaBuffer {
public:
    MaaBuffer() : buffer_(MaaStringBufferCreate()) {}
    ~MaaBuffer() { MaaStringBufferDestroy(buffer_); }

    MaaBuffer(const MaaBuffer&) = delete;
    MaaBuffer& operator=(const MaaBuffer&) = delete;

    MaaStringBuffer* get() const noexcept { return buffer_; }
    std::string str() const {
        return std::string(MaaStringBufferGet(buffer_), MaaStringBufferSize(buffer_));
    }

private:
    MaaStringBuffer* buffer_;
};
```

改造前后对比（`Battle.cpp` 里的 `readRemainingTime`）：

```cpp
// 改造前：手动 create/destroy，冗长
MaaStringBuffer* detailBuf = MaaStringBufferCreate();
if (!doRecognition(...)) { MaaStringBufferDestroy(detailBuf); return 60; }
std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
MaaStringBufferDestroy(detailBuf);

// 改造后：RAII 自动管理
MaaBuffer detailBuf;
if (!doRecognition(context, cap.img, "BattleRemainingTime", {}, nullptr, detailBuf.get())) {
    return 60;
}
std::string detail = detailBuf.str();
```

### 3.2 接口层：抽象基类

**识别接口**（对应 `MaaCustomRecognitionCallback` 签名）：

```cpp
// src/recognizer/ICustomRecognizer.h
#pragma once
#include "MaaFramework/MaaAPI.h"

class ICustomRecognizer {
public:
    virtual ~ICustomRecognizer() = default;
    virtual const char* name() const = 0;
    virtual MaaBool recognize(
        MaaContext* context, MaaTaskId taskId, const char* nodeName,
        const char* customRecognitionName, const char* customRecognitionParam,
        const MaaImageBuffer* image, const MaaRect* roi,
        void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail) = 0;
};
```

**动作接口**（对应 `MaaCustomActionCallback` 签名）：

```cpp
// src/action/ICustomAction.h
#pragma once
#include "MaaFramework/MaaAPI.h"

class ICustomAction {
public:
    virtual ~ICustomAction() = default;
    virtual const char* name() const = 0;
    virtual MaaBool run(
        MaaContext* context, MaaTaskId taskId, const char* nodeName,
        const char* customActionName, const char* customActionParam,
        MaaRecoId recoId, const MaaRect* box, void* transArg) = 0;
};
```

### 3.3 实现层：具体识别器示例

把 `ExpressionRecognition.cpp` 重构成类（保留已有的 `ExprParser`，收进类内部）：

```cpp
// src/recognizer/ExpressionRecognizer.h
#pragma once
#include "ICustomRecognizer.h"

class ExpressionRecognizer : public ICustomRecognizer {
public:
    const char* name() const override { return "ExpressionRecognition"; }

    MaaBool recognize(
        MaaContext* context, MaaTaskId taskId, const char* nodeName,
        const char* customRecognitionName, const char* customRecognitionParam,
        const MaaImageBuffer* image, const MaaRect* roi,
        void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail) override;

private:
    class ExprParser;  // 表达式求值器（原实现移入 .cpp）
    // 各步骤拆成私有方法，职责单一
    bool resolveExpressionValues(MaaContext* context, const MaaImageBuffer* image,
                                 const std::string& expression, std::string& outResolved);
    bool resolveResultBox(MaaContext* context, const MaaImageBuffer* image,
                          const char* boxNode, MaaRect& outBox);
};
```

**C 回调适配**（MaaFramework 需要 C 函数指针，用 static thunk 桥接）：

```cpp
// src/recognizer/ExpressionRecognizer.cpp
namespace {
ExpressionRecognizer g_expressionRecognizer;  // 单例实例
}

// static 桥接函数，签名匹配 MaaCustomRecognitionCallback
MaaBool ExpressionRecognizer_thunk(MaaContext* ctx, MaaTaskId tid, const char* node,
                                   const char* name, const char* param,
                                   const MaaImageBuffer* img, const MaaRect* roi,
                                   void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return g_expressionRecognizer.recognize(ctx, tid, node, name, param,
                                            img, roi, transArg, outBox, outDetail);
}
```

### 3.4 注册中心

用列表驱动替代 `Reco.cpp` 手写 20 行：

```cpp
// src/recognizer/RecognitionRegistry.h
#pragma once
#include "Registry.h"
#include "ICustomRecognizer.h"
#include <vector>

class RecognitionRegistry {
public:
    static RecognitionRegistry& instance();

    // 收集所有识别器（把 new 出来的识别器 push 进来）
    void registerRecognizer(ICustomRecognizer* recognizer);

    // 一次性注册到 MaaFramework
    void registerAll(MaaResource* res, void* userData);

private:
    std::vector<ICustomRecognizer*> recognizers_;
};
```

调用处（`main.cpp` / `init`）变为：

```cpp
auto& reg = RecognitionRegistry::instance();
reg.registerRecognizer(&g_expressionRecognizer);
reg.registerRecognizer(&g_serverRecognizer);
// ...
reg.registerAll(res, userData);
```

---

## 四、分阶段实施计划

### 阶段 1：工具层（约半天，立竿见影）
- 新建 `src/util/`：`MaaBuffer.h`、`JsonHelper.h`（封装 `json::parse().value_or()`）、`OcrHelper.h`（收编 `parseOcrNumericValue` / `normalizeNumberToken` / `extractNumberFromOcrDetail`）。
- 把 `MaaStringBuffer` 手动管理全部换成 `MaaBuffer`。
- **验收**：编译通过，行为不变。

### 阶段 2：接口层 + 注册中心（约 1 天，体现架构）
- 新建 `src/recognizer/` 与 `src/action/`，定义两个抽象基类。
- 新建 `RecognitionRegistry` / `ActionRegistry`，替代 `Reco.cpp` 手写列表。
- 先迁移 1~2 个代表性识别（`ExpressionRecognition` + 一个简单模块）作示范。
- **验收**：这 2 个识别经注册中心注册后功能正常。

### 阶段 3：模块类封装（按模块推进，每个 0.5~1 小时）
- 把每个任务模块（`Battle.cpp`、`Guild.cpp`…）里的自由函数收进 `XxxModule` 类，识别/动作变类方法。
- 拆 `Comm.cpp`：通用工具归入 `util/`，业务识别/动作归入对应模块。
- 优先级：先简单模块（`WeeklySchedule`、`Guild`、`Ferdinand`），再复杂模块（`Battle`、`CelestialIsle`、`GreatMarshExpedition`）。
- **验收**：逐模块回归测试（跑对应任务）。

---

## 五、代码规范清单（面试观感关键）

1. **RAII**：所有 `MaaStringBuffer`、`MaaImageBuffer` 等裸资源用 RAII 包装。
2. **命名**：类 `PascalCase`；方法 `camelCase`；成员 `snake_case_`（尾部下划线）；常量 `kPascalCase`。
3. **const 正确性**：只读参数标 `const`；成员函数能 `const` 就 `const`。
4. **职责单一**：一个函数只做一件事（现在的 `readRemainingTime`、`getBeanCount` 里混合了「截图 + 识别 + JSON 解析 + 字符串处理」）。
5. **错误处理**：失败返回 `false` + 记 `LogUtils`，不在中间静默吞错。
6. **避免裸全局**：`g_user_battle` 等收敛进配置类，由显式传入或单例访问。
7. **头文件**：用前向声明减少 include；include 顺序 = 自身头文件 → 第三方 → 标准库。
8. **注释**：写「为什么」不写「是什么」；关键算法/协议逻辑必须有注释。

---

## 六、待你拍板的点

1. **范围**：只重构「识别」（Recognition），还是「识别 + 动作」一起？（两者代码交织，建议一起，否则识别里调动作的边界很别扭）
2. **深度**：只做「阶段 1 + 2」拿到架构骨架即可，还是要「阶段 3」全量铺开？
3. **Comm.cpp 拆解**：它 97KB，要不要拆？（建议拆，否则面试官看到 97KB 单文件仍是减分项）

你定完这三点，我可以先帮你把「阶段 1 的 util 工具层 + 阶段 2 的接口/注册中心」代码写出来，作为重构起点。
