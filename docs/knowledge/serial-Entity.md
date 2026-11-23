# 业务实体知识

serial_manager 模块核心业务实体的完整知识定义，涵盖静态概念、动态运行、规则约束与代码映射。路径均相对仓库根目录。

## 串口设备信息（SerialPortInfo）

### 实体概念

| 子维度 | 内容 |
|---|---|
| 实体名称 | 串口设备信息（`SerialPortInfo`，JS 侧 `SerialPortInfo`） |
| 实体定义 | 一台物理串口设备在服务边界的描述快照：设备是谁（厂商/产品）、在哪（端口名）。它是发现结果、打开目标与端口对象判等的统一载体，不承载任何打开后的运行状态 |
| 核心特征 | 四字段：`portName`（String，剥离 `/dev/` 前缀的设备名）、`productId`（int）、`vendorId`（int）、`manufacturer`（String）；IDL 定义 `interfaces/innerkits/SerialServerTypes.idl:18`，序列化由生成物 `serial_server_types.*` 完成 |
| 类型/分类 | 无业务子类；按来源可分为板载（ttyS）、USB 转串口（ttyUSB）、USB CDC-ACM（ttyACM），判定靠 `IsUsbSerial()` 字符串包含检查（`serial_service.cpp:1730`） |

### 上下文与场景

| 子维度 | 内容 |
|---|---|
| 交互流程 | HDI `QueryDevices` 返回 `SerialDeviceInfo` → `ConvertDeviceInfo`（`serial_service.cpp:390`）剥离 `/dev/` 前缀 → 经 IPC 返回客户端 → NAPI 层构造 JS `portInfo` 子对象并决定端口对象缓存复用/新建；`OpenSerial` 携带客户端持有的 `portInfo` 上行，服务端以四字段与当前枚举结果全等匹配（`serial_service.cpp:634-649`）后才放行 |
| 状态流转 | 无自身状态。每次 `QuerySerialDevices` 都重建列表，同时刷新服务端 `productIdVendorIdToPortName_` 映射表（`serial_service.cpp:458`） |

### 规格与约束

| 子维度 | 内容 |
|---|---|
| 业务规则 | `portName` 空时可用 `productId+vendorId` 反查端口名（`FindPortNameForDevice` 策略 2，`serial_service.cpp:477`）；打开前设备四字段必须与实时枚举一致，否则 `SERIAL_ERR_DEVICE_DISCONNECTED`；EDM 禁用 RS-232 时非 USB 设备不出现在枚举结果中（`QuerySerialDevices` 过滤，`serial_service.cpp:449`） |
| 并发约束 | 枚举与映射表更新持 `serviceMutex_`；NAPI 端对象缓存判等用 `IsSerialPortInfoEqual`（`serial_info.cpp:183`），访问持 `g_portCacheMutex` |

### 知识关联

| 子维度 | 内容 |
|---|---|
| 上层依赖 | 由 `SerialService::QuerySerialDevices` 创建；JS 侧被 `SerialPort` 对象以 `portInfo` 只读属性持有（`.d.ts:60`） |
| 下游影响 | 驱动 `DeviceContext` 创建（打开时的身份校验）、NAPI/Taihe 端口对象缓存的判等与失效（信息变化即 INVALIDATE）、`deviceId` 拼装（授权键组成部分） |
| 平级关联 | `productIdVendorIdToPortName_`（`serial_service.h:194`）记录 `pair<productId, vendorId> → portName` 的反向索引 |

### 代码与符号

| 子维度 | 内容 |
|---|---|
| 核心类 | `SerialPortInfo`（`interfaces/innerkits/SerialServerTypes.idl`）；HDI 侧 `OHOS::HDI::Serial::V1_0::SerialDeviceInfo` |
| 核心方法 | `ConvertDeviceInfo()`（HDI→服务结构）、`FindPortNameForDevice()`（portName/VID+PID 双策略定位）、`IsSerialPortInfoEqual()`（NAPI 判等） |

## 打开端口上下文（DeviceContext）

### 实体概念

| 子维度 | 内容 |
|---|---|
| 实体名称 | 打开端口上下文（`DeviceContext`） |
| 实体定义 | 服务端"一个端口被某客户端成功打开"的全部运行时事实：底层 HDI 设备代理、两条回调链路（HDI 回调 + 客户端回调）、死亡通知对象、调用方身份、打开配置与时间。它是打开后一切操作（写/读/信号/缓冲/关闭）的裁决依据 |
| 核心特征 | `services/native/include/serial_service.h:84`：`hdiDevice`、`hdiCallback`（`SerialHdiCallback`）、`serviceCallback`（客户端 oneway 回调）、`deathRecipient`、`portName`、`bundleName`、`tokenId`、`userId`、`config`、`openTime` |
| 类型/分类 | 无分类；以 `portName` 为唯一键，一个端口同时至多一个实例 |

### 上下文与场景

| 子维度 | 内容 |
|---|---|
| 交互流程 | `OpenSerial` 成功路径创建（`BuildDeviceContext`，`serial_service.cpp:560`）：记录调用方身份、在客户端回调对象上注册 `SerialClientDeathRecipient`、存入 `deviceContexts_`。此后每个操作先 `FindDeviceContext` 再 `CheckPortAccessPermission` |
| 状态流转 | 创建（open 成功）→ 销毁有四条路径：①`CloseSerial`（正常，先摘死亡通知再 HDI Close）；②客户端死亡 `OnClientDied`（HDI Close + 摘表）；③设备离线 `HandleDeviceOffline`（**不调** HDI Close，先 `onDisconnect` 通知客户端）；④`OnStop`（关机，同样**不调** HDI Close）。任一路径后若 `deviceContexts_` 为空则调度 30s 延时卸载 |

### 规格与约束

| 子维度 | 内容 |
|---|---|
| 业务规则 | 端口已存在上下文时再次打开返回 `SERIAL_ERR_DEVICE_BUSY`（35700004，`serial_service.cpp:659`）；所有后续操作的调用方 tokenId 必须等于创建时的 tokenId（`CheckPortAccessPermission`，`serial_service.cpp:413`），不等返回 `SERIAL_ERR_NOT_ALLOWED` |
| 并发约束 | 表 `deviceContexts_` 与映射表均由 `serviceMutex_` 保护；离线回调经全局实例 `GetGlobalInstance()` 回到服务线程处理 |

### 知识关联

| 子维度 | 内容 |
|---|---|
| 上层依赖 | `SerialService` 持有（`deviceContexts_`，`shared_ptr` 值语义） |
| 下游影响 | 消费方为全部端口操作方法与 `DumpOpenedDevices`；其消亡触发 `SerialSrvClient` 侧收到 `onDisconnect`、NAPI 层端口对象被标记 invalidated |
| 平级关联 | 与 `productIdVendorIdToPortName_`（定位）、卸载定时器（空表触发）协作 |

### 代码与符号

| 子维度 | 内容 |
|---|---|
| 核心类 | `DeviceContext`、`SerialHdiCallback`、`SerialClientDeathRecipient`（均在 `serial_service.h`） |
| 核心方法 | `BuildDeviceContext()` / `FindDeviceContext()` / `OnClientDied()` / `HandleDeviceOffline()` / `CheckPortAccessPermission()`（`serial_service.cpp`） |

## 授权记录（SerialRightFullRecord / rightCache_）

### 实体概念

| 子维度 | 内容 |
|---|---|
| 实体名称 | 串口访问授权记录（`SerialRightFullRecord`；DB 行视角 `SerialRightAppInfo`） |
| 实体定义 | "某用户下某应用（token）对某串口设备（deviceId）享有访问授权"这一事实的载体。授权即授予 `open` 资格，作用域是应用 × 设备 × 用户三维 |
| 核心特征 | 字段：`primaryKeyId`、`uid`（userId）、`deviceId`、`bundleName`、`tokenId`、`installTime`/`updateTime`/`requestTime`、`validPeriod`（写入 `SERIAL_RIGHT_VALID_PERIOD_MAX`，即永久）；`serial_right_db_helper.h:50` |
| 类型/分类 | 内存态（`rightCache_` 键 `userId_deviceId_bundleName_tokenId`）与持久态（`serial_right` 表行）两种形态，字段一致 |

### 上下文与场景

| 子维度 | 内容 |
|---|---|
| 交互流程 | 创建有三途：①用户在授权弹窗点同意 → 弹窗 HAP 调 `AddPortAuthorization` → 服务校验调用方（须 `com.usb.right` 系统应用）与目标 token → `SerialRightManager::AddDeviceRight` 写缓存；②系统应用直接调 `addPortAuthorization`（同服务端路径）；③SA 启动时 `LoadCacheFromDb` 从 RDB 批量重建。查询：`open` 前 `RequestRight → HasRight`（只查缓存）。持久化：SA `OnStop` → `SaveCacheToDb` 建库批量落盘 |
| 状态流转 | 缓存内记录无失效时间（validPeriod 永久）；设备离线**不**清除授权（`serial_service.cpp:163` 注释），但插拔导致 portName 变化时因 deviceId 含 portName 而需重新授权；跨重启由 init `post-fs-data` 删除 DB 文件，等效"每次开机授权清零" |

### 规格与约束

| 子维度 | 内容 |
|---|---|
| 业务规则 | 空 `bundleName` 记作 `"serial_cli"`（CLI 场景）；系统 HAP 应用（`IsSystemAppOrSa`）打开时旁路授权；弹窗全流程互斥（`dialogRunning_`），`sem_wait` 阻塞等待用户操作；弹窗结束后以 `HasRight` 复查判定用户是否同意（弹窗 HAP 失败未调授权接口也判为拒绝） |
| 并发约束 | `cacheMutex_` 保护 `rightCache_`；`dialogParams_`（静态）由 `dialogParamsMutex_` 保护；DB 操作由 `databaseMutex_` 保护且单例（`SerialRightDbHelper::GetInstance`） |

### 知识关联

| 子维度 | 内容 |
|---|---|
| 上层依赖 | `SerialRightManager` 持有缓存；`SerialService::OpenSerial` 经 `RequestSerialPortRight`（`serial_service.cpp:1455`）消费 |
| 下游影响 | 决定 `open` 是否弹窗/放行；`OnStop` 落库时机影响同次开机内 SA 重启后的免弹窗 |
| 平级关联 | 键中的 `deviceId` 由 `BuildDeviceId()` 从 `SerialPortInfo` 拼装 |

### 代码与符号

| 子维度 | 内容 |
|---|---|
| 核心类 | `SerialRightManager`（`serial_right_manager.h`）、`SerialRightDbHelper` + `SerialRightRdbOpenCallback`（`serial_right_db_helper.h`）、内部 `SerialAbilityConn`（弹窗连接回调） |
| 核心方法 | `HasRight()` / `AddDeviceRight()`（仅缓存）、`RequestRight()` / `GetUserAgreementByDiag()` / `ShowSerialDialog()` / `ConnectAbility()`（弹窗链）、`LoadCacheFromDb()` / `SaveCacheToDb()` / `InsertAllRecords()`（持久化） |

## 数据监听器（ListenerHandle 与两层监听表）

### 实体概念

| 子维度 | 内容 |
|---|---|
| 实体名称 | 数据监听器（`ListenerHandle`、`DataReceivedCallback`、`DisconnectedCallback`） |
| 实体定义 | 应用注册的"对某端口的回调"在客户端内的登记项。innerkit 层以 `std::function` + 递增句柄表示；NAPI 层额外持有 `napi_ref` 与线程安全函数；Taihe 层持有 `taihe::callback`。一个端口可挂多个监听器（多路复用），底层读取线程只按"有无监听器"启停 |
| 核心特征 | `ListenerHandle = uint64_t`，从 1 递增（0 为非法值 `INVALID_LISTENER_HANDLE`）；innerkit 表 `listeners_`/`disconnectedListeners_` 为 `map<portName, map<handle, callback>>`（`serial_service_callback.h:53-59`） |
| 类型/分类 | 数据监听（`onDataRead`）与断连监听（`onDisconnect`）两类，各有独立表与互斥锁 |

### 上下文与场景

| 子维度 | 内容 |
|---|---|
| 交互流程 | 注册：JS `onDataRead(cb)` → NAPI `RegisterOnDataReadListener`（`serial_info.cpp:1244`）建 tsfn → `SerialSrvClient::RegisterDataListener`（`serial_srv_client.cpp:400`）加表；**该端口第一个**监听器触发 IPC `StartRead`（失败回滚已加表项）。数据到达：服务端 oneway `onDataRead` → `SerialServiceCallback::onDataRead` 锁内拷贝、锁外逐个回调 → NAPI `CallJsCallback`（JS 线程）。注销：`offDataRead([cb])` → 定向移除（`napi_strict_equals` 匹配）或全清；**最后一个**移除触发 IPC `StopRead` |
| 状态流转 | 句柄生命周期 = 注册→注销；`close()` 与 `onDisconnect`（客户端侧收到后）会清空该端口全部监听器；`OpenSerial` 成功后客户端也先清空该端口旧监听器（`serial_srv_client.cpp:174-177`） |

### 规格与约束

| 子维度 | 内容 |
|---|---|
| 业务规则 | NAPI 层同端口重复注册同一 JS 函数被静默跳过（去重）；Taihe 层每端口仅向 innerkit 注册**一个**数据监听器（`g_innerkitHandles` 每端口一个 handle），对上向多个 `taihe::callback` 广播 |
| 并发约束 | innerkit 注册/注销整体持 `listenerMutex_`（StartRead/StopRead 决策与表变更原子）；`onDataRead` 分发先拷贝后调用，禁止持锁调用户回调 |

### 知识关联

| 子维度 | 内容 |
|---|---|
| 上层依赖 | NAPI `g_dataCallbacks`（`serial_info.cpp:160`）与 Taihe `g_callbacks`（`ohos.busManager.serial.impl.cpp:70`）分别是两绑定的登记总表 |
| 下游影响 | 监听器数量驱动服务端 `StartRead`/`StopRead`；断连监听触发 NAPI 端口对象失效标记 |
| 平级关联 | 与 `SerialServiceCallback`（回调桩本体）共生：桩随 `SerialSrvClient` 单例存在，`OpenSerial` 时作为 `ISerialServiceCallback` 传给服务端 |

### 代码与符号

| 子维度 | 内容 |
|---|---|
| 核心类 | `SerialServiceCallback`（`interfaces/innerkits/native/include/serial_service_callback.h`） |
| 核心方法 | `AddListener`/`RemoveListener`/`RemoveAllListeners`/`HasListeners`/`GetListenerCount`、`onDataRead`/`onDisconnect`（实现 `serial_service_callback.cpp`） |

## 端口对象（SerialPort JS/Taihe 对象与 SerialPortNative）

### 实体概念

| 子维度 | 内容 |
|---|---|
| 实体名称 | 端口对象（JS `SerialPort` interface / Taihe `interface SerialPort`；NAPI 原生态 `SerialPortNative`，Taihe 原生态 `SerialPortHolder`） |
| 实体定义 | `getSerialPortList()` 返回给应用的对象：静态描述（portName/portInfo）+ 14 个方法 + 打开标记。NAPI 侧经 `napi_wrap` 绑定 `SerialPortNative*`，缓存机制保证同一物理端口跨多次查询返回同一 JS 对象（身份稳定，回调与状态可延续） |
| 核心特征 | `SerialPortNative`（`serial_info.cpp:170`）：`portName`、`info`、`invalidated`/`isOpened`（atomic）、`objectRef`（napi_ref）、`env`。JS 对象固定携带 `portName` 属性、`portInfo` 子对象与 14 个函数（`SerialPortToJSObject`，`serial_info.cpp:393`） |
| 类型/分类 | NAPI 与 Taihe 两套实现；Taihe 侧还有 `SerialPortInfoHolder`（portInfo 提供者） |

### 上下文与场景

| 子维度 | 内容 |
|---|---|
| 交互流程 | `getSerialPortList` Complete 阶段 `CreateDeviceListArray`（`serial_info.cpp:551`）对每个设备：先 `TryReuseActivePort`（活动且信息相等 → 复用原 JS 对象，重连时清除 invalidated 标记 RESTORE）；否则 `RestoreInvalidatedPort`（失效池中信息相等且引用仍可取回 → 降级引用升强引用恢复）；否则 `CreateNewPortObject` 新建；最后 `InvalidateDisappearedPorts` 把本次未出现的活动端口全部失效。`open` 成功 → `isOpened=true` 并注册断连监听（标记 invalidated/isOpened）；GC 时 `SerialPortFinalize` 对"已打开且未失效"的对象自动 `CloseSerial` 兜底 |
| 状态流转 | 活动态（强引用，阻止 GC）↔ 失效态（弱引用，等 GC）：失效触发有消失（枚举不再出现）、信息变化、设备断连、显式 `close()` 前的 INVALIDATE 标记；回收（GC finalize）为终态，清理两表与引用 |
| 交互流程补充 | Taihe 侧 `g_cachedPorts`（`CachedPortInfo`）+ `g_holderRegistry`（multimap）实现同类缓存；`GetSerialPortListSync` 经 `make_holder<SerialPortHolder, SerialPort>(...)` 创建接口实例 |

### 规格与约束

| 子维度 | 内容 |
|---|---|
| 业务规则 | 活动对象强引用 ref=1 阻止 GC：想触发回收必须先 INVALIDATE（降弱引用）再 GC（AGENTS.md「常见陷阱 5」）；对失效对象调用方法返回 `35700003`（`IsPortInvalidated` 检查），**例外**：`close()` 跳过服务调用直接 resolve（`serial_info.cpp:995-1012`）、`offDataRead`/`offDisconnect` 静默成功（`skipService`，`serial_info.cpp:1419/2127`），Taihe `close()` 同样跳过（`ohos.busManager.serial.impl.cpp:572-577`）；`write`/`onDataRead` 等方法前检查 `isOpened`（未打开抛 `35700005`） |
| 并发约束 | 两表由 `g_portCacheMutex` 保护；对象内部标志为 atomic；Taihe 侧 `g_portCacheMutex`/`g_holderRegistryMutex` |

### 知识关联

| 子维度 | 内容 |
|---|---|
| 上层依赖 | 由 NAPI/Taihe 绑定层创建持有；应用侧是唯一使用者 |
| 下游影响 | 其方法全部委托 `SerialSrvClient`；其失效由服务端 `onDisconnect`/枚举差异驱动 |
| 平级关联 | `g_dataCallbacks` 中的回调按 portName 与之关联 |

### 代码与符号

| 子维度 | 内容 |
|---|---|
| 核心类 | `SerialPortNative`（`serial_info.cpp:170`）、`SerialPortHolder`/`SerialPortInfoHolder`（`ohos.busManager.serial.impl.cpp:430` 起） |
| 核心方法 | `SerialPortToJSObject`、`InvalidatePort`、`RestoreInvalidatedPort`、`InvalidateDisappearedPorts`、`SerialPortFinalize`（`serial_info.cpp:233-579`） |

## 审计事件（操作审计 / 管理审计）

### 实体概念

| 子维度 | 内容 |
|---|---|
| 实体名称 | 串口审计事件（`SERIAL_OP_EVENT_ID` 操作事件 / `SERIAL_EDM_EVENT_ID` 管理事件） |
| 实体定义 | 上报给 security_guard 的安全留痕事实。操作事件记录"谁对哪个端口做了 Open/Close/Read/Write"；管理事件记录"RS-232 策略被启停" |
| 核心特征 | ID 与版本常量 `serial_service.h:46-49`；操作事件 JSON 字段：`happenTime`（ms）、`portId`（`CalculatePortId` 编码）、`operationType`（`SerialOpen/SerialClose/SerialRead/SerialWrite`）、`uid`（`IPCSkeleton::GetCallingAppUid`）；管理事件字段：`portType`（"RS232"）、`portStatus`（Disabled/Enabled）、`userId`、`happenTime` |
| 类型/分类 | 两类；均默认异步上报（`ReportSecurityInfoAsync`） |

### 上下文与场景

| 子维度 | 内容 |
|---|---|
| 交互流程 | `OpenSerial`/`CloseSerial` 每次调用直接上报；`StartRead`/`WriteSerial` 经 `QueryAndRecordFirstRead/Write`（端口名插入 set，插入成功才上报）——即同一端口的读/写各只上报一次，`CloseSerial` 时 `ResetFirstRead/Write` 重置；EDM 策略变化经 `WatchParameter` 回调上报管理事件（`reportManageFlag_` 抑制非参数触发的管理事件上报，`serial_service.cpp:1741-1747`） |
| 状态流转 | 事件为一次性产物，无状态；"已上报"标记存于 `portsHasBeenRead_`/`portsHasBeenWritten_`（独立互斥锁），随端口关闭重置 |

### 规格与约束

| 子维度 | 内容 |
|---|---|
| 业务规则 | 事件 ID/版本、字段名为安全采集契约，禁止修改（AGENTS.md「Do not」）；`SECURITY_GUARD_ENABLE` 由部件存在性门控（`services/BUILD.gn:89-93`），未启用时上报退化为日志 |
| 并发约束 | 读/写标记分别由 `readStatusMutex_`/`writeStatusMutex_` 保护，避免并发调用漏报 |

### 知识关联

| 子维度 | 内容 |
|---|---|
| 上层依赖 | `SerialService` 产生；`SerialSecurityReport` 送达（`serial_security_report.cpp`） |
| 下游影响 | security_guard 消费 |
| 平级关联 | `portId` 编码依赖 `pcieSerialOffsets_`（PCIE 配置） |

### 代码与符号

| 子维度 | 内容 |
|---|---|
| 核心类 | `SerialSecurityReport`（`serial_security_report.h`） |
| 核心方法 | `ReportSerialOperationAudit`、`ReportSerialManageAuditEvent`、`CalculatePortId`、`QueryAndRecordFirstRead/Write`（`serial_service.cpp`） |
