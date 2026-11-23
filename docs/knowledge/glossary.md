# 术语表

按"通用机制 → 业务模块"分组。数值与代码位置均以仓内实现核实；与文档（README/`.d.ts`）不一致处以代码为准并注明。

## SA 4202 / serial_service 进程

串口管理 System Ability。ID `SERIAL_SYSTEM_ABILITY_ID = 4202`（定义于系统头 `system_ability_definition.h`），进程名 `serial_service`（`sa_profile/4202.json`），以 uid/gid `serial` 运行，`apl system_basic`，SELinux `u:r:serial_service:s0`（`services/serial_service.cfg`）。`run-on-create=false`（不随进程启动即实例化），`auto-restart=true`；`start-on-demand` 由参数 `persist.edm.rs232_serial_disable`（值 0/1）触发。服务实现 `services/native/src/serial_service.cpp`。

## 串口管理器（serial_manager / @ohos.busManager.serial）

本仓部件名。子系统 `busmanager`，syscap `SystemCapability.BusManager.Serial`，模块名 `@ohos.busManager.serial`（`@since 26.0.0 dynamic&static`：dynamic 指 NAPI 路径、static 指 Taihe 路径，同一 `.d.ts` 契约）。区别于 usb_manager 内的 serial 模块：后者以 `portId` 标识 USB 转串口、提供同步/异步 read；本仓以设备名标识端口、只提供回调式接收，并扩展了流控信号、缓冲控制、应用级授权与企业策略。

## 端口名（portName）

端口的业务标识，为设备节点名去掉 `/dev/` 前缀（`serial_service.cpp:390` `ConvertDeviceInfo` 剥离前缀），如 `ttyS0`、`ttyUSB0`、`ttyACM0`。`OpenSerial` 时按 portName 主键定位；服务端调用 HDI 时会重新拼回 `"/dev/" + portName`（`serial_service.cpp:665`）。与 usb_manager 的 `portId`（bus/dev、VID/PID、序列号组合）不是同一概念。

## 端口 ID（审计 portId，CalculatePortId）

仅用于审计事件 `portId` 字段的整数编码（`serial_service.cpp:1774`）：取 portName 末尾数字为 baseId；USB 串口（含 "USB"）→ baseId；ACM（含 "ACM"）→ baseId + `ACM_PORT_ID_OFFSET`(1000)；命中 `pcieSerialOffsets_` 前缀（`ttyS` 固定 `TTYS_PORT_ID_OFFSET` 2000，其余前缀来自 `etc/serial/pcie_serial_config.json`）→ baseId + offset；其余板载串口 → -baseId。**不是**对外 API 的端口标识。

## 设备信息（SerialPortInfo）

`interfaces/innerkits/SerialServerTypes.idl` 定义的四字段结构：`portName`、`productId`、`vendorId`、`manufacturer`，来自 HDI `SerialDeviceInfo`。是设备匹配（打开时四字段全等校验）、端口对象缓存判等（`IsSerialPortInfoEqual`）的依据。

## 通信配置（SerialConfig / SerialConfigs）

IDL 结构 `SerialConfig`（8 个 int/boolean 字段）与 JS 侧 `SerialConfigs`（`.d.ts`，字段全部 optional）对应。默认值：`baudRate=115200`、`dataBits=8`、`stopBits=1`、`parity=0(none)`、`rtscts/xon/xoff/xany=false`（NAPI `serial_info.cpp:46-49`，Taihe `MakeDefaultConfig`）。校验范围：dataBits ∈ [5,8]、stopBits ∈ [1,2]、parity ∈ [0,4]，客户端与服务端双份实现。

## 打开端口上下文（DeviceContext）

服务端"一个已打开端口"的聚合实体（`services/native/include/serial_service.h:84`）：HDI 设备代理、HDI 回调、客户端回调、死亡通知对象、portName、调用方 `bundleName`/`tokenId`/`userId`、打开时的 config 与时间戳。以 portName 为键存于 `deviceContexts_`，由 `serviceMutex_` 保护；创建于 `OpenSerial`，销毁于 `CloseSerial`、客户端死亡（`OnClientDied`）、设备离线（`HandleDeviceOffline`）或 `OnStop`。

## 设备标识（deviceId，BuildDeviceId）

授权体系里"一台串口设备"的业务标识，由 `BuildDeviceId()`（`serial_service.cpp:1590`）拼装：`"{vendorId:04x}_{productId:04x}_{manufacturer}_{portName}"`（0/空字段跳过）。板载串口通常退化为 `"_portName"` 形态。`.d.ts` 对 `addPortAuthorization` 的 `deviceId` 描述为"板载串口取 portName，USB 虚拟串口取 VID+PID+SN 组合"——实际实现以代码拼装格式为准，两处描述存在差异，以代码为准。

## 授权记录（SerialRightFullRecord / rightCache_）

"某用户下某应用对某设备享有串口访问授权"的实体（`serial_right_db_helper.h:50`）。内存缓存 `rightCache_`（`serial_right_manager.h:143`）键格式为 `userId_deviceId_bundleName_tokenId`（`BuildCacheKey`）。`HasRight`/`AddDeviceRight` 只操作缓存不触碰数据库；RDB 仅在 SA `OnStart` 加载、`OnStop` 批量落库。

## 授权数据库（SerialRight.db / serial_right 表）

RDB 文件 `/data/service/el1/public/database/serial_manager/SerialRight.db`（`serial_right_db_helper.cpp:33`），版本 1，表 `serial_right`（id 自增主键、uid、device_id、bundle_name、token_id、install/update/request_time、valid_period），安全级别 S1。`OnStart` 仅尝试打开已存在库（`OpenDatabaseIfExists`），`OnStop` 才创建并批量写入。注意：`services/serial_service.cfg` 的 `post-fs-data` job 每次开机**删除**该库文件——因此授权持久性是"单次开机周期内跨 SA 重启"，而非跨重启（与 `serial_service.cpp:163` 注释"authorization valid for this launch cycle"一致）。

## 弹窗 HAP（com.usb.right / UsbServiceExtAbility）

串口授权弹窗的宿主应用。服务端 `ConnectAbility` 实际连接的是 sceneboard（`com.ohos.sceneboard.systemdialog`，回退 `com.ohos.systemui.dialog`），连接成功后以 MessageParcel 携带 `bundleName="com.usb.right"`、`abilityName="UsbServiceExtAbility"` 与弹窗参数请求展示（`serial_right_manager.cpp:438` `OnAbilityConnectDone`，命令码 `CMD_CODE_SHOW_DIALOG=1`）。用户同意后该 HAP 反向调用 `AddPortAuthorization` 写入授权。`AddPortAuthorization` 的调用方白名单也限定为 `com.usb.right`（`serial_service.cpp:1303`）。

## 令牌标识（tokenId / bundleName / userId）

调用方身份三元组：`token_id`（`IPCSkeleton::GetCallingTokenID` 十进制字符串）、`bundle_name`（HAP 取 `HapTokenInfo.bundleName`，native 取进程名，`GetCallingBundleName`）、用户 ID（HAP 取 `userID`，否则按 UID 换算，默认 `SERIAL_RIGHT_USERID_DEFAULT=100`，控制台 0）。空 bundleName 在授权体系中默认为 `"serial_cli"`（CLI 工具，`serial_right_manager.cpp:182`）。

## 系统应用旁路（IsSystemAppOrSa）

`SerialRightManager::IsSystemAppOrSa()`（`serial_right_manager.cpp:396`）：仅当调用方为 HAP token 且 `TokenIdKit::IsSystemAppByFullTokenID` 为真时返回 true，可跳过授权弹窗；native/SA token 按当前实现返回 false（方法名含 "OrSa" 但代码未对 native token 放行——以代码为准，若需 SA 旁路属行为变更，需评审）。

## 企业管控（EDM / persist.edm.rs232_serial_disable）

企业设备管理策略参数。置 "1" 时 `isRs232Disabled_=true`：`QuerySerialDevices` 过滤非 USB 串口（全被过滤返回 `SERIAL_ERR_ENTERPRISE_POLICY` → JS 203），所有端口操作对非 USB 端口拒绝（返回 `SERIAL_ERR_SERVICE`）。服务通过 `WatchParameter` 动态响应（`serial_service.cpp:200`），该参数变更亦触发 SA 按需启动。策略启停上报管理审计事件 `0x030000101`。

## USB 串口判定（IsUsbSerial）

`portName` 含 `"USB"` 或 `"ACM"` 即视为 USB 串口（`serial_service.cpp:1730`），EDM 禁用时仅这类端口可用。

## HDI 串口服务（ISerials / ISerialDevice / SerialHdiCallback）

`drivers_interface_serial` V1_0 接口。`ISerials` 为枚举/打开入口（`QueryDevices`/`OpenDevice`），`ISerialDevice` 为单端口操作（`Write`/`StartRead`/`StopRead`/`Flush`/`Drain`/`SendBrkSignal`/`SetRtsSignal`/`GetCtsSignal`/`SetDtrSignal`/`GetDsrSignal`/`Close`）。`SerialHdiCallback`（`serial_service.h:59`）是本服务的桥接回调：`OnReadData`（int8→uint8 转换后转发客户端）、`OnDeviceOffline`（触发 `HandleDeviceOffline`）。

## 数据回调（ISerialServiceCallback / onDataRead / onDisconnect）

客户端向服务端注册的 oneway 回调接口（`ISerialServiceCallback.idl`，两方法均 `[oneway]`）。`onDataRead(portName, buffer)` 是接收数据唯一通道；`onDisconnect(portName)` 通知端口失效。注意：接口自 `OpenSerial` 时随参数注册，服务端按回调远端对象挂死亡通知。

## 监听器与监听器句柄（ListenerHandle）

客户端侧"JS 回调的登记凭证"，`uint64_t`，从 1 递增（`serial_service_callback.h:32`）。数据监听按 `map<portName, map<handle, callback>>` 多路复用：第一个注册触发 `StartRead`、最后一个注销触发 `StopRead`（`serial_srv_client.cpp:400-535`）。NAPI 层另维护 `g_dataCallbacks`（napi_ref → CallbackInfo）完成 tsfn 与 handle 的对应；Taihe 层每端口只向 innerkit 注册一个监听器（`g_innerkitHandles`），对上多路复用 `g_callbacks`。

## 端口对象缓存（g_activePorts / g_invalidatedPorts / SerialPortNative）

NAPI 层的 JS `SerialPort` 对象与 `SerialPortNative*` 绑定表（`serial_info.cpp:170-181`）。活动端口持强引用（ref=1，阻止 GC），失效端口降为弱引用等待 GC。生命周期事件链：CREATE / REUSE / RESTORE / INVALIDATE / GC（详见 Technology 文档「端口对象缓存与失效恢复机制」）。Taihe 层对应 `g_cachedPorts` + `g_holderRegistry`。

## 内部错误码 / JS 错误码（SERIAL_ERR_* / SERIAL_JS_ERR_*）

双错误码体系（`utils/native/include/serial_error_code.h`）：内部码负数（`SERIAL_ERR_SERVICE=-1` 至 `SERIAL_ERR_PARAM_ERROR=-11`），JS 码正数（`35700001`-`35700008` 业务码 + `202` 非系统应用 + `203` 企业策略 + `401` 参数错误）。`SerialInternalToJsCode()` 负责映射，未知码落 `35700001`；`SerialJsErrorMessage()` 提供文案。NAPI 经 `BusinessError.code` 抛出。

## not supported stub（801）

`serial_manager_feature_deploy_sa=false` 时客户端/NAPI/Taihe 换用 `*_without_service.cpp`：`QuerySerialDevices` 返回空列表 + 成功，其余所有操作返回 `SERIAL_JS_ERR_NOT_SUPPORTED = 801`（`serial_srv_client_without_service.cpp:24`）。用于无 SA 部署形态的 API 面兼容。

## 日志域与模块（0xD002A08 / MODULE_*）

全仓统一日志域 `BASE_SERIAL_DOMAIN_ID = 0xD002A08`（`utils/native/include/hilog_wrapper.h:56`），四个模块 tag：`MODULE_INNERKIT`→`SerialSrvClient`、`MODULE_SERVICE`→`SerialService`、`MODULE_JS_NAPI`→`SerialNapi`、`MODULE_TAIHE`→`SerialTaihe`。该取值被 IDL 生成配置（`interfaces/innerkits/BUILD.gn` `log_domainid`）与 DFX 采集共同依赖，禁止修改。

## 审计事件（0x030000100 / 0x030000101）

security_guard 上报的两个安全事件（`serial_service.h:46-47`）：**操作审计** `SERIAL_OP_EVENT_ID=0x030000100`，字段 happenTime/portId/operationType(SerialOpen/SerialClose/SerialRead/SerialWrite)/uid；open 与 close 每次上报，read/write 每端口各一次（关闭时重置）；**管理审计** `SERIAL_EDM_EVENT_ID=0x030000101`，字段 portType(RS232)/portStatus(Enabled/Disabled)/userId/happenTime。上报经 `SerialSecurityReport::ReportSecurityInfo`（默认异步，`SECURITY_GUARD_ENABLE` 门控）。

## Taihe / ANI / ABC

ArkTS 静态化发布链路：Taihe IDL（`ohos.busManager.serial.taihe`）→ 生成 ANI 绑定（`.ani.cpp`/`.abi.c`）→ `serial_manager_taihe_native.z.so`（含 `ANI_Constructor` 入口与 C++ impl）→ 编译为静态 ABC `serial_manager_abc.abc`（boot abc，安装 `/system/framework/`）。`@promise` 标注生成 Promise 方法、`@get` 映射属性、`@gen_promise` 同时生成 sync+Promise 两版、`@sts_inject` 注入手写 ArkTS（如 `getSerialPortList` 的 Promise 包装）。

## 环回读线程（仅测试变体）

模拟硬件收发的读线程（本地 `ReadContext` + `ReadThreadFunc`），只存在于 `test/develop/` 的服务变体（lifecycle_test/errcodetest/paramtest/twodevice_test）。正式服务对接真实 HDI，**没有**环回逻辑——`onDataRead` 不触发时首先怀疑无硬件数据（AGENTS.md「常见陷阱 3」）。

## 延时卸载（UNLOAD_SA_DELAY_MS = 30000）

服务空闲自卸载机制：无打开端口满 30 秒（`Utils::Timer`）后向 SAMgr 发 `UnloadSystemAbility(4202)`；任何 `OpenSerial` 会取消定时器。见 Technology「SA 按需生命周期机制」。

## IPC full 监控

`hicollie` 组件的 `IpcFull::AddIpcFull(30s)`（`serial_service.cpp:233`）：周期检测 Binder 缓冲区占用，防范 oneway 数据回调洪泛导致的 IPC 满阻塞。

## hidumper

系统诊断工具入口：`hidumper -s 4202 [-a]` 调 `SerialService::Dump`，输出物理设备列表与已打开端口上下文（`-h` 帮助、`-a`/`-l` 全量）。
