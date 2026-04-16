# Serial Manager 项目简介

## 项目概述

Serial Manager 是一个基于OpenHarmony系统的串口设备管理服务组件，提供串口设备的发现、打开、关闭、读写、流控制等完整管理功能。该组件参考了OpenHarmony标准USB Manager组件的设计架构，实现了完整的Native到JavaScript的NAPI绑定层，并通过IDL驱动的IPC机制实现Client-Server间通信。

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

```
┌──────────────────────────────────────────────┐
│            JavaScript 应用层                   │
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
│  ReadContext (每端口读线程 + 回调代理)            │
│  当前为模拟实现：硬编码设备 + 环回回显             │
└──────────────────────────────────────────────┘
```

## 项目结构

```
serial_manager/
├── bundle.json                                  # 组件定义
├── serialmgr.gni                                # 全局路径变量
├── disable_seliux.sh                            # 禁用SELinux脚本
├── push.bat                                     # Windows部署脚本
├── interfaces/
│   ├── innerkits/
│   │   ├── ISerialServer.idl                    # 主接口IDL（12个方法）
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
│   └── kits/js/napi/
│       ├── @ohos.busManager.serial.d.ts         # TypeScript API 声明
│       ├── BUILD.gn                             # NAPI构建配置
│       ├── include/
│       │   └── napi_common.h                    # NAPI模块初始化声明
│       └── src/
│           └── serial_info.cpp                  # NAPI完整实现（~1395行）
├── services/
│   ├── BUILD.gn                                 # 服务构建配置
│   ├── serial_service.cfg                       # Init进程服务配置
│   └── native/
│       ├── include/
│       │   └── serial_service.h                 # 服务头文件（含ReadContext）
│       └── src/
│           └── serial_service.cpp               # 服务实现（模拟）
├── sa_profile/
│   ├── 4202.json                                # SA配置文件
│   └── BUILD.gn                                 # SA构建配置
└── utils/
    └── native/
        └── include/
            ├── hilog_wrapper.h                  # 日志宏（CLIENT/SERVICE/NAPI三模块）
            └── delayed_sp_singleton.h           # 延迟单例模板
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

### ISerialServer.idl — 主接口（12个方法）

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
| `AddPortAuthorization` | `[in] String authToken, [in] String deviceId` | 端口授权 |

### ISerialServiceCallback.idl — 数据回调

```idl
[callback] interface OHOS.Serial.ISerialServiceCallback {
    [oneway] void onDataRead([in] String portName, [in] unsigned char[] buffer);
}
```

## JavaScript API

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
const bytesWritten = await ports[0].write([0x01, 0x02, 0x03], 1000);

// 流控制
await ports[0].setRts(true);
const cts = await ports[0].getCts();

// 关闭串口
await ports[0].close();
```

### 导出接口一览

| API | 类型 | 说明 |
|-----|------|------|
| `getSerialPortList()` | Promise / Callback | 获取串口设备列表，返回 SerialPort 对象数组 |
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

### 枚举

```typescript
enum DataBits  { FIVE = 5, SIX = 6, SEVEN = 7, EIGHT = 8 }
enum StopBits  { ONE = 1, TWO = 2 }
enum Parity    { NONE = 'none', EVEN = 'even', ODD = 'odd', MARK = 'mark', SPACE = 'space' }
```

### 错误码

| 错误码 | 含义 |
|--------|------|
| 203 | 权限校验失败 |
| 35700001 | 服务内部错误 |
| 35700002 | 参数无效 |
| 35700003 | 设备未找到 |
| 35700004 | 设备被占用 |
| 35700005 | 设备未打开 |
| 35700006 | 操作超时 |
| 35700007 | 无用户授权 |
| 35700008 | 非授权弹窗应用 |

## 核心实现细节

### 1. 客户端（SerialSrvClient）

- **单例模式**：通过 `SerialSrvClient::GetInstance()` 获取
- **连接管理**：首次调用时通过 `SystemAbilityManagerClient` 懒加载 SA 4202（超时4秒），注册死亡通知自动重连
- **监听器多路复用**：`SerialServiceCallback` 维护 `map<portName, map<handle, callback>>`，支持每端口注册多个JS回调
- **自动启停读取**：首个监听器注册时触发 `StartRead`，最后一个移除时触发 `StopRead`

### 2. 服务端（SerialService）

- **SA注册**：`REGISTER_SYSTEM_ABILITY_BY_ID(SerialService, 4202, true)`，开机自启
- **每端口上下文**：`ReadContext` 包含回调代理、读线程、原子运行标志、条件变量
- **当前模拟实现**：
  - `QuerySerialDevices` 返回3个硬编码设备（`/dev/ttyS0`、`/dev/ttyS1`、`/dev/ttyUSB0`）
  - `WriteSerial` 记录数据并返回写入字节数（无真实硬件I/O）
  - `ReadThreadFunc` 每5秒将最后写入的数据通过 `onDataRead` 回环发送（模拟环回）
  - `Flush` / `Drain` / `SendBrkSignal` / `SetRtsSignal` / `AddPortAuthorization` 为空操作桩
  - `GetCtsSignal` 固定返回 `false`

### 3. NAPI绑定层

- 所有变更操作使用 `napi_create_async_work` 异步模式（Worker线程执行 + JS线程回调）
- `getSerialPortList` 同时支持 Callback 和 Promise 两种调用风格
- `SerialPort` 为JS对象，携带 `portName` 和 `portInfo` 属性及绑定方法
- `onDataRead` 注册时创建C++ lambda包装回调，通过 `napi_async_work` 调度到JS线程执行

## 编译说明

### 编译方式

**重要说明**：每次编译必须使用子agent（Task工具，subagent_type为general）执行，不可直接在主会话中运行编译命令。编译命令的工作目录为 `../../../`（即 build.sh 所在目录）。

### 编译命令

#### 完整编译（首次编译、修改了IDL/BUILD.gn/bundle.json时必须使用）

```bash
./build.sh --product-name rk3568 --ccache --build-target serial_manager
```

#### 快速编译（仅修改源代码时使用）

**重要说明**：
- 只有在**没有修改** `bundle.json` 和 **BUILD.gn** 的情况下，才能使用 `--fast-rebuild` 选项
- 如果修改了 `bundle.json` 或 `BUILD.gn`，必须使用完整编译命令，**不能使用** `--fast-rebuild`
- 修改IDL文件后可以使用 `--fast-rebuild`
- 使用 `--fast-rebuild` 可以显著提高编译速度，因为它跳过了GN生成阶段，直接使用已有的构建配置

```bash
./build.sh --product-name rk3568 --ccache --fast-rebuild --build-target serial_manager
```

### 编译产物

| 产物 | 说明 |
|------|------|
| `libserialsrv_client.z.so` | InnerAPI客户端库 |
| `libserialservice.z.so` | System Ability服务库 |
| `libserial.z.so` | NAPI绑定库（安装路径 `module/busmanager`） |

### 系统注册

- 组件注册到 `busmanager/serial_manager` 子系统
- SA ID: 4202
- 进程名: `serial_service`，以 `serial` 用户/组运行
- 按需加载（ondemand），开机事件触发启动，自动重启
- SA配置文件: `sa_profile/4202.json`
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

1. **遵循OpenHarmony规范**：标准IDL接口定义、System Ability框架、NAPI JavaScript绑定
2. **参考USB Manager设计**：相同目录结构、构建配置、IPC通信模式
3. **子系统架构**：子系统名 `bus`，部件名 `serial_manager`，可独立编译集成
4. **完整日志系统**：`SERIAL_HILOGI/E/W/D` 宏，支持 CLIENT / SERVICE / NAPI 三模块独立域
5. **回调驱动架构**：`ISerialServiceCallback` 使用 `[oneway]` 异步回调，服务端主动推送数据到客户端
6. **监听器多路复用**：单端口支持注册多个JS回调，自动管理底层读取线程生命周期
7. **环回模拟**：当前服务端实现为环回模式，写入数据5秒后通过 `onDataRead` 回显，可用于端到端功能验证

## TypeScript API 声明

API声明文件位于 `interfaces/kits/js/napi/@ohos.busManager.serial.d.ts`，模块名为 `@ohos.busManager.serial`，syscap 为 `SystemCapability.BusManager.Serial`。

## 扩展指南

1. **添加新接口**：在 `ISerialServer.idl` 中定义新方法
2. **实现新功能**：在 `serial_service.cpp` 中实现对应 override 方法
3. **添加NAPI绑定**：在 `serial_info.cpp` 中添加 NAPI 函数并导出
4. **替换模拟实现**：在 `serial_service.cpp` 中将模拟逻辑替换为真实硬件 I/O（操作 `/dev/ttyS*`、`/dev/ttyUSB*`）

## 当前实现状态

- 完整的目录结构和代码框架
- 3个IDL接口定义和代码生成（ISerialServer + ISerialServiceCallback + SerialServerTypes）
- InnerAPI客户端实现（12个方法 + 监听器管理 + 死亡通知重连）
- System Ability服务实现（12个方法，当前为模拟 + 环回）
- 完整NAPI绑定层（12个JS API + 3个枚举 + SerialPort对象 + Promise/Callback双模式）
- SA配置和注册（SA 4202 + Init配置 + SELinux策略）
- 编译系统集成，成功编译通过
- TypeScript API声明文件
- 设备部署脚本（push.bat + disable_seliux.sh）

## 周边依赖

项目依赖以下外部仓库的修改（详见 `周边依赖的PR列表.md`）：
- `security_selinux_adapter` — SELinux 策略
- `startup_init` — Init 系统配置
- `systemabilitymgr_samgr` — SA 管理器注册
- `hiviewdfx_hidumper` — 诊断 dump 支持

## 常见问题

### Q1: 编译时提示 "Duplicate definition"
修改了BUILD.gn后使用了 `--fast-rebuild`。请移除该选项进行完整编译。

### Q2: 如何判断是否需要完整编译？
修改了 `bundle.json` 或任何 `BUILD.gn` → 完整编译；仅修改源代码 → 可用 `--fast-rebuild`。

### Q3: 快速编译能提速多少？
通常节省50-70%编译时间，因为跳过了GN生成阶段。

### Q4: onDataRead 回调没有触发？
确认已调用 `open()` 打开端口并 `write()` 写入数据。当前模拟实现每5秒回显最后写入的数据。

### Q5: 如何在真机上测试？
使用 `push.bat`（Windows）部署编译产物到设备，确保SELinux已设置为permissive（使用 `disable_seliux.sh`）。
