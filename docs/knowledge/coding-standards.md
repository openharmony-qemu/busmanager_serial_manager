# 编码铁律

以下规则提炼自本仓 AGENTS.md「编码规范」「代码风格」「禁止与请示」「常见陷阱」与实际代码（路径均相对仓根）。数值与命名以仓内实现为准。

## 公开接口与 IPC 契约

- **禁止**直接编辑 IDL 生成物（`out/<product>/gen/base/busmanager/serial_manager/interfaces/innerkits/` 下的 `*_proxy.*`、`*_stub.*`、`*_types.*`、`serial_service_callback_*`）。以 `interfaces/innerkits/*.idl` 为唯一事实源，改后用 `--fast-rebuild` 重新生成，并同步 `services/native/src/serial_service.cpp`、`interfaces/innerkits/native/src/serial_srv_client.cpp` 的 override/调用签名。签名不匹配是本仓最常见编译错误（AGENTS.md「常见陷阱 2」）。
- **禁止**未经请示修改 `ISerialServer.idl` 已有方法签名、`.d.ts` 已发布接口、`SerialPortInfo`/`SerialConfig` 字段；这些是公开 API，兼容性变更需评审（AGENTS.md「Ask before」）。
- 新增接口必须走完八步（AGENTS.md「扩展新接口流程」）：IDL → 生成物核对 → service 实现 → client 方法 → NAPI 绑定导出 → Taihe IDL 声明 → Taihe impl → `.d.ts` 更新。NAPI 与 Taihe 的 API 面必须与 `.d.ts` 保持一致，禁止只改一侧。

## 错误码与错误处理

- **禁止**在新代码中直接返回内部负数码给 JS 层。仓内是双错误码体系：内部码 `SERIAL_ERR_*`（负数，`utils/native/include/serial_error_code.h:26-36`）与 JS 码 `SERIAL_JS_ERR_*`（`35700001`-`35700008`，另有 `202`/`203`/`401`）。客户端边界统一经 `SerialInternalToJsCode()`（`utils/native/src/serial_error_code.cpp:21`）转换；未列内部码一律落到 `35700001`（Service error），**不要**给未知 HDI 错误新造映射。
- **禁止**用 `napi_create_string_utf8` 创建错误码；错误码必须是 `int32_t`（`napi_create_int32`），JS 侧经 `BusinessError.code` 读取。统一使用 `serial_info.cpp` 的 `CreateBusinessError()` / `ThrowBusinessError()`（同步方法抛出）与 `CreateRejectedPromise()`（Promise 拒绝）。
- **禁止**改错误码数值或语义而不更新两侧：`serial_error_code.h/cpp` 与 `.d.ts` 的 `@throws` 声明必须同步（AGENTS.md「Ask before」）。
- HDI 返回码只能经 `SerialHdiToInternalCode()`（`serial_service.cpp:54`）映射为内部码，再交给客户端映射为 JS 码；两段映射职责不可混在一层。
- 新增校验规则时，客户端（`serial_srv_client.cpp` 各方法开头的空串/范围检查）与服务端（`ValidateOpenSerialParams`、`CheckWritePreconditions`）要同时覆盖：portName 非空、baudRate > 0、dataBits ∈ [5,8]、stopBits ∈ [1,2]、parity ∈ [0,4]、write 数据 ∈ (0, 4096]、timeout ∈ [0, 300000]。

## 安全与审计（不可绕过）

- **禁止**绕过 `CheckPortAccessPermission()`（`serial_service.cpp:413`）的 tokenId 归属校验去操作端口：所有打开后的操作必须确认 `DeviceContext::tokenId == GetCallingTokenId()`。
- **禁止**跳过 `SerialSecurityReport` 审计上报：`open`/`close` 每次上报，`StartRead`/`WriteSerial` 每端口各仅一次上报（`QueryAndRecordFirstRead/Write`），策略变更上报管理事件。
- **禁止**修改日志域 `0xD002A08`、审计事件 ID `0x030000100`/`0x030000101`、EDM 参数名 `persist.edm.rs232_serial_disable`：DFX 采集与安全审计按固定取值消费（AGENTS.md「Do not」）。
- **禁止**放宽 `AddPortAuthorization` 的调用方校验（`serial_service.cpp:1277` `ValidateDialogHapCaller`）：调用方必须是 HAP token + 系统应用 + `bundleName == "com.usb.right"`。修改弹窗 HAP 包名属安全评审事项。
- **禁止**未经请示修改授权缓存键格式（`userId_deviceId_bundleName_tokenId`，`serial_right_manager.cpp:88`）或 `serial_right` 表结构：旧数据需迁移（AGENTS.md「Ask before」）。
- **禁止**在日志中明文输出敏感信息；使用 `SERIAL_HILOG*` 宏的 `%{public}s`/`%{private}s` 标注（服务端调用方身份字段只记录于审计事件，不进普通日志）。

## 并发模型

- **禁止**绕开 `serviceMutex_` 直接读写 `deviceContexts_`/`productIdVendorIdToPortName_`/`unloadTimerId_`；该锁保护全部服务可变状态。新增服务状态字段需评估是否纳入同一把锁。
- **禁止**在持有 `serviceMutex_` 时执行可能阻塞的操作（弹窗 `sem_wait`、长 I/O）——弹窗流程在 `RequestSerialPortRight` 中于加锁前完成；`HandleDeviceOffline` 刻意跳过 `hdiDevice->Close()` 就是因为 Close 可能无限阻塞（`serial_service.cpp:823`）。
- **禁止**并发弹窗：`GetUserAgreementByDiag` 全程持 `dialogRunning_`（一次只有一个授权弹窗）。
- 回调分发必须在锁外执行：`SerialServiceCallback::onDataRead`/`onDisconnect` 先在锁内拷贝回调列表，锁外逐个调用（`serial_service_callback.cpp:22`）；NAPI/Taihe 层的 `g_*` 表同模式。**禁止**持锁调用用户回调。
- `OnStop` **禁止**调用 `hdiDevice->Close()`（HDI 可能先停，段错误风险，`serial_service.cpp:278` 注释）；`OnStop` 第一步必须先调 `SerialRightManager::OnStop()` 落库授权缓存。

## NAPI / Taihe 绑定

- **禁止**在 JS 线程之外直接调用 JS 回调：NAPI 数据回调必须经 `napi_threadsafe_function`（`serial_info.cpp` `CreateThreadSafeFunction` + `CallJsCallback`）；Taihe 回调必须经 `napi_async_work`（`ohos.busManager.serial.impl.cpp` `ScheduleCallbackAsyncWork`）调度回 JS 线程。
- **禁止**同步阻塞 JS 线程：所有变更操作必须用 `napi_create_async_work`（Worker 执行 + JS 线程 Complete）；仅同步注册类方法（`onDataRead`/`offDataRead` 等）允许直接返回。
- 端口对象生命周期遵循缓存机制（CREATE/REUSE/RESTORE/INVALIDATE/GC，见 Technology 文档）：`g_activePorts` 持有强引用（ref=1）会**阻止 GC**，必须先 `InvalidatePort`（降为弱引用）后 GC 才能回收；对象被 GC 时 `SerialPortFinalize` 对已打开未失效端口自动 `CloseSerial`。修改端口对象逻辑时必须同时维护 `g_activePorts` 与 `g_invalidatedPorts` 两张表。
- `onDataRead` 重复注册同一 JS 回调被允许（`napi_strict_equals` 判重后跳过），但 `offDataRead` 不传参清空全部；Taihe 层同理（`g_callbacks` 向量 + 单一 innerkit handle）。新增回调类 API 必须实现同等去重与清理（`close()` 时清空所有回调）。
- Taihe 顶层函数只有 `getSerialPortListSync`/`addPortAuthorizationSync` 需要 `TH_EXPORT_CPP_API`；类型冲突用别名规避（`TaiheSerialPortInfo`/`TaiheSerialPort`/`TaiheSerialConfigs`）。

## 代码风格与质量

- K&R 风格：开括号不换行、4 空格缩进、`if` 单语句也必须加花括号、`} else {` 紧凑写法。
- **禁止**单个函数（含 lambda）非空非注释行数超过 **50 行**（AGENTS.md「函数体行数限制」）；超出必须拆子函数（参考 `serial_service.cpp` 的 `ValidateOpenSerialParams`/`OpenHdiDevice`/`BuildDeviceContext` 拆分模式）。
- **禁止**C 风格类型转换（`(int)x`、`(void**)&p`）；必须用 `static_cast`/`reinterpret_cast`/`const_cast`。例外：Taihe impl 中既有的 `(uint32_t)ResourceFlag::...` 风格不应扩散到新代码。
- **禁止**魔鬼数字：数字常量必须定义为 `constexpr` 命名常量（如 `UNLOAD_SA_DELAY_MS`、`ACM_PORT_ID_OFFSET`）；`0`/`1` 等语义明确值可保留。
- 提交前格式化：C/C++ 与 BUILD.gn 的格式化命令及配置见 AGENTS.md 格式化说明（C/C++ 用仓根配置文件，BUILD.gn 用 `gn format`，路径 `prebuilts/build-tools/linux-x86/bin/gn`）；bundle.json 用 python json 序列化。
- IDL/头文件遵循仓内命名：类型 PascalCase，方法与成员 camelCase，文件 snake_case，日志宏 `SERIAL_HILOGI/E/W/D/F(MODULE_*, ...)` 按四模块（`MODULE_INNERKIT`/`MODULE_SERVICE`/`MODULE_JS_NAPI`/`MODULE_TAIHE`）选择。

## 构建纪律

- 改了 `bundle.json`、任何 `BUILD.gn`、新增/删除源文件、或尚无构建产物：**必须完整编译**（无 `--fast-rebuild`），否则出现 "Duplicate definition"。
- 仅改 `.cpp`/`.h`/`.idl`：可用 `--fast-rebuild`（约省 50-70% 时间）。
- 编译必须在 build.sh 所在目录（`../../../`）执行，且按 AGENTS.md 要求使用子 agent，不在主会话直接执行。

## 测试编码（ArkTS/hypium）

- **禁止**用 for 循环动态注册 `it()`（hypium 不支持，运行时报 "No Any Test Case Found"）：每个用例必须静态声明，公共逻辑提取到 `async function runScenario(...)`。
- Promise 类型必须精确（`Promise<void>`/`Promise<number>`/`Promise<boolean>`），**禁止**宽泛的 `Promise<Object>`；`getSerialPortList()` 返回的是 `serial.SerialPort[]`（带方法的对象），不是 `serial.SerialPortInfo[]`。
- ArkTS **禁止**：`as` 类型断言（用 `let err: BusinessError = e`）、给非可选类型赋 `null`、模板字符串（用 `+` 拼接）。
- **禁止**在正式服务代码引入仅测试用的宏或环境变量开关；测试行为一律放 `test/develop/` 独立服务变体（`install_enable=false`）。
