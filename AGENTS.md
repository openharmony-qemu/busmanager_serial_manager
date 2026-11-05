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

修改了以下文件必须完整编译：`bundle.json`、任何 `BUILD.gn`、新增/删除源文件、首次编译。仅改 `.cpp`/`.h`/`.idl` 等源文件可用 `--fast-rebuild`。

### 编译产物

| 产物 | 说明 |
|------|------|
| `libserialsrv_client.z.so` | InnerAPI 客户端库 |
| `libserialservice.z.so` | SA 服务库 |
| `libserial.z.so` | NAPI 绑定库，安装路径 `module/busmanager` |
| `serial_manager_taihe_native.z.so` | Taihe 原生库（ANI 桥接 + 接口实现） |
| `serial_manager_abc.abc` | Taihe 生成 ArkTS 字节码，安装路径 `/system/framework/` |

### Taihe 编译

```bash
./build.sh --product-name rk3568 --ccache --build-target serial_manager_taihe
```

## 项目定位

OpenHarmony 串口设备管理服务组件（SA 4202），参考 USB Manager 架构。子系统 `busmanager`，部件 `serial_manager`。

服务端已对接 HDI `ISerials`，实现真实串口 I/O；权限校验、操作审计、企业策略与 SA 生命周期管理均在服务内完成。环回读线程仅在测试变体（`test/develop/`）中实现。

## Where to look

本仓无嵌套 `AGENTS.md`，子目录一律以本文件为准。动手前先说明：本次任务属于哪一类、读了哪些文档、发现了哪些约束。任务描述、日志或代码中出现 `EDM`、`RDB`、`审计`、`NAPI`、`Taihe`、`HDI` 等术语时，按下表对应行先读相关文件，不要凭猜测修改。若任务只是定位某个功能的入口或单个符号，直接用 grep 搜索，不要按表逐项遍历。

| 任务类型 | 先看 | 同时读 |
|---|---|---|
| 新增或修改对外接口 | `interfaces/innerkits/ISerialServer.idl`、`interfaces/kits/js/@ohos.busManager.serial.d.ts` | README「IDL 接口定义」「错误码」 |
| 端口打开 / 关闭 / 读写 | `services/native/src/serial_service.cpp` | `interfaces/innerkits/native/src/serial_srv_client.cpp` |
| 权限、授权弹窗、EDM、审计 | `services/native/src/serial_right_manager.cpp`、`serial_right_db_helper.cpp`、`serial_security_report.cpp` | `sa_profile/4202.json`、`services/serial_service.cfg` |
| HDI 适配与设备枚举 | `serial_service.cpp` 的 `InitHdiProxy` / `QuerySerialDevices` / `OpenHdiDevice` | `bundle.json` 中的 `drivers_interface_serial` |
| NAPI 与 Taihe 绑定 | `interfaces/kits/js/napi/src/serial_info.cpp`、`interfaces/kits/js/taihe/src/ohos.busManager.serial.impl.cpp` | 对应的 `.d.ts` 与 `.taihe` |
| 错误码 | `utils/native/include/serial_error_code.h` | `.d.ts` 中的 `@throws` |
| SA 配置与进程属性 | `sa_profile/4202.json`、`services/serial_service.cfg` | 本文件「关键文件速查」 |
| 测试 | `test/unittest/`、`test/develop/`、`test/module_test/` | 本文件「测试构建」 |

## 架构

### NAPI 路径

```
JS 应用 → NAPI (libserial.z.so) → InnerAPI Client (libserialsrv_client.z.so) → IPC/Binder → SA Service (libserialservice.z.so)
```

### Taihe 路径

```
JS 应用 → Taihe ABC (serial_manager_abc.abc) → ANI Bridge (serial_manager_taihe_native.z.so) → InnerAPI Client → IPC/Binder → SA Service
```

## 关键文件速查

| 文件 | 职责 |
|------|------|
| `interfaces/innerkits/ISerialServer.idl` | 主接口定义（14 个方法），修改后需同步 service/client 的 override 签名 |
| `interfaces/innerkits/ISerialServiceCallback.idl` | 数据回调 `[oneway]`，服务端→客户端推送 |
| `interfaces/innerkits/SerialServerTypes.idl` | `SerialPortInfo`(portName, productId, vendorId, manufacturer) + `SerialConfig` 数据结构 |
| `interfaces/innerkits/native/src/serial_srv_client.cpp` | 客户端单例，SA 4202 连接管理 + 死亡通知重连 |
| `interfaces/innerkits/native/src/serial_service_callback.cpp` | 监听器管理，`onDataRead` 分发到多个 JS 回调 |
| `interfaces/kits/js/napi/src/serial_info.cpp` | NAPI 完整实现（~2340 行），所有 JS API + SerialPort 对象 |
| `interfaces/kits/js/@ohos.busManager.serial.d.ts` | ArkTS API 声明 |
| `interfaces/kits/js/taihe/idl/ohos.busManager.serial.taihe` | Taihe IDL 定义（interface SerialPort + struct/enum） |
| `interfaces/kits/js/taihe/src/ohos.busManager.serial.impl.cpp` | Taihe C++ 实现（SerialPortHolder 类，持有端口状态） |
| `interfaces/kits/js/taihe/src/ani_constructor.cpp` | ANI 入口，注册 Taihe 模块 |
| `interfaces/kits/js/taihe/BUILD.gn` | Taihe 编译配置（IDL→ANI→共享库→ABC→安装） |
| `services/native/src/serial_service.cpp` | SA 服务实现，对接 HDI 完成设备发现、打开、读写与信号控制 |
| `services/native/include/serial_service.h` | `SerialService` 类定义 |
| `sa_profile/4202.json` | SA 配置，run-on-create=false（按需加载），auto-restart=true |
| `services/serial_service.cfg` | Init 进程配置，uid=serial，ondemand |
| `bundle.json` | 组件定义，syscap `SystemCapability.BusManager.Serial` |
| `serialmgr.gni` | 全局路径变量 `serial_manager_path` |

## IDL 代码生成

编译时 IDL 工具链自动生成 stub/proxy/types 到 `out/rk3568/gen/base/busmanager/serial_manager/interfaces/innerkits/`。**不应手动编辑生成文件**。

修改 IDL 后步骤：
1. 用 `--fast-rebuild` 编译
2. 检查生成文件中的虚函数签名
3. 同步更新 `serial_service.h/cpp` 和 `serial_srv_client.h/cpp` 的 override 方法签名

## 服务端实现要点

| 方法 | 实现 |
|------|------|
| `QuerySerialDevices` | 经 HDI `QueryDevices` 枚举设备；企业策略禁用 RS-232 时仅保留 USB 串口 |
| `OpenSerial` | 校验参数与权限，经 HDI `OpenDevice` 打开端口并注册设备回调与死亡通知 |
| `CloseSerial` | 权限校验后经 HDI `Close` 关闭端口，清理端口上下文 |
| `WriteSerial` | 经 HDI `Write` 下发数据，返回实际写入字节数 |
| `StartRead` / `StopRead` | 经 HDI 启停异步读，数据由 `OnReadData` 回调推送 |
| `Flush` / `Drain` / `SendBrkSignal` | 经 HDI 执行缓冲区清理、等待发送完成、发送断帧信号 |
| `SetRtsSignal` / `GetCtsSignal` / `SetDtrSignal` / `GetDsrSignal` | 经 HDI 读写流控与调制解调器信号 |
| `AddPortAuthorization` | 系统应用为指定应用（tokenId）授权指定设备，结果记录到权限缓存并落库 |

> **注意**：无打开设备时延时 30 秒卸载 SA；客户端进程死亡、设备离线时清理端口上下文。`test/develop/` 下的测试变体自行维护环回读线程，正式服务不含该逻辑。

## 客户端关键模式

- `SerialSrvClient::GetInstance()` — 单例，首次调用懒加载 SA 4202（4秒超时）
- `SerialServiceCallback` — `map<portName, map<handle, callback>>` 监听器多路复用
- 首个 listener 注册 → `StartRead`；最后一个移除 → `StopRead`
- 死亡通知：`SerialSrvDeathRecipient` → `ResetProxy()` → 下次调用自动重连

## NAPI 关键模式

- 所有变更操作用 `napi_create_async_work`（Worker 执行 + JS 线程 Complete）
- `getSerialPortList` 仅提供 Promise 调用（不接收回调参数）；数据经 `onDataRead` 回调推送
- `SerialPort` JS 对象通过 `napi_wrap` 绑定 `SerialPortNative*`，支持缓存复用/失效/恢复/GC 回收
- `onDataRead` 注册时创建 C++ lambda，通过 `napi_async_work` 调度到 JS 线程

## Taihe 关键模式

- `interface SerialPort` 与 d.ts 完全对齐，`@promise` 生成 Promise 方法，`@get` 映射属性
- `SerialPortHolder` 持有 `portName_/productId_/vendorId_/manufacturer_`，所有方法委托到 `SerialSrvClient`
- `GetSerialPortListSync()` 通过 `make_holder<SerialPortHolder, SerialPort>(...)` 创建接口实例
- `@gen_promise("getSerialPortList")` 生成 sync + Promise 两个版本，sync 版本在 JS 线程创建 Taihe 对象
- `onDataRead`/`offDataRead` 全局回调管理：`g_callbacks` map + mutex，首个 listener 注册 innerkit 回调
- `close()` 自动清除所有 `onDataRead` 回调
- 仅两个顶层函数（`getSerialPortListSync`/`addPortAuthorizationSync`）需要 `TH_EXPORT_CPP_API` 宏
- 类型别名避免命名冲突：`TaiheSerialPortInfo`/`TaiheSerialPort`/`TaiheSerialConfigs`

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

四模块独立域：
- `SERIAL_HILOGI/E/W/D(MODULE_INNERKIT, ...)` — 域 `0xD002A08`，tag `SerialSrvClient`
- `SERIAL_HILOGI/E/W/D(MODULE_SERVICE, ...)` — 域 `0xD002A08`，tag `SerialService`
- `SERIAL_HILOGI/E/W/D(MODULE_JS_NAPI, ...)` — 域 `0xD002A08`，tag `SerialNapi`
- `SERIAL_HILOGI/E/W/D(MODULE_TAIHE, ...)` — 域 `0xD002A08`，tag `SerialTaihe`

## 扩展新接口流程

1. `ISerialServer.idl` 添加方法声明
2. `--fast-rebuild` 编译生成 stub/proxy
3. `serial_service.h/cpp` 添加 override 实现
4. `serial_srv_client.h/cpp` 添加客户端代理方法
5. `serial_info.cpp` 添加 NAPI 绑定 + 导出
6. `ohos.busManager.serial.taihe` 添加 Taihe IDL 方法声明
7. `ohos.busManager.serial.impl.cpp` 添加 Taihe C++ 实现
8. `@ohos.busManager.serial.d.ts` 更新类型声明

## 测试构建

### 目录结构

```
test/
├── BUILD.gn                           # group("serial_manager_tests") 汇总所有测试目标
├── develop/                           # 开发调试用测试服务变体（错误码/生命周期/参数校验）
└── unittest/                          # 单元测试（客户端、回调、无服务场景）
```

### 关键目标

| 目标 | 路径 | 说明 |
|------|------|------|
| `serial_manager_tests` | `test:serial_manager_tests` | 汇总所有测试子目标的 group，已注册到 `bundle.json` 的 `test` 字段 |
| `serialservice_errcodetest` | `test/develop:serialservice_errcodetest` | 错误码测试服务变体 |
| `serialservice_lifecycle_test` | `test/develop:serialservice_lifecycle_test` | 生命周期测试服务变体 |
| `serialservice_paramtest` | `test/develop:serialservice_paramtest` | 参数校验测试服务变体 |

### 编译命令

```bash
# 编译全部测试目标
./build.sh --product-name rk3568 --ccache --build-target serial_manager_tests

# 仅编译错误码测试服务
./build.sh --product-name rk3568 --ccache --build-target serialservice_errcodetest
```

### 错误码测试（serialservice_errcodetest）

- **独立源文件** `test/develop/serial_service_errcodetest.cpp`，不依赖 `#ifdef` 宏分支
- 包含与正式 `serial_service.cpp` 相同的 SA 框架代码（构造/析构/OnStart/OnStop）和独立的读线程逻辑（本地 `ReadContext` + `ReadThreadFunc`），14 个 IDL 方法均使用 `TestScenario` 按轮次返回预设错误码
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

### 函数体行数限制

每个函数（含成员函数、lambda）的非空、非注释行数不得超过 **50 行**。超出时必须将逻辑拆分为子函数或辅助函数。

### 禁止 C 风格类型转换

代码中禁止使用 C 风格类型转换（如 `(int)x`、`(void**)&p`），必须使用 C++ 提供的类型转换操作符：

| C++ 转换 | 用途 |
|----------|------|
| `static_cast<T>(expr)` | 相关类型间转换（数值、指针上下转型） |
| `reinterpret_cast<T>(expr)` | 不相关类型间转换（如 `uint8_t**` → `void**`） |
| `const_cast<T>(expr)` | 添加/移除 `const` 或 `volatile` |

```cpp
// 错误 — C 风格
napi_get_typedarray_info(env, argv[0], nullptr, &length, (void**)&dataPtr, nullptr, nullptr);
printf("%llu", (unsigned long long)handle);

// 正确 — C++ 风格
napi_get_typedarray_info(env, argv[0], nullptr, &length, reinterpret_cast<void**>(&dataPtr), nullptr, nullptr);
printf("%llu", static_cast<unsigned long long>(handle));
```

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
3. **onDataRead 不触发** → 正式服务依赖真实 HDI 设备产生数据；无硬件时部署测试变体（lifecycle_test/errcodetest/paramtest）才有环回回显
4. **delayed_sp_singleton.h 未使用** — 客户端用的是 static 局部变量单例，非此模板
5. **RECYCLE(GC) 不触发** — `g_activePorts` 持有强引用(ref=1)阻止 GC，必须先 INVALIDATE(ref→0)后 GC 才能回收

## 禁止与请示

**Do not**

- 不得直接编辑 IDL 生成物（`out/.../gen/.../interfaces/innerkits/` 下的 `*_proxy`、`*_stub`、`*_types`）。
- 不得把「基线已具备」当作不实现需求的理由；题面要求新增或修改时，必须产出实际代码改动。
- 不得绕过 `CheckPortAccessPermission()` 的 tokenId 校验去读写端口。
- 不得跳过 `SerialSecurityReport` 的审计上报。
- 不得修改日志域 `0xD002A08` 与审计事件 ID `0x030000100` / `0x030000101`：DFX 采集与安全审计依赖固定取值。
- 不得在正式服务中引入仅测试用的宏或环境变量开关。
- 不得在单个函数内堆叠超过 50 行逻辑（见「编码规范」）。

**Ask before**

- 修改 `ISerialServer.idl` 已有方法签名、`.d.ts` 中已发布接口，或 `SerialPortInfo` / `SerialConfig` 字段（公开 API 兼容性）。
- 修改错误码数值或语义（`serial_error_code.h` 与 `.d.ts` 需同步）。
- 修改权限缓存表结构或缓存键（`serial_right_db_helper`）。
- 修改 EDM 开关参数名 `persist.edm.rs232_serial_disable`。
- 新增或升级第三方依赖，或修改 `bundle.json` 的 `deps` / `license` 字段（依赖与许可证合规）。
- 修改 SA profile 的 `run-on-create`、`auto-restart`、`start-on-demand`，或 Init 配置中的 `uid`、`secon` 标签。
- 执行可能影响真实设备的命令（直接读写 `/dev/tty*`、`dd`、批量删除）。

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

## 完成标准

**最小检查**

- 改动 `bundle.json` 或任何 `BUILD.gn`：执行完整编译，不可用 `--fast-rebuild`。
- 仅改 `.cpp` / `.h` / `.idl`：可用 `--fast-rebuild`。
- C/C++ 改动后执行「代码格式化」一节中的格式化命令；`BUILD.gn` 改动后执行 `gn format`。

**任务相关检查**

- 改 IDL：编译后核对生成物中的虚函数签名，并同步 `serial_service.*`、`serial_srv_client.*`。
- 改权限或审计：编译 `serial_manager_tests`，在设备侧运行 `SerialRightTest` 与 `SerialServiceTest`。
- 改 NAPI：编译 `serial` 目标，确认导出函数与 `.d.ts` 一致。

**Done 定义**

编译通过、相关测试通过、格式已统一，三者齐备才算完成。

**最终回复**

写明改动的文件、执行的检查、未执行的检查及残留风险。

**无法验证时**

说明哪一步没跑、原因（无设备、无构建环境等）以及由此带来的风险，不得默认「应该没问题」。
