# Serial Manager 项目简介

## 项目概述

Serial Manager 是一个基于OpenHarmony系统的串口设备管理服务组件，提供串口设备的发现、打开、关闭、读写、流控制等完整管理功能。该组件实现了完整的Native到ArkTS的NAPI绑定层，并通过IDL驱动的IPC机制实现Client-Server间通信。

## 主要功能

- **设备发现**：`getSerialPortList()` 返回可用的串口设备列表
- **端口操作**：`open(config)` / `close()` 打开/关闭串口，支持波特率、数据位、停止位、校验位等配置
- **数据读写**：`write(data, timeout)` 写入数据，`onDataRead(callback)` / `offDataRead(callback)` 注册/取消数据读取回调
- **流控制**：`setRts(enable)` 设置RTS信号，`getCts()` 获取CTS信号状态
- **缓冲区控制**：`flush()` 清空缓冲区，`drain()` 等待写入完成，`sendBrk()` 发送中断信号
- **权限管理**：`addPortAuthorization(tokenId, deviceId)` 系统级端口授权（系统API）
- **系统服务**：作为System Ability（SA 4202）运行在系统服务层，按需加载、自动重启
- **IPC通信**：通过Binder机制实现Client-Server间通信，支持服务端到客户端的异步数据回调

## 技术架构

### NAPI 路径

```
┌──────────────────────────────────────────────┐
│            ArkTS 应用层                         │
│  import serial from '@ohos.busManager.serial' │
└────────────────────┬─────────────────────────┘
                      │
┌────────────────────▼─────────────────────────┐
│            NAPI 绑定层 (libserial.z.so)        │
│  getSerialPortList / SerialPort 对象方法        │
│  DataBits / StopBits / Parity 枚举             │
└────────────────────┬─────────────────────────┘
                      │
┌────────────────────▼─────────────────────────┐
│        InnerAPI 客户端 (libserialsrv_client.z.so) │
│  SerialSrvClient (单例)                        │
│  SerialServiceCallback (接收服务端数据回调)       │
└────────────────────┬─────────────────────────┘
                      │ IPC (Binder) SA ID: 4202
┌────────────────────▼─────────────────────────┐
│        System Ability 服务 (libserialservice.z.so) │
│  SerialService extends SystemAbility + Stub    │
│  对接 HDI ISerials，实现真实串口 I/O         │
└──────────────────────────────────────────────┘
```

### Taihe 路径（ArkTS-Static）

```
┌──────────────────────────────────────────────┐
│            ArkTS 应用层                         │
│  import serial from '@ohos.busManager.serial' │
└────────────────────┬─────────────────────────┘
                      │
┌────────────────────▼─────────────────────────┐
│     Taihe ABC (serial_manager_abc.abc)         │
│  interface SerialPort + 枚举/结构体             │
└────────────────────┬─────────────────────────┘
                      │
┌────────────────────▼─────────────────────────┐
│   ANI Bridge (serial_manager_taihe_native.z.so)│
│  SerialPortHolder 类，持有端口状态               │
│  所有方法委托到 SerialSrvClient                  │
└────────────────────┬─────────────────────────┘
                      │
┌────────────────────▼─────────────────────────┐
│        InnerAPI 客户端 → IPC → SA 服务           │
└──────────────────────────────────────────────┘
```

## 与 usb_manager 的关系与兼容性

USB 转串口的通信能力最初实现于 `usb_manager` 仓内的 serial 模块。随着串口场景从 USB 转串口扩展到板载串口，本仓将通用串口管理能力独立为 System Ability，两者能力互补、边界清晰：

| 能力维度 | usb_manager 的 serial 模块 | serial_manager（本仓） |
| --- | --- | --- |
| 设备范围 | USB 转串口（`ttyUSB`，按 portId 标识） | 板载串口（`ttyS`）、USB 转串口（`ttyUSB`）、USB CDC-ACM（`ttyACM`） |
| 对外接口 | `@ohos.usbManager.serial` | `@ohos.busManager.serial` |
| 数据收发 | 同步/异步 `read`、`write` | 异步写入 + `onDataRead` 回调推送 |
| 串口信号 | 不涉及 | RTS/CTS、DTR/DSR 读写 |
| 缓冲控制 | 不涉及 | `flush` / `drain` / `sendBrk` |
| 通信参数 | 属性读写（`getAttribute` / `setAttribute`） | 打开时配置波特率、数据位、停止位、校验位、软硬件流控 |
| 权限模型 | 按 tokenId 校验端口归属，提供 has/request/add/cancel 权限接口 | 应用级授权（弹窗 + token 校验 + RDB 持久化），支持 EDM 企业策略 |
| 服务形态 | `usb_manager` SA 内的模块 | 独立 System Ability（SA 4202），按需加载、自动重启 |

**两仓在串口链路上的分工**

```
                    ArkTS 应用
          ┌──────────────┴──────────────┐
          │                             │
   serial_manager                  usb_manager
   通用串口管理                   USB 基础能力（含 serial 模块）
   多串口类型 + 完整信号管理         USB 转串口基础通信
          │                             │
   HDI ISerials V1_0              USB HAL（USB Serial HDI）
          │                             │
          └──────────────┬──────────────┘
                         ▼
          /dev/ttyS*、/dev/ttyUSB*、/dev/ttyACM*
```

### 兼容说明

1. **端口标识不同**：本仓以设备名（如 `ttyUSB0`）标识端口，`usb_manager` 以 `portId`（对应 USB 设备的 bus/dev、VID/PID、序列号）标识端口，两者最终操作同一批 USB 转串口设备。同一端口不宜同时由两个服务打开：本仓在服务内对已打开的端口返回 `35700004`（端口已被占用），跨服务的并发访问需由应用侧规避。
2. **能力互补**：仅需 USB 转串口的基础收发，可继续使用 `usb_manager`；需要板载串口、流控信号或缓冲控制时，应使用本仓。
3. **接口差异**：本仓以 `onDataRead` 回调推送数据，并额外提供 `setDtr`/`getDsr`、`flush`/`drain`/`sendBrk`，以及按应用与设备维度的持久化授权（RDB）与企业管控（EDM）策略；`usb_manager` 提供同步/异步 `read`、`write` 与更细粒度的权限接口。
4. **迁移建议**：新增串口业务统一使用 `@ohos.busManager.serial`；存量 USB 业务无需改造，两者按上述边界并行演进。

## 项目结构

```
serial_manager/
├── bundle.json                                  # 组件定义
├── serialmgr.gni                                # 全局路径变量
├── interfaces/
│   ├── innerkits/
│   │   ├── ISerialServer.idl                    # 主接口IDL
│   │   ├── ISerialServiceCallback.idl           # 数据回调IDL（oneway）
│   │   ├── SerialServerTypes.idl                # 数据类型IDL
│   │   ├── BUILD.gn                             # InnerAPI构建配置
│   │   └── native/
│   │       ├── include/
│   │       │   ├── serial_srv_client.h          # 客户端单例类
│   │       │   └── serial_service_callback.h    # 回调桩 + 监听器管理
│   │       └── src/
│   │           ├── serial_srv_client.cpp        # 客户端实现
│   │           └── serial_service_callback.cpp  # 回调分发 + 监听器管理实现
│   └── kits/js/
│       ├── @ohos.busManager.serial.d.ts         # ArkTS API 声明
│       ├── napi/
│       │   ├── BUILD.gn                         # NAPI构建配置
│       │   ├── include/
│       │   │   └── napi_common.h                # NAPI模块初始化声明
│       │   └── src/
│       │       └── serial_info.cpp              # NAPI完整实现
│       └── taihe/
│           ├── BUILD.gn                         # Taihe构建配置（IDL→ANI→共享库→ABC→安装）
│           ├── idl/
│           │   └── ohos.busManager.serial.taihe # Taihe IDL（interface SerialPort + struct/enum）
│           └── src/
│               ├── ani_constructor.cpp          # ANI入口，注册Taihe模块
│               └── ohos.busManager.serial.impl.cpp # Taihe C++实现（SerialPortHolder）
├── services/
│   ├── BUILD.gn                                 # 服务构建配置
│   ├── serial_service.cfg                       # Init进程服务配置
│   └── native/
│       ├── include/
│       │   └── serial_service.h                 # 服务头文件
│       └── src/
│           └── serial_service.cpp               # 服务实现（HDI 对接）
├── sa_profile/
│   ├── 4202.json                                # SA配置文件
│   └── BUILD.gn                                 # SA构建配置
├── utils/
│   ├── BUILD.gn                                 # 工具库构建配置
│   └── native/
│       ├── include/
│       │   ├── hilog_wrapper.h                  # 日志宏（CLIENT/SERVICE/NAPI/TAIHE四模块）
│       │   └── serial_error_code.h              # 错误码定义 + 内部码↔JS码映射
│       └── src/
│           └── serial_error_code.cpp            # 错误码映射 + 消息查询实现
└── test/                                        # 测试（develop/ 服务变体 + unittest/ 单元测试）
```

## IDL 接口定义

### SerialServerTypes.idl — 数据类型

```idl
package OHOS.Serial;

struct SerialPortInfo {
    String portName;         // 端口名，如 "/dev/ttyS0"
    int productId;
    int vendorId;
    String manufacturer;
};

struct SerialConfig {
    int baudRate;            // 波特率，默认 115200
    int dataBits;            // 数据位 (5/6/7/8)，默认 8
    int stopBits;            // 停止位 (1/2)，默认 1
    int parity;              // 校验 (0=none, 1=even, 2=odd, 3=mark, 4=space)，默认 0
    boolean rtscts;          // 硬件流控 RTS/CTS，默认 false
    boolean xon;             // 软件流控 XON，默认 false
    boolean xoff;            // 软件流控 XOFF，默认 false
    boolean xany;            // 软件流控 XANY，默认 false
};
```

### ISerialServer.idl — 主接口（14 个方法）

| 方法 | 签名 | 说明 |
|------|------|------|
| `QuerySerialDevices` | `[out] SerialPortInfo[]` | 枚举可用串口设备 |
| `OpenSerial` | `[in] SerialPortInfo, [in] SerialConfig, [in] ISerialServiceCallback` | 打开串口并注册回调 |
| `CloseSerial` | `[in] String portName` | 关闭串口 |
| `WriteSerial` | `[in] String, [in] unsigned char[], [in] int timeout, [out] int bytesWritten` | 写入数据 |
| `StartRead` | `[in] String portName` | 启动异步读取 |
| `StopRead` | `[in] String portName` | 停止异步读取 |
| `Flush` | `[in] String portName` | 清空缓冲区 |
| `Drain` | `[in] String portName` | 等待写入完成 |
| `SendBrkSignal` | `[in] String portName` | 发送中断信号 |
| `SetRtsSignal` | `[in] String portName, [in] boolean rts` | 设置RTS信号 |
| `GetCtsSignal` | `[in] String portName, [out] boolean cts` | 获取CTS信号 |
| `SetDtrSignal` | `[in] String portName, [in] boolean dtr` | 设置DTR信号 |
| `GetDsrSignal` | `[in] String portName, [out] boolean dsr` | 获取DSR信号 |
| `AddPortAuthorization` | `[in] String authToken, [in] String deviceId` | 端口授权 |

### ISerialServiceCallback.idl — 数据回调

```idl
[callback] interface OHOS.Serial.ISerialServiceCallback {
    [oneway] void onDataRead([in] String portName, [in] unsigned char[] buffer);
}
```

## ArkTS API

### 使用示例

```typescript
import serial from '@ohos.busManager.serial';

// 获取设备列表
const ports = await serial.getSerialPortList();
// ports: [{ portName: "/dev/ttyS0", manufacturer: "Manufacturer1", ... }, ...]

// 打开串口
await ports[0].open({ baudRate: 115200, dataBits: serial.DataBits.EIGHT });

// 注册数据接收回调
ports[0].onDataRead((data: Uint8Array) => {
    console.log("Received:", data);
});

// 写入数据
const bytesWritten = await ports[0].write(new Uint8Array([0x01, 0x02, 0x03]), 1000);

// 流控制
await ports[0].setRts(true);
const cts = await ports[0].getCts();

// 关闭串口
await ports[0].close();
```

### 导出接口一览

| API | 类型 | 说明 |
|-----|------|------|
| `getSerialPortList()` | Promise | 获取串口设备列表，返回 SerialPort 对象数组 |
| `addPortAuthorization(tokenId, deviceId)` | Promise | 系统级端口授权（@systemapi） |
| `SerialPort.open(config?)` | Promise | 打开串口，config 可选，有默认值 |
| `SerialPort.close()` | Promise | 关闭串口 |
| `SerialPort.write(data, timeout?)` | Promise\<number\> | 写入数据，返回写入字节数 |
| `SerialPort.onDataRead(callback)` | 同步 | 注册数据接收回调 |
| `SerialPort.offDataRead([callback])` | 同步 | 取消回调，不传参则取消全部 |
| `SerialPort.flush()` | Promise | 清空缓冲区 |
| `SerialPort.drain()` | Promise | 等待写入完成 |
| `SerialPort.setRts(enable)` | Promise | 设置 RTS 信号 |
| `SerialPort.sendBrk()` | Promise | 发送中断信号 |
| `SerialPort.getCts()` | Promise\<boolean\> | 获取 CTS 信号状态 |
| `SerialPort.setDtr(enable)` | Promise | 设置 DTR 信号 |
| `SerialPort.getDsr()` | Promise\<boolean\> | 获取 DSR 信号状态 |
| `SerialPort.onDisconnect(callback)` | 同步 | 注册端口断连回调 |
| `SerialPort.offDisconnect([callback])` | 同步 | 取消断连回调 |

### 枚举

```typescript
enum DataBits  { FIVE = 5, SIX = 6, SEVEN = 7, EIGHT = 8 }
enum StopBits  { ONE = 1, TWO = 2 }
enum Parity    { NONE = 'none', EVEN = 'even', ODD = 'odd', MARK = 'mark', SPACE = 'space' }
```

### 错误码

| 错误码 | 含义 |
|--------|------|
| 202 | 非系统应用调用权限拒绝 |
| 203 | 企业管理策略禁止此功能 |
| 35700001 | 服务错误 |
| 35700002 | 无效参数 |
| 35700003 | 虚拟串口已断开 |
| 35700004 | 端口已被占用 |
| 35700005 | 端口未打开 |
| 35700006 | 传输超时 |
| 35700007 | 需要用户授权 |
| 35700008 | 权限拒绝 |
| 401 | 参数错误 |

## 核心实现细节

### 1. 客户端（SerialSrvClient）

- **单例模式**：通过 `SerialSrvClient::GetInstance()` 获取
- **连接管理**：首次调用时通过 `SystemAbilityManagerClient` 懒加载 SA 4202（超时4秒），注册死亡通知自动重连
- **监听器多路复用**：`SerialServiceCallback` 维护 `map<portName, map<handle, callback>>`，支持每端口注册多个JS回调
- **自动启停读取**：首个监听器注册时触发 `StartRead`，最后一个移除时触发 `StopRead`

### 2. 服务端（SerialService）

- **SA注册**：`REGISTER_SYSTEM_ABILITY_BY_ID(SerialService, SERIAL_SYSTEM_ABILITY_ID, true)`；`sa_profile/4202.json` 中配置 `run-on-create=false` 与 `start-on-demand`，实现按需加载、自动重启
- **硬件接入**（`serial_service.cpp`）：通过 HDI 获取 `ISerials` 代理，`QueryDevices` 枚举设备、`OpenDevice` 打开端口、`Write` 下发数据、`StartRead`/`StopRead` 启停异步读，设备离线由 `OnDeviceOffline` 回调通知客户端
- **权限与审计**：`SerialRightManager` 负责授权弹窗与 RDB 持久化，`SerialSecurityReport` 上报串口操作审计事件，`AddPortAuthorization` 供系统应用为指定应用（tokenId）授权指定设备
- **生命周期**：无打开设备时延时 30 秒卸载 SA；客户端进程死亡、设备离线时清理端口上下文
- **测试变体实现**（`test/develop/`）：
  - `serial_service_lifecycle_test.cpp`：模拟设备枚举（多场景轮替）+ 端口占用检测 + 环回读线程
  - `serial_service_errcodetest.cpp`：基于 `TestScenario` 按轮次返回预设错误码，覆盖全部错误码场景
  - `serial_service_paramtest.cpp`：参数校验测试，含环回读线程

### 3. NAPI绑定层

- 所有变更操作使用 `napi_create_async_work` 异步模式（Worker线程执行 + JS线程回调）
- `getSerialPortList` 支持 Promise 调用风格
- `SerialPort` 为JS对象，通过 `napi_wrap` 绑定 `SerialPortNative*`，支持缓存复用/失效/恢复/GC 回收
- `onDataRead` 注册时创建C++ lambda包装回调，通过 `napi_async_work` 调度到JS线程执行

### 4. 错误码映射（utils）

- `serial_error_code.h` 定义内部错误码（`SERIAL_ERR_*`，负数）和JS错误码（`SERIAL_JS_ERR_*`，正数）
- `serial_error_code.cpp` 提供 `SerialInternalToJsCode()` 内部→JS映射和 `SerialJsErrorMessage()` 消息查询
- NAPI 层统一使用 JS 错误码通过 `BusinessError.code` 返回给应用

## 编译说明

### 编译方式

**重要说明**：每次编译必须使用子agent（Task工具，subagent_type为general）执行，不可直接在主会话中运行编译命令。编译命令的工作目录为 `../../../`（即 build.sh 所在目录）。

### 编译命令

#### 完整编译（首次编译、修改了IDL/BUILD.gn/bundle.json时必须使用）

```bash
./build.sh --product-name rk3568 --ccache --build-target serial_manager serial_manager_tests
```

#### 快速编译（仅修改源代码时使用）

**重要说明**：
- 只有在**没有修改** `bundle.json` 和 **BUILD.gn** 的情况下，才能使用 `--fast-rebuild` 选项
- 如果修改了 `bundle.json` 或 `BUILD.gn`，必须使用完整编译命令，**不能使用** `--fast-rebuild`
- 修改IDL文件后可以使用 `--fast-rebuild`
- 使用 `--fast-rebuild` 可以显著提高编译速度，因为它跳过了GN生成阶段，直接使用已有的构建配置

```bash
./build.sh --product-name rk3568 --ccache --fast-rebuild --build-target serial_manager serial_manager_tests
```

### 编译产物

| 产物 | 说明 |
|------|------|
| `libserialsrv_client.z.so` | InnerAPI客户端库 |
| `libserialservice.z.so` | System Ability服务库 |
| `libserial.z.so` | NAPI绑定库（安装路径 `module/busmanager`） |
| `serial_manager_taihe_native.z.so` | Taihe原生库（ANI桥接 + 接口实现） |
| `serial_manager_abc.abc` | Taihe生成ArkTS字节码（安装路径 `/system/framework/`） |

### 测试编译产物

| 产物 | 路径 | 说明 |
|------|------|------|
| `serialservice_errcodetest` | `test/develop:serialservice_errcodetest` | 错误码测试服务，`install_enable = false` |
| `serial_service_lifecycle_test` | `test/develop:serial_service_lifecycle_test` | 生命周期测试服务，`install_enable = false` |

### 系统注册

- 组件注册到 `busmanager` 子系统，部件名 `serial_manager`
- SA ID: 4202
- 进程名: `serial_service`，以 `serial` 用户/组运行
- 按需加载（ondemand），开机完成事件触发启动（`bootevent.boot.completed`），自动重启
- SA配置文件: `sa_profile/4202.json`（run-on-create=false，按需加载；auto-restart=true）
- Init配置: `services/serial_service.cfg`

## IDL 代码生成说明

IDL文件定义了接口和数据类型，编译时由IDL工具链自动生成C++的stub/proxy/types代码。修改IDL文件后使用 `--fast-rebuild` 即可重新生成。

### 生成文件路径

```
out/rk3568/gen/base/busmanager/serial_manager/interfaces/innerkits/
├── iserial_server.h                          # ISerialServer 接口定义
├── serial_server_proxy.h / .cpp              # 客户端代理（序列化请求、发送IPC）
├── serial_server_stub.h / .cpp               # 服务端桩（反序列化请求、分发调用）
├── serial_server_types.h / .cpp              # SerialPortInfo/SerialConfig 序列化
├── iserial_service_callback.h                # 回调接口定义
├── serial_service_callback_proxy.h / .cpp    # 服务端→客户端回调代理
└── serial_service_callback_stub.h / .cpp     # 客户端接收回调桩
```

### 生成文件用途

| 文件 | 用途 | 编译产物 |
|------|------|---------|
| `serial_server_proxy.*` | 客户端通过proxy调用服务端方法 | `serialsrv_client.so` |
| `serial_server_stub.*` | 服务端接收并处理客户端请求 | `serialservice.so` |
| `serial_server_types.*` | 结构体的IPC序列化/反序列化 | 两者都依赖 |
| `serial_service_callback_proxy.*` | 服务端通过proxy回调客户端onDataRead | `serialservice.so` |
| `serial_service_callback_stub.*` | 客户端接收服务端的onDataRead回调 | `serialsrv_client.so` |

### 注意事项

- 生成代码由IDL工具链自动管理，**不应手动编辑**
- 修改IDL文件后，`serial_service.h/cpp` 和 `serial_srv_client.h/cpp` 中的override方法签名需要同步更新
- 如果编译出现签名不匹配错误，先检查生成文件中的接口签名，再对齐手写代码

## 技术特点

1. **遵循OpenHarmony规范**：标准IDL接口定义、System Ability框架、NAPI ArkTS绑定
2. **子系统架构**：子系统名 `busmanager`，部件名 `serial_manager`，可独立编译集成
3. **完整日志系统**：`SERIAL_HILOGI/E/W/D` 宏，支持 CLIENT / SERVICE / NAPI / TAIHE 四模块独立域
4. **回调驱动架构**：`ISerialServiceCallback` 使用 `[oneway]` 异步回调，服务端主动推送数据到客户端
5. **监听器多路复用**：单端口支持注册多个JS回调，自动管理底层读取线程生命周期
6. **统一错误码体系**：内部负数错误码通过 `SerialInternalToJsCode()` 映射为JS层正数错误码，NAPI统一通过 `BusinessError` 抛出

## ArkTS API 声明

API声明文件位于 `interfaces/kits/js/@ohos.busManager.serial.d.ts`，模块名为 `@ohos.busManager.serial`，syscap 为 `SystemCapability.BusManager.Serial`。

## 扩展指南

1. **添加新接口**：在 `ISerialServer.idl` 中定义新方法
2. **实现新功能**：在 `serial_service.cpp` 中实现对应 override 方法
3. **添加NAPI绑定**：在 `serial_info.cpp` 中添加 NAPI 函数并导出
4. **适配新的 HDI 实现**：在 `serial_service.cpp` 中通过 `ISerials` 对接新的串口 HDI，适配不同板卡的 `/dev/ttyS*`、`/dev/ttyUSB*` 设备
5. **新增错误码**：同步更新 `serial_error_code.h/cpp`、`.d.ts` 的 `@throws` 声明

## 当前实现状态

- 完整的目录结构和代码框架
- 3个IDL接口定义和代码生成（ISerialServer + ISerialServiceCallback + SerialServerTypes）
- InnerAPI客户端实现（14 个方法 + 监听器管理 + 死亡通知重连）
- System Ability服务实现（14 个方法，对接 HDI ISerials 实现真实串口 I/O）
- 完整NAPI绑定层（16 个 JS API + 3 个枚举 + SerialPort 对象 + Promise 模式）
- 完整Taihe绑定层（interface SerialPort + 枚举/结构体 + ANI桥接）
- SA配置和注册（SA 4202 + Init配置）
- 编译系统集成，成功编译通过
- ArkTS API声明文件
- 错误码映射模块（内部码↔JS码 + 消息查询）
- 测试：服务变体（develop/ 错误码/生命周期/参数校验）+ 单元测试（unittest/ 客户端/回调/无服务）

## 常见问题

### Q1: 编译时提示 "Duplicate definition"
修改了BUILD.gn后使用了 `--fast-rebuild`。请移除该选项进行完整编译。

### Q2: 如何判断是否需要完整编译？
修改了 `bundle.json` 或任何 `BUILD.gn` → 完整编译；仅修改源代码 → 可用 `--fast-rebuild`。

### Q3: 快速编译能提速多少？
通常节省50-70%编译时间，因为跳过了GN生成阶段。

### Q4: onDataRead 回调没有触发？
主服务已对接真实 HDI 设备，不包含环回逻辑。如需在无硬件环境验证读写与回调，请部署 `serial_service_lifecycle_test` 测试变体（内置环回读线程）。

### Q5: 如何部署测试变体到设备？
将测试变体 so 替换正式 `libserialservice.z.so` 部署到设备对应路径，重启服务即可。
