# Serial Manager 项目记忆

本文件为 AI Agent 提供项目上下文，确保代码修改、编译和调试的一致性。

## 编译

- **工作目录**: `../../../`（build.sh 所在目录）
- **编译必须使用子agent**（Task工具，subagent_type=general），不可在主会话直接执行

### 命令

```bash
# 完整编译（首次/改了 bundle.json 或 BUILD.gn 时必须用），默认同时编译正式目标+测试目标
./build.sh --product-name rk3568 --ccache --build-target serial_manager serial_manager_tests

# 快速编译（仅改了 .cpp/.h/.idl 源文件时可用，跳过GN阶段，快50-70%）
./build.sh --product-name rk3568 --ccache --fast-rebuild --build-target serial_manager serial_manager_tests
```

### 何时用完整编译

修改了以下文件必须完整编译：`bundle.json`、任何 `BUILD.gn`、新增/删除源文件、首次编译。

### 编译产物

| 产物 | 说明 |
|------|------|
| `libserialsrv_client.z.so` | InnerAPI 客户端库 |
| `libserialservice.z.so` | SA 服务库 |
| `libserial.z.so` | NAPI 绑定库，安装路径 `module/busmanager` |

## 项目定位

OpenHarmony 串口设备管理服务组件（SA 4202），参考 USB Manager 架构。子系统 `busmanager`，部件 `serial_manager`。

当前服务端为**模拟实现**（硬编码3个设备 + 环回回显），未接入真实硬件。

## 架构四层

```
JS 应用 → NAPI (libserial.z.so) → InnerAPI Client (libserialsrv_client.z.so) → IPC/Binder → SA Service (libserialservice.z.so)
```

## 关键文件速查

| 文件 | 职责 |
|------|------|
| `interfaces/innerkits/ISerialServer.idl` | 主接口定义（12个方法），修改后需同步 service/client 的 override 签名 |
| `interfaces/innerkits/ISerialServiceCallback.idl` | 数据回调 `[oneway]`，服务端→客户端推送 |
| `interfaces/innerkits/SerialServerTypes.idl` | `SerialPortInfo`(portName, productId, vendorId, manufacturer) + `SerialConfig` 数据结构 |
| `interfaces/innerkits/native/src/serial_srv_client.cpp` | 客户端单例，SA 4202 连接管理 + 死亡通知重连 |
| `interfaces/innerkits/native/src/serial_service_callback.cpp` | 监听器管理，`onDataRead` 分发到多个 JS 回调 |
| `interfaces/kits/js/napi/src/serial_info.cpp` | NAPI 完整实现（~1395行），所有 JS API + SerialPort 对象 |
| `interfaces/kits/js/@ohos.busManager.serial.d.ts` | ArkTS API 声明 |
| `services/native/src/serial_service.cpp` | SA 服务实现，模拟设备列表 + 环回读线程 |
| `services/native/include/serial_service.h` | `SerialService` 类 + `ReadContext` 结构体 |
| `sa_profile/4202.json` | SA 配置，run-on-create=true，auto-restart=true |
| `services/serial_service.cfg` | Init 进程配置，uid=serial，ondemand |
| `bundle.json` | 组件定义，syscap `SystemCapability.USB.SerialManager` |
| `serialmgr.gni` | 全局路径变量 `serial_manager_path` |

## IDL 代码生成

编译时 IDL 工具链自动生成 stub/proxy/types 到 `out/rk3568/gen/base/busmanager/serial_manager/interfaces/innerkits/`。**不应手动编辑生成文件**。

修改 IDL 后步骤：
1. 用 `--fast-rebuild` 编译
2. 检查生成文件中的虚函数签名
3. 同步更新 `serial_service.h/cpp` 和 `serial_srv_client.h/cpp` 的 override 方法签名

## 服务端模拟行为

| 方法 | 行为 |
|------|------|
| `QuerySerialDevices` | 返回3个硬编码设备：`/dev/ttyS0`、`/dev/ttyS1`、`/dev/ttyUSB0` |
| `OpenSerial` | 创建 `ReadContext`，存储回调 |
| `WriteSerial` | 记录数据到 `lastWrittenData`，返回 `data.size()`，无真实I/O |
| `ReadThreadFunc` | 每5秒检查 `lastWrittenData`，非空则通过 `onDataRead` 回显（环回模拟） |
| `Flush`/`Drain`/`SendBrkSignal`/`SetRtsSignal`/`AddPortAuthorization` | 空操作，返回0 |
| `GetCtsSignal` | 固定返回 `cts=false` |

## 客户端关键模式

- `SerialSrvClient::GetInstance()` — 单例，首次调用懒加载 SA 4202（4秒超时）
- `SerialServiceCallback` — `map<portName, map<handle, callback>>` 监听器多路复用
- 首个 listener 注册 → `StartRead`；最后一个移除 → `StopRead`
- 死亡通知：`SerialSrvDeathRecipient` → `ResetProxy()` → 下次调用自动重连

## NAPI 关键模式

- 所有变更操作用 `napi_create_async_work`（Worker 执行 + JS 线程 Complete）
- `getSerialPortList` 支持 Callback 和 Promise 双模式
- `SerialPort` JS 对象通过 `napi_wrap` 绑定 `SerialPortNative*`，支持缓存复用/失效/恢复/GC 回收
- `onDataRead` 注册时创建 C++ lambda，通过 `napi_async_work` 调度到 JS 线程

### 错误码规范

**类型**：错误码必须用 `napi_create_int32` 创建（`int32_t`），**禁止**用 `napi_create_string_utf8`。
JS 侧通过 `BusinessError.code`（`number` 类型）获取。

**封装函数**：
```cpp
// 创建 BusinessError 对象（返回 napi_value，用于 reject/赋值）
static napi_value CreateBusinessError(napi_env env, int32_t code, const char *message);

// 创建并抛出 BusinessError（用于同步方法立即中断）
static void ThrowBusinessError(napi_env env, int32_t code, const char *message);
```

**错误码定义**（来自 `.d.ts`）：

| 错误码 | 含义 | 使用场景 |
|--------|------|---------|
| 35700001 | Service error | 服务端调用失败（通用） |
| 35700002 | Invalid parameter | 参数校验失败 |
| 35700003 | Device disconnected or info changed | 设备不存在/已断开/对象已失效 |
| 35700004 | Device busy | 设备已被占用 |
| 35700005 | Device not opened | 设备未打开 |
| 35700006 | Timeout | 操作超时 |
| 35700007 | Permission denied | 缺少用户授权 |
| 35700008 | Not allowed | 非串口授权弹窗应用调用 |

**新增错误码时**：同步更新 `@ohos.busManager.serial.d.ts` 的 `@throws` 声明。

## JS API 速查

```typescript
import serial from '@ohos.busManager.serial';

serial.getSerialPortList()                    // Promise<SerialPort[]>
serial.addPortAuthorization(tokenId, deviceId) // Promise<void> @systemapi

port.open(config?)   // config 默认: baudRate=115200, dataBits=8, stopBits=1, parity=0
port.close()
port.write(data, timeout?)  // Promise<number>
port.onDataRead((data: Uint8Array) => {})
port.offDataRead([callback])
port.flush() / port.drain() / port.sendBrk()
port.setRts(enable)  // Promise<void>
port.getCts()        // Promise<boolean>

serial.DataBits { FIVE=5, SIX=6, SEVEN=7, EIGHT=8 }
serial.StopBits { ONE=1, TWO=2 }
serial.Parity { NONE='none', EVEN='even', ODD='odd', MARK='mark', SPACE='space' }
```

## 日志

三模块独立域：
- `SERIAL_HILOGI/E/W/D(MODULE_INNERKIT, ...)` — 域 `0xD002A08`，tag `SerialSrvClient`
- `SERIAL_HILOGI/E/W/D(MODULE_SERVICE, ...)` — 域 `0xD002A08`，tag `SerialService`
- `SERIAL_HILOGI/E/W/D(MODULE_JS_NAPI, ...)` — 域 `0xD002A08`，tag `SerialNapi`

## 扩展新接口流程

1. `ISerialServer.idl` 添加方法声明
2. `--fast-rebuild` 编译生成 stub/proxy
3. `serial_service.h/cpp` 添加 override 实现
4. `serial_srv_client.h/cpp` 添加客户端代理方法
5. `serial_info.cpp` 添加 NAPI 绑定 + 导出
6. `@ohos.busManager.serial.d.ts` 更新类型声明

## 测试构建

### 目录结构

```
test/
├── BUILD.gn                           # group("serial_manager_tests") 汇总所有测试目标
└── develop/
    ├── BUILD.gn                       # serialservice_errcodetest 编译目标
    └── serial_service_errcodetest.cpp # 错误码测试服务，独立编译（不依赖正式 serial_service.cpp）
```

### 关键目标

| 目标 | 路径 | 说明 |
|------|------|------|
| `serial_manager_tests` | `test:serial_manager_tests` | 汇总所有测试子目标的 group，已注册到 `bundle.json` 的 `test` 字段 |
| `serialservice_errcodetest` | `test/develop:serialservice_errcodetest` | 错误码测试服务变体，编译独立的 `serial_service_errcodetest.cpp` |

### 编译命令

```bash
# 编译全部测试目标
./build.sh --product-name rk3568 --ccache --build-target serial_manager_tests

# 仅编译错误码测试服务
./build.sh --product-name rk3568 --ccache --build-target serialservice_errcodetest
```

### 错误码测试（serialservice_errcodetest）

- **独立源文件** `test/develop/serial_service_errcodetest.cpp`，不依赖 `#ifdef` 宏分支
- 包含与正式 `serial_service.cpp` 相同的 SA 框架代码（构造/析构/OnStart/OnStop）和读线程逻辑（ReadThreadFunc），仅 12 个 IDL 方法使用 `TestScenario` 按轮次返回预设错误码
- `install_enable = false`，不会随系统安装
- 编译时抑制 `-Wno-unused-variable` / `-Wno-unused-function`（测试代码存在未使用的场景字段）
- 覆盖所有错误码场景：`SERIAL_OK`、`SERIAL_ERR_SERVICE`、`SERIAL_ERR_INVALID_PARAM`、`SERIAL_ERR_DEVICE_DISCONNECTED`、`SERIAL_ERR_DEVICE_BUSY`、`SERIAL_ERR_DEVICE_NOT_OPENED`、`SERIAL_ERR_TIMEOUT`、`SERIAL_ERR_PERMISSION_DENIED`、`SERIAL_ERR_NOT_ALLOWED`、未知错误码

### 新增测试目标流程

1. 在 `test/` 下创建子目录（如 `test/fuzz/`、`test/unittest/`）
2. 在子目录中编写 `BUILD.gn`，定义测试目标
3. 在 `test/BUILD.gn` 的 `group("serial_manager_tests")` 的 `deps` 中追加新目标
4. `bundle.json` 无需修改（已指向 `test:serial_manager_tests`）

### 设计原则

- **不在正式目标中通过宏或 GN 参数控制测试行为**：`SERIAL_ERROR_CODE_TEST` 宏已移除，测试变体通过独立源文件实现
- **test/BUILD.gn 只做汇总**：不定义编译目标，仅通过 `group` 聚合子目录的测试目标
- **每个子目录职责单一**：如 `develop/` 放开发调试用目标，`fuzz/` 放模糊测试，`unittest/` 放单元测试

## 代码格式化

- **C/C++**: `clang-format -i <file>`，配置文件位于项目根目录 `.clang-format`，所有 C/C++ 代码提交前必须格式化
- **BUILD.gn**: `gn format <file>`，修改 BUILD.gn 后必须格式化，工具路径 `prebuilts/build-tools/linux-x86/bin/gn`
- **bundle.json**: 使用 `python3 -c "import json; ... json.dump(data, f, indent=2)"` 格式化

## 代码风格

**K&R 风格**（Kernighan & Ritchie），C/C++ 代码必须遵守：

- **花括号**：开括号 `{` 不换行（同行），闭合括号 `}` 独占一行
- **缩进**：4 空格
- **控制语句**：`if`/`else`/`for`/`while`/`switch` 的 `{` 紧跟在条件/关键字同一行
- **单行 if**：即使只有一条语句也必须加花括号
- **函数定义**：返回类型与函数名在同一行，`{` 不换行
- **else/catch**：`} else {` 紧凑写法，不换行

```cpp
if (condition) {
    doSomething();
} else {
    doOther();
}

void MyClass::MyMethod(int arg) {
    for (int i = 0; i < count; i++) {
        process(i);
    }
}
```

## 编码规范

### 禁止魔鬼数字

代码中禁止使用未命名的字面量数字（魔鬼数字）。所有数字常量必须定义为 `constexpr` 命名常量，并放在合理的词法作用域中：

```cpp
// 正确 — 命名常量
constexpr uint8_t ASCII_PRINTABLE_MIN = 32;
constexpr uint8_t ASCII_PRINTABLE_MAX = 126;
constexpr int HEX_WIDTH = 2;

if (data[i] >= ASCII_PRINTABLE_MIN && data[i] <= ASCII_PRINTABLE_MAX) { ... }

// 错误 — 魔鬼数字
if (data[i] >= 32 && data[i] <= 126) { ... }
```

例外：语义完全清晰且无歧义的数字可以保留，如 `0`（零值初始化）、`1`（单步递增）、`std::setfill('0')` 中的字符参数等。

## 常见陷阱

1. **改了 BUILD.gn 用 --fast-rebuild** → "Duplicate definition" 错误，改用完整编译
2. **改了 IDL 未同步签名** → override 不匹配编译错误，先检查生成文件签名
3. **onDataRead 不触发** → 确认已 open + write，模拟模式5秒后才回显
4. **delayed_sp_singleton.h 未使用** — 客户端用的是 static 局部变量单例，非此模板
5. **RECYCLE(GC) 不触发** — `g_activePorts` 持有强引用(ref=1)阻止 GC，必须先 INVALIDATE(ref→0)后 GC 才能回收

## ArkTS 语法约束

编写 `.ets` 测试代码或应用层代码时必须遵守：

### 禁止使用的语法

| 禁止 | 替代方案 |
|------|---------|
| `as` 类型断言（如 `e as BusinessError`） | 直接声明类型：`let err: BusinessError = e` |
| `null` 赋值给非可选类型变量 | 利用函数作用域结束自动释放引用 |
| 模板字符串 `` `...${expr}...` `` | 字符串拼接 `'...' + expr + '...'` |

### 异常处理

- NAPI 抛出的错误类型为 `BusinessError`（来自 `@ohos.base`）
- catch 中用 `let err: BusinessError = e` 接收，通过 `err.code` 获取错误码（`number` 类型）
- 导入：`import { BusinessError } from '@ohos.base'`

### 引用释放与 GC

- ArkTS 不支持手动赋 `null` 释放引用，需将对象放在独立函数作用域中，函数返回后自动释放
- GC 是异步非确定性的，可通过分配大数组制造内存压力尝试触发
- `g_activePorts` 中对象的 `napi_ref` ref_count=1 阻止 GC，必须 INVALIDATE(ref→0)后才能回收

## ArkTS 测试用例编写限制（@ohos/hypium）

### 必须静态声明 it() 块

**hypium 框架不支持 for 循环动态注册 `it()` 测试用例。** 动态注册在运行时会报 "No Any Test Case Found In The Suite" 错误，所有用例都不会被发现和执行。

```typescript
// 错误 — 运行时 "No Any Test Case Found"
for (let i = 0; i < scenarios.length; i++) {
  it('case_' + i, 0, async () => { ... });
}

// 正确 — 逐个静态声明
it('case_0_All_OK', 0, async () => { await runScenario(SCENARIOS[0]); });
it('case_1_SERVICE', 0, async () => { await runScenario(SCENARIOS[1]); });
// ... 每个场景一个 it()
```

**推荐做法**：将公共逻辑提取到 `async function runScenario(sc: Scenario)` 辅助函数中，每个静态 `it()` 块仅一行调用。

### Promise 类型必须精确

```typescript
// 错误 — Promise<Object> 太宽泛
function checkResult(promise: Promise<Object>): void

// 正确 — 明确具体类型
function checkResult(promise: Promise<void>): void
function checkResult(promise: Promise<number>): void
function checkResult(promise: Promise<boolean>): void
```

### getSerialPortList() 返回类型

`serial.getSerialPortList()` 返回 `serial.SerialPort[]`（具有 `.open()`/`.close()` 等方法的对象），**不是** `serial.SerialPortInfo[]`。

### 测试文件结构模板

```typescript
import serial from '@ohos.busManager.serial';
import { BusinessError } from '@ohos.base';
import { describe, beforeAll, beforeEach, afterEach, afterAll, it, expect } from '@ohos/hypium';
import { hilog } from '@kit.PerformanceAnalysisKit';

// 辅助函数
async function checkVoid(promise: Promise<void>, expected: number, label: string): Promise<void> {
  try {
    await promise;
    expect(expected).assertEqual(0);
  } catch (e) {
    let err: BusinessError = e;
    expect(err.code).assertEqual(expected);
  }
}

// 场景执行器（提取公共逻辑）
async function runScenario(sc: Scenario): Promise<void> { ... }

// 导出函数（供 List.test.ets 导入）
export default function serialXxxTest() {
  describe('SuiteName', () => {
    beforeAll(() => { })
    beforeEach(() => { })
    afterEach(() => { })
    afterAll(() => { })

    // 静态声明每个测试用例
    it('case_0', 0, async () => { await runScenario(SCENARIOS[0]); });
    it('case_1', 0, async () => { await runScenario(SCENARIOS[1]); });
    // ...
  });
}
```

### 多 suite 文件组织

在 `List.test.ets` 中导入并调用各 suite 的导出函数：

```typescript
import serialXxxTest from './SerialXxx.test';
export default function listTest() {
  serialXxxTest();
}
```
