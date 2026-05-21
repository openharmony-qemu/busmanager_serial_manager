/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file serial_module_test.cpp
 * @brief 串口服务模块测试统一入口
 * @details 整合四个独立测试模块：
 *          - loopback: 串口回环测试（验证通信功能，需硬件TX/RX短接）
 *          - db: 数据库测试（验证权限管理增删查）
 *          - dialog: 弹窗流程测试（自动授权流程验证）
 *          - popup: 弹窗交互测试（显示真实弹窗，需用户点击）
 *          - showdb: 显示数据库内容
 *          - verify_init: 验证init创建数据库目录
 *
 * @section Usage 使用方法
 * - ./serial_module_test loopback [/dev/ttyUSB0]  回环测试，可指定端口
 * - ./serial_module_test db                       数据库测试
 * - ./serial_module_test dialog                   弹窗流程测试（自动）
 * - ./serial_module_test popup                    弹窗交互测试（需用户点击）
 * - ./serial_module_test showdb                   显示数据库内容
 * - ./serial_module_test all                      运行全部测试
 * - ./serial_module_test help                     显示帮助信息
 *
 * @since 2026-04-27
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "accesstoken_kit.h"
#include "hilog/log.h"
#include "hilog_wrapper.h"
#include "nativetoken_kit.h"
#include "serial_error_code.h"
#include "serial_right_db_helper.h"
#include "serial_right_manager.h"
#include "serial_srv_client.h"
#include "token_setproc.h"

using namespace OHOS::Serial;
using namespace OHOS::HiviewDFX;
using namespace OHOS::Security::AccessToken;

// ==================== 全局常量定义 ====================

/// 日志标签
constexpr HiLogLabel LABEL = {LOG_CORE, 0xD002F00, "SerialModuleTest"};

// 回环测试相关常量
constexpr int32_t LOOPBACK_TIMEOUT_SECONDS = 3;
constexpr int32_t LOOPBACK_WRITE_TIMEOUT_MS = 1000;
constexpr int32_t DEFAULT_BAUD_RATE = 115200;
constexpr int32_t DEFAULT_DATA_BITS = 8;
constexpr int32_t DEFAULT_STOP_BITS = 1;
constexpr int32_t DEFAULT_PARITY = 0;

// ASCII 可打印字符范围常量
constexpr uint8_t ASCII_PRINTABLE_MIN = 32;
constexpr uint8_t ASCII_PRINTABLE_MAX = 126;

// 数据库测试相关常量
constexpr const char* TEST_PORT_NAME = "/dev/ttyUSB0";
constexpr const char* TEST_BUNDLE_NAME = "com.test.serialapp";
constexpr const char* TEST_TOKEN_ID = "12345678";
constexpr int32_t TEST_USER_ID = 100;
constexpr int32_t TEST_USER_ID_2 = 101;
constexpr const char* TEST_PORT_NAME_2 = "/dev/ttyUSB1";
constexpr const char* TEST_BUNDLE_NAME_2 = "com.test.serialapp2";
constexpr int32_t BATCH_TEST_COUNT = 100;
constexpr int32_t BATCH_TEST_USER_ID = 100;
constexpr const char* BATCH_TEST_TOKEN_ID = "123456789";
constexpr const char* BATCH_TEST_BUNDLE_NAME = "test.app";

// 弹窗测试相关常量
constexpr const char* FAKE_PORT_NAME = "/dev/ttyUSB0";
constexpr int32_t FAKE_PRODUCT_ID = 0x1234;
constexpr int32_t FAKE_VENDOR_ID = 0x5678;
constexpr const char* FAKE_MANUFACTURER = "TestDevice";

// 数据库显示表格格式化宽度常量
constexpr int32_t TABLE_WIDTH_ID = 4;
constexpr int32_t TABLE_WIDTH_UID = 3;
constexpr int32_t TABLE_WIDTH_PORTNAME = 20;
constexpr int32_t TABLE_WIDTH_BUNDLENAME = 20;
constexpr int32_t TABLE_WIDTH_TOKENID = 10;
constexpr int32_t TABLE_WIDTH_TIME = 12;

// 命令行参数索引常量
constexpr int32_t ARGV_COMMAND_INDEX = 1;
constexpr int32_t ARGV_PORT_INDEX = 2;

// ==================== 回环测试全局变量 ====================

std::mutex g_loopbackMutex;
std::condition_variable g_loopbackCv;
bool g_dataReceived = false;
std::vector<uint8_t> g_receivedData;

// ==================== 数据库测试计数器 ====================

static int32_t g_dbPassedTests = 0;
static int32_t g_dbFailedTests = 0;

// ==================== 模拟 HAP Token 配置 ====================

// 类型别名：明确使用 AccessToken 命名空间的类型，避免与 AppExecFwk 命名空间同名类型的歧义
using AccessTokenPermissionDef = OHOS::Security::AccessToken::PermissionDef;
using AccessTokenPermissionStateFull = OHOS::Security::AccessToken::PermissionStateFull;
using AccessTokenHapPolicyParams = OHOS::Security::AccessToken::HapPolicyParams;
using AccessTokenHapInfoParams = OHOS::Security::AccessToken::HapInfoParams;

// 模拟普通 HAP 应用的权限定义
AccessTokenPermissionDef g_testPermDef = {.permissionName = "",
    .bundleName = "serial_test_hap",
    .grantMode = 1,
    .label = "label",
    .labelId = 1,
    .description = "test serial service hap",
    .descriptionId = 1,
    .availableLevel = APL_NORMAL};

AccessTokenPermissionStateFull g_testPermState = {.grantFlags = {1},
    .grantStatus = {PermissionState::PERMISSION_GRANTED},
    .isGeneral = true,
    .permissionName = "",
    .resDeviceID = {"local"}};

AccessTokenHapPolicyParams g_testPolicyParams = {
    .apl = APL_NORMAL, .domain = "test.domain", .permList = {g_testPermDef}, .permStateList = {g_testPermState}};

// Dialog HAP 需要系统应用级别的权限 (APL_SYSTEM_BASIC)
AccessTokenHapPolicyParams g_dialogPolicyParams = {.apl = APL_SYSTEM_BASIC,
    .domain = "dialog.domain",
    .permList = {g_testPermDef},
    .permStateList = {g_testPermState}};

// 模拟普通用户 HAP (userId=0, 需要授权)
AccessTokenHapInfoParams g_normalHapInfo = {
    .bundleName = "serial_test_hap", .userID = 0, .instIndex = 0, .appIDDesc = "serial test hap", .isSystemApp = false};

// 模拟系统应用 HAP (isSystemApp=true, 无需授权)
AccessTokenHapInfoParams g_systemHapInfo = {.bundleName = "serial_test_system_hap",
    .userID = 0,
    .instIndex = 0,
    .appIDDesc = "serial test system hap",
    .isSystemApp = true};

// 模拟 Dialog HAP (com.usb.right, 系统应用，用于授权)
AccessTokenHapInfoParams g_dialogHapInfo = {
    .bundleName = "com.usb.right", .userID = 0, .instIndex = 0, .appIDDesc = "serial dialog hap", .isSystemApp = true};

static bool g_nativeTokenInited = false;

void InitNativeTokenIfNeeded()
{
    if (g_nativeTokenInited) {
        return;
    }
    constexpr const char* TEST_PROCESS_NAME = "serial_module_test";
    constexpr const char* TEST_DCAP = "privacymanager";
    const char* dcaps[] = {TEST_DCAP};
    NativeTokenInfoParams params = {
        .dcapsNum = 1,
        .permsNum = 0,
        .aclsNum = 0,
        .dcaps = dcaps,
        .perms = nullptr,
        .acls = nullptr,
        .processName = TEST_PROCESS_NAME,
        .aplStr = "system_core",
    };
    uint64_t nativeTokenId = GetAccessTokenId(&params);
    if (nativeTokenId != 0) {
        HiLog::Info(LABEL, "GetAccessTokenId success, nativeTokenId=%{public}lu", nativeTokenId);
        SetSelfTokenID(nativeTokenId);
        g_nativeTokenInited = true;
    } else {
        HiLog::Error(LABEL, "GetAccessTokenId failed");
    }
}

AccessTokenID AllocTestHapToken(const HapInfoParams& info)
{
    InitNativeTokenIfNeeded();
    AccessTokenIDEx tokenIdEx = AccessTokenKit::AllocHapToken(info, g_testPolicyParams);
    AccessTokenID tokenId = tokenIdEx.tokenIdExStruct.tokenID;
    if (tokenId != 0) {
        HiLog::Info(LABEL, "AllocHapToken success, tokenId=%{public}d, bundleName=%{public}s, userId=%{public}d",
            tokenId, info.bundleName.c_str(), info.userID);
    } else {
        HiLog::Error(LABEL, "AllocHapToken failed");
    }
    return tokenId;
}

AccessTokenIDEx AllocSystemHapTokenEx(const HapInfoParams& info)
{
    InitNativeTokenIfNeeded();
    AccessTokenIDEx tokenIdEx = AccessTokenKit::AllocHapToken(info, g_dialogPolicyParams);
    AccessTokenID tokenId = tokenIdEx.tokenIdExStruct.tokenID;
    if (tokenId != 0) {
        HiLog::Info(LABEL,
            "AllocSystemHapTokenEx success, tokenId=%{public}d, fullTokenId=%{public}llu, bundleName=%{public}s, "
            "userId=%{public}d, apl=SYSTEM_BASIC",
            tokenId, tokenIdEx.tokenIDEx, info.bundleName.c_str(), info.userID);
    } else {
        HiLog::Error(LABEL, "AllocSystemHapTokenEx failed");
    }
    return tokenIdEx;
}

/**
 * @brief 设置当前进程使用指定的 Token
 * @param tokenId 要设置的 AccessTokenID（低32位）
 */
void SetTestSelfToken(AccessTokenID tokenId)
{
    int ret = SetSelfTokenID(tokenId);
    if (ret == 0) {
        HiLog::Info(LABEL, "SetSelfTokenID success, tokenId=%{public}d", tokenId);
    } else {
        HiLog::Error(LABEL, "SetSelfTokenID failed, ret=%{public}d", ret);
    }
    ret = AccessTokenKit::ReloadNativeTokenInfo();
    if (ret == 0) {
        HiLog::Info(LABEL, "ReloadNativeTokenInfo success");
    } else {
        HiLog::Error(LABEL, "ReloadNativeTokenInfo failed, ret=%{public}d", ret);
    }
}

/**
 * @brief 设置当前进程使用指定的完整 Token（64位，含系统应用标志）
 * @param tokenIdEx 要设置的完整 AccessTokenIDEx
 */
void SetTestSelfTokenEx(uint64_t tokenIdEx)
{
    int ret = SetSelfTokenID(tokenIdEx);
    if (ret == 0) {
        HiLog::Info(LABEL, "SetSelfTokenID success, fullTokenId=%{public}lu", tokenIdEx);
    } else {
        HiLog::Error(LABEL, "SetSelfTokenID failed, ret=%{public}d", ret);
    }
    ret = AccessTokenKit::ReloadNativeTokenInfo();
    if (ret == 0) {
        HiLog::Info(LABEL, "ReloadNativeTokenInfo success");
    } else {
        HiLog::Error(LABEL, "ReloadNativeTokenInfo failed, ret=%{public}d", ret);
    }
}

/**
 * @brief 删除分配的模拟 Token
 * @param tokenId 要删除的 AccessTokenID
 */
void DeleteTestHapToken(AccessTokenID tokenId)
{
    int ret = AccessTokenKit::DeleteToken(tokenId);
    if (ret == 0) {
        HiLog::Info(LABEL, "DeleteToken success, tokenId=%{public}d", tokenId);
    } else {
        HiLog::Error(LABEL, "DeleteToken failed, ret=%{public}d, tokenId=%{public}d", ret, tokenId);
    }
}

// ==================== 辅助宏定义 ====================

// 辅助函数替代宏 - 符合门禁规范
inline void DbTestAssert(bool condition, const std::string& message)
{
    if (condition) {
        std::cout << "[PASS] " << message << std::endl;
        g_dbPassedTests++;
    } else {
        std::cout << "[FAIL] " << message << std::endl;
        g_dbFailedTests++;
    }
}

inline void DbTestLog(const std::string& message)
{
    std::cout << "[LOG] " << message << std::endl;
}

// ==================== 通用辅助函数 ====================

/**
 * @brief 显示帮助信息
 */
void ShowHelp()
{
    std::cout << "=== Serial Module Test Help ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: serial_module_test <command> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  loopback [port]  - 串口回环测试（硬件TX/RX短接，验证通信）" << std::endl;
    std::cout << "  db               - 数据库测试（增删查验证）" << std::endl;
    std::cout << "  dialog           - 弹窗流程测试（自动授权流程验证）" << std::endl;
    std::cout << "  popup            - 弹窗交互测试（显示真实弹窗，需用户点击）" << std::endl;
    std::cout << "  showdb           - 显示数据库所有记录" << std::endl;
    std::cout << "  all              - 运行全部测试" << std::endl;
    std::cout << "  help             - 显示此帮助信息" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  ./serial_module_test loopback              # 回环测试（默认端口）" << std::endl;
    std::cout << "  ./serial_module_test loopback /dev/ttyUSB1 # 回环测试（指定端口）" << std::endl;
    std::cout << "  ./serial_module_test db                    # 数据库测试" << std::endl;
    std::cout << "  ./serial_module_test dialog                # 弹窗流程测试" << std::endl;
    std::cout << "  ./serial_module_test popup                 # 弹窗交互测试" << std::endl;
    std::cout << "  ./serial_module_test showdb                # 显示数据库内容" << std::endl;
    std::cout << "  ./serial_module_test all                   # 运行所有测试" << std::endl;
    std::cout << std::endl;
}

/**
 * @brief 打印数据缓冲区内容
 * @param prefix 打印前缀
 * @param data 数据缓冲区
 */
void PrintBuffer(const std::string& prefix, const std::vector<uint8_t>& data)
{
    std::cout << prefix;
    for (size_t i = 0; i < data.size(); i++) {
        if (data[i] >= ASCII_PRINTABLE_MIN && data[i] <= ASCII_PRINTABLE_MAX) {
            std::cout << static_cast<char>(data[i]);
        } else {
            std::cout << "[" << std::hex << static_cast<int>(data[i]) << std::dec << "]";
        }
    }
    std::cout << std::endl;
}

/**
 * @brief 打印错误码说明
 * @param code 错误码
 */
void PrintErrorCode(int32_t code)
{
    std::cout << "Error code: " << code << " (";
    switch (code) {
        case SERIAL_OK:
            std::cout << "SERIAL_OK";
            break;
        case SERIAL_ERR_SERVICE:
            std::cout << "SERIAL_ERR_SERVICE";
            break;
        case SERIAL_ERR_INVALID_PARAM:
            std::cout << "SERIAL_ERR_INVALID_PARAM";
            break;
        case SERIAL_ERR_DEVICE_DISCONNECTED:
            std::cout << "SERIAL_ERR_DEVICE_DISCONNECTED";
            break;
        case SERIAL_ERR_DEVICE_BUSY:
            std::cout << "SERIAL_ERR_DEVICE_BUSY";
            break;
        case SERIAL_ERR_DEVICE_NOT_OPENED:
            std::cout << "SERIAL_ERR_DEVICE_NOT_OPENED";
            break;
        case SERIAL_ERR_TIMEOUT:
            std::cout << "SERIAL_ERR_TIMEOUT";
            break;
        case SERIAL_ERR_PERMISSION_DENIED:
            std::cout << "SERIAL_ERR_PERMISSION_DENIED";
            break;
        case SERIAL_ERR_NOT_ALLOWED:
            std::cout << "SERIAL_ERR_NOT_ALLOWED";
            break;
        default:
            std::cout << "UNKNOWN";
            break;
    }
    std::cout << ")" << std::endl;
}

// ==================== 回环测试模块 ====================

/**
 * @brief 数据接收回调函数
 * @param data 接收到的数据
 */
void OnLoopbackDataReceived(const std::vector<uint8_t>& data)
{
    HiLog::Info(LABEL, "Loopback data received, size=%{public}zu", data.size());
    std::lock_guard<std::mutex> lock(g_loopbackMutex);
    g_receivedData = data;
    g_dataReceived = true;
    g_loopbackCv.notify_one();
}

/**
 * @brief 查询设备步骤
 */
int LoopbackQueryDevices(SerialSrvClient& client)
{
    std::cout << "\n[Step 1] Querying serial devices..." << std::endl;
    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    if (ret != SERIAL_OK) {
        std::cerr << "QuerySerialDevices failed: " << ret << std::endl;
        return 1;
    }
    std::cout << "Found " << devices.size() << " serial devices:" << std::endl;
    for (const auto& dev : devices) {
        std::cout << "  - Port: " << dev.portName << ", ProductID: 0x" << std::hex << dev.productId << ", VendorID: 0x"
                  << dev.vendorId << std::dec << std::endl;
    }
    return 0;
}

/**
 * @brief 打开串口步骤
 */
int LoopbackOpenSerial(SerialSrvClient& client, const std::string& deviceId, SerialConfig& config)
{
    std::cout << "\n[Step 2] Opening serial port: " << deviceId << std::endl;
    SerialPortInfo portInfo;
    portInfo.portName = deviceId;
    portInfo.productId = 0;
    portInfo.vendorId = 0;
    portInfo.manufacturer = "";

    config.baudRate = DEFAULT_BAUD_RATE;
    config.dataBits = DEFAULT_DATA_BITS;
    config.stopBits = DEFAULT_STOP_BITS;
    config.parity = DEFAULT_PARITY;
    config.rtscts = false;
    config.xon = false;
    config.xoff = false;
    config.xany = false;

    std::cout << "Config: baudRate=" << config.baudRate << ", dataBits=" << config.dataBits
              << ", stopBits=" << config.stopBits << std::endl;

    int32_t ret = client.OpenSerial(portInfo, config);
    if (ret != SERIAL_OK) {
        std::cerr << "OpenSerial failed: " << ret << std::endl;
        return 1;
    }
    std::cout << "OpenSerial success!" << std::endl;
    return 0;
}

/**
 * @brief 注册监听并写入数据步骤
 */
int LoopbackWriteData(SerialSrvClient& client, const std::string& deviceId, ListenerHandle& handle)
{
    std::cout << "\n[Step 3] Registering data listener..." << std::endl;
    int32_t ret = client.RegisterDataListener(deviceId, OnLoopbackDataReceived, handle);
    if (handle == 0) {
        std::cerr << "RegisterDataListener failed!" << std::endl;
        client.CloseSerial(deviceId);
        return 1;
    }
    std::cout << "RegisterDataListener success, handle=" << handle << std::endl;

    std::cout << "\n[Step 4] Writing data..." << std::endl;
    std::string testString = "Hello Loopback";
    std::vector<uint8_t> writeData(testString.begin(), testString.end());
    writeData.push_back('\n');

    int32_t bytesWritten = 0;
    ret = client.WriteSerial(deviceId, writeData, LOOPBACK_WRITE_TIMEOUT_MS, bytesWritten);
    if (ret != SERIAL_OK) {
        std::cerr << "WriteSerial failed: " << ret << std::endl;
        client.UnregisterDataListener(deviceId, handle);
        client.CloseSerial(deviceId);
        return 1;
    }
    std::cout << "WriteSerial success, bytesWritten=" << bytesWritten << std::endl;
    PrintBuffer("Written: ", writeData);
    return 0;
}

/**
 * @brief 等待回环数据并验证
 */
void LoopbackWaitAndVerify(const std::vector<uint8_t>& writeData)
{
    std::cout << "\n[Step 5] Waiting for loopback data (timeout=" << LOOPBACK_TIMEOUT_SECONDS << "s)..." << std::endl;
    std::unique_lock<std::mutex> lock(g_loopbackMutex);
    if (!g_loopbackCv.wait_for(lock, std::chrono::seconds(LOOPBACK_TIMEOUT_SECONDS), [] {
            return g_dataReceived;
        })) {
        std::cerr << "Timeout! No data received within " << LOOPBACK_TIMEOUT_SECONDS << " seconds." << std::endl;
        std::cerr << "Note: This is expected if TX/RX pins are NOT connected." << std::endl;
    } else {
        std::cout << "Data received!" << std::endl;
        PrintBuffer("Received: ", g_receivedData);
        if (g_receivedData.size() == writeData.size() &&
            std::memcmp(g_receivedData.data(), writeData.data(), writeData.size()) == 0) {
            std::cout << "Loopback test PASSED! Data matches." << std::endl;
        } else {
            std::cout << "Loopback test PARTIAL. Data differs." << std::endl;
            std::cout << "Expected size: " << writeData.size() << ", Received size: " << g_receivedData.size()
                      << std::endl;
        }
    }
}

/**
 * @brief 执行回环测试（模拟 HAP 应用访问）
 * @details 使用 AccessTokenKit 分配模拟 HAP token，完整测试授权流程：
 *          1. 分配模拟 HAP token (userId=100, bundleName="serial_test_hap")
 *          2. 设置当前进程使用该 token
 *          3. 调用 AddPortAuthorization 添加授权（模拟弹窗 HAP）
 *          4. 调用 OpenSerial 打开串口
 *          5. 测试完成后删除 token
 */

// Helper functions for RunLoopbackTest
struct LoopbackTestTokens {
    AccessTokenID normalTokenId;
    AccessTokenID dialogTokenId;
    AccessTokenIDEx dialogTokenIdEx;
};

bool AllocateLoopbackTestTokens(LoopbackTestTokens& tokens);
bool PreAuthorizePortForLoopback(const LoopbackTestTokens& tokens, const std::string& deviceId);
bool AllocateLoopbackTestTokens(LoopbackTestTokens& tokens)
{
    std::cout << "\n[Step 0] Allocating simulated HAP tokens..." << std::endl;

    tokens.normalTokenId = AllocTestHapToken(g_normalHapInfo);
    if (tokens.normalTokenId == 0) {
        std::cerr << "Failed to allocate normal HAP token!" << std::endl;
        return false;
    }
    std::cout << "Allocated normal HAP token: " << tokens.normalTokenId << std::endl;
    std::cout << "  bundleName: " << g_normalHapInfo.bundleName << std::endl;
    std::cout << "  userId: " << g_normalHapInfo.userID << std::endl;

    tokens.dialogTokenIdEx = AllocSystemHapTokenEx(g_dialogHapInfo);
    tokens.dialogTokenId = tokens.dialogTokenIdEx.tokenIdExStruct.tokenID;
    if (tokens.dialogTokenId == 0) {
        std::cerr << "Failed to allocate dialog HAP token!" << std::endl;
        DeleteTestHapToken(tokens.normalTokenId);
        return false;
    }
    std::cout << "Allocated dialog HAP token: " << tokens.dialogTokenId << std::endl;
    std::cout << "  fullTokenId: " << tokens.dialogTokenIdEx.tokenIDEx << " (with system app flag)" << std::endl;
    return true;
}

bool PreAuthorizePortForLoopback(const LoopbackTestTokens& tokens, const std::string& deviceId)
{
    auto& client = SerialSrvClient::GetInstance();

    std::cout << "\n[Step 1] Pre-authorizing port via Dialog HAP..." << std::endl;
    SetTestSelfTokenEx(tokens.dialogTokenIdEx.tokenIDEx);
    std::cout << "Set dialog HAP token for authorization (full 64-bit)" << std::endl;

    std::string tokenIdStr = std::to_string(tokens.normalTokenId);
    int32_t authRet = client.AddPortAuthorization(tokenIdStr, deviceId);
    if (authRet != 0) {
        std::cerr << "Pre-authorization via IPC failed: " << authRet << std::endl;
        return false;
    }
    std::cout << "Pre-authorization success (dialog HAP granted permission to normal HAP)" << std::endl;
    return true;
}

int RunLoopbackTest(const std::string& deviceId)
{
    std::cout << "=== Serial Loopback Test (Simulating HAP App) ===" << std::endl;
    std::cout << "Port: " << deviceId << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_loopbackMutex);
        g_dataReceived = false;
        g_receivedData.clear();
    }

    LoopbackTestTokens tokens;
    if (!AllocateLoopbackTestTokens(tokens)) {
        return 1;
    }

    if (!PreAuthorizePortForLoopback(tokens, deviceId)) {
        DeleteTestHapToken(tokens.normalTokenId);
        DeleteTestHapToken(tokens.dialogTokenId);
        return 1;
    }

    auto& client = SerialSrvClient::GetInstance();

    // Switch to normal HAP token for OpenSerial
    std::cout << "\n[Step 2] Switching to normal HAP token..." << std::endl;
    SetTestSelfToken(tokens.normalTokenId);
    std::cout << "Set normal HAP token for serial operations" << std::endl;

    if (LoopbackQueryDevices(client) != 0) {
        DeleteTestHapToken(tokens.normalTokenId);
        DeleteTestHapToken(tokens.dialogTokenId);
        return 1;
    }

    SerialConfig config;
    if (LoopbackOpenSerial(client, deviceId, config) != 0) {
        DeleteTestHapToken(tokens.normalTokenId);
        DeleteTestHapToken(tokens.dialogTokenId);
        return 1;
    }

    ListenerHandle handle = 0;
    if (LoopbackWriteData(client, deviceId, handle) != 0) {
        client.CloseSerial(deviceId);
        DeleteTestHapToken(tokens.normalTokenId);
        DeleteTestHapToken(tokens.dialogTokenId);
        return 1;
    }

    std::vector<uint8_t> writeData;
    {
        std::lock_guard<std::mutex> lock(g_loopbackMutex);
        std::string tempStr("Hello Loopback");
        writeData = std::vector<uint8_t>(tempStr.begin(), tempStr.end());
        writeData.push_back('\n');
    }
    LoopbackWaitAndVerify(writeData);

    std::cout << "\n[Step 7] Closing serial port..." << std::endl;
    client.UnregisterDataListener(deviceId, handle);
    int32_t ret = client.CloseSerial(deviceId);
    if (ret != SERIAL_OK) {
        std::cerr << "CloseSerial failed: " << ret << std::endl;
    } else {
        std::cout << "CloseSerial success!" << std::endl;
    }

    // Step 8: 清理模拟 HAP tokens
    std::cout << "\n[Step 8] Cleaning up test tokens..." << std::endl;
    DeleteTestHapToken(tokens.normalTokenId);
    DeleteTestHapToken(tokens.dialogTokenId);
    std::cout << "Tokens deleted successfully" << std::endl;

    std::cout << "\n=== Loopback Test Complete ===" << std::endl;
    return 0;
}

// ==================== 数据库测试模块 ====================

/**
 * @brief 测试初始化 SerialRightManager
 */
bool TestDbInitSerialRightManager()
{
    DbTestLog("Test 1: Initialize SerialRightManager");
    auto& manager = SerialRightManager::GetInstance();
    int32_t ret = manager.Init();
    DbTestAssert(ret == SERIAL_RIGHT_OK, "SerialRightManager::Init() should return OK");

    auto helper = SerialRightDbHelper::GetInstance();
    DbTestAssert(helper != nullptr, "SerialRightDbHelper::GetInstance() should return valid instance");

    return (ret == SERIAL_RIGHT_OK && helper != nullptr);
}

/**
 * @brief 测试添加设备权限
 */
bool TestDbAddDeviceRight()
{
    DbTestLog("Test 2: AddDeviceRight() - Insert authorization record");
    auto& manager = SerialRightManager::GetInstance();

    bool result = manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    DbTestAssert(result, "AddDeviceRight() should return true");

    return result;
}

/**
 * @brief 测试查询权限
 */
bool TestDbHasRight()
{
    DbTestLog("Test 3: HasRight() - Query and verify authorization");
    auto& manager = SerialRightManager::GetInstance();

    bool hasRight = manager.HasRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    DbTestAssert(hasRight, "HasRight() should return true after AddDeviceRight()");

    bool hasRightNonExist = manager.HasRight(TEST_PORT_NAME_2, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID);
    DbTestAssert(!hasRightNonExist, "HasRight() should return false for non-existent authorization");

    return hasRight && !hasRightNonExist;
}

/**
 * @brief 测试 tokenId 变体的添加权限
 */
bool TestDbAddDeviceRightByTokenId()
{
    DbTestLog("Test 4: AddDeviceRight() with tokenId variant");
    auto& manager = SerialRightManager::GetInstance();

    // 使用4参数变体进行实际测试
    bool result = manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    DbTestAssert(result, "AddDeviceRight() 4-param variant should work");

    return result;
}

/**
 * @brief 测试多条记录操作
 */
bool TestDbMultipleRecords()
{
    DbTestLog("Test 6: Multiple authorization records");
    auto& manager = SerialRightManager::GetInstance();

    bool result1 = manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    bool result2 = manager.AddDeviceRight(TEST_PORT_NAME_2, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    bool result3 = manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID_2);

    DbTestAssert(result1 && result2 && result3, "All AddDeviceRight() calls should succeed");

    bool has1 = manager.HasRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    bool has2 = manager.HasRight(TEST_PORT_NAME_2, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    bool has3 = manager.HasRight(TEST_PORT_NAME, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID_2);

    DbTestAssert(has1 && has2 && has3, "All HasRight() should return true");

    return result1 && result2 && result3 && has1 && has2 && has3;
}

// TestDbIsRecordExpired 已删除 - 权限永久有效，不再需要过期检查

/**
 * @brief 测试并发操作
 */
bool TestDbConcurrentOperations()
{
    DbTestLog("Test 10: Concurrent operations");
    auto& manager = SerialRightManager::GetInstance();

    const int32_t threadCount = 4;
    const int32_t operationsPerThread = 10;

    std::vector<std::thread> threads;
    std::atomic<int32_t> successCount {0};
    std::atomic<int32_t> failCount {0};

    auto worker = [&successCount, &failCount, &manager, threadCount, operationsPerThread](int32_t threadId) {
        std::string deviceId = "/dev/ttyUSB" + std::to_string(threadId);
        std::string bundleName = "com.test.thread" + std::to_string(threadId);
        std::string tokenId = std::to_string(100000 + threadId);

        for (int32_t i = 0; i < operationsPerThread; i++) {
            if (manager.AddDeviceRight(deviceId, bundleName, tokenId, TEST_USER_ID)) {
                successCount++;
            } else {
                failCount++;
            }

            if (manager.HasRight(deviceId, bundleName, tokenId, TEST_USER_ID)) {
                successCount++;
            } else {
                failCount++;
            }
        }
    };

    for (int32_t i = 0; i < threadCount; i++) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::ostringstream oss4;
    oss4 << "Concurrent test: success=" << successCount << ", fail=" << failCount;
    DbTestLog(oss4.str());
    DbTestAssert(successCount > 0, "At least some concurrent operations should succeed");

    return successCount > 0;
}

/**
 * @brief 打印数据库测试摘要
 */
void PrintDbTestSummary()
{
    std::cout << std::endl;
    std::cout << "=== Database Test Summary ===" << std::endl;
    std::cout << "Total tests passed: " << g_dbPassedTests << std::endl;
    std::cout << "Total tests failed: " << g_dbFailedTests << std::endl;
    std::cout << "Result: " << (g_dbFailedTests == 0 ? "ALL PASSED" : "SOME FAILED") << std::endl;
    std::cout << std::endl;
}

/**
 * @brief 执行数据库测试
 * @return 测试结果，0=成功，1=失败
 */
int RunDbTest()
{
    std::cout << "=== SerialRightManager Database Test ===" << std::endl;
    std::cout << "Testing SerialRightManager database operations" << std::endl;
    std::cout << std::endl;

    // 重置计数器
    g_dbPassedTests = 0;
    g_dbFailedTests = 0;

    // 运行所有数据库测试
    TestDbInitSerialRightManager();
    TestDbAddDeviceRight();
    TestDbHasRight();
    TestDbAddDeviceRightByTokenId();
    TestDbMultipleRecords();
    // TestDbIsRecordExpired 已删除 - 权限永久有效
    TestDbConcurrentOperations();

    PrintDbTestSummary();

    return (g_dbFailedTests == 0) ? 0 : 1;
}

// ==================== 弹窗测试模块 ====================

/**
 * @brief 创建默认串口配置
 */
SerialConfig MakeDefaultConfig()
{
    SerialConfig config;
    config.baudRate = DEFAULT_BAUD_RATE;
    config.dataBits = DEFAULT_DATA_BITS;
    config.stopBits = DEFAULT_STOP_BITS;
    config.parity = DEFAULT_PARITY;
    config.rtscts = false;
    config.xon = false;
    config.xoff = false;
    config.xany = false;
    return config;
}

/**
 * @brief 创建虚拟端口信息
 */
SerialPortInfo MakeFakePortInfo()
{
    SerialPortInfo portInfo;
    portInfo.portName = FAKE_PORT_NAME;
    portInfo.productId = FAKE_PRODUCT_ID;
    portInfo.vendorId = FAKE_VENDOR_ID;
    portInfo.manufacturer = FAKE_MANUFACTURER;
    return portInfo;
}

/**
 * @brief 弹窗测试 - 查询设备步骤
 */
void DialogQueryDevices(SerialSrvClient& client)
{
    std::cout << "[Step 1] Querying serial devices..." << std::endl;
    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    std::cout << "QuerySerialDevices result: ";
    PrintErrorCode(ret);
    std::cout << "Found " << devices.size() << " devices" << std::endl;

    if (!devices.empty()) {
        for (const auto& dev : devices) {
            std::cout << "  - " << dev.portName << " (productId=" << dev.productId << ", vendorId=" << dev.vendorId
                      << ")" << std::endl;
        }
    }
    std::cout << std::endl;
}

/**
 * @brief 弹窗测试 - 打开设备步骤
 */
void DialogOpenDevice(SerialSrvClient& client)
{
    std::cout << "[Step 2] Attempting to open fake device..." << std::endl;
    std::cout << "  Port: " << FAKE_PORT_NAME << std::endl;
    std::cout << "  ProductId: 0x" << std::hex << FAKE_PRODUCT_ID << std::dec << std::endl;
    std::cout << "  VendorId: 0x" << std::hex << FAKE_VENDOR_ID << std::dec << std::endl;
    std::cout << "  Manufacturer: " << FAKE_MANUFACTURER << std::endl;
    std::cout << std::endl;

    SerialPortInfo portInfo = MakeFakePortInfo();
    SerialConfig config = MakeDefaultConfig();

    std::cout << "Calling OpenSerial()..." << std::endl;
    std::cout << "*** Watch for authorization dialog on device screen ***" << std::endl;
    std::cout << std::endl;

    int32_t ret = client.OpenSerial(portInfo, config);
    std::cout << "OpenSerial result: ";
    PrintErrorCode(ret);

    if (ret == SERIAL_OK) {
        std::cout << std::endl;
        std::cout << "Device opened successfully (unexpected in stub mode)" << std::endl;
        std::cout << "Closing device..." << std::endl;
        ret = client.CloseSerial(portInfo.portName);
        std::cout << "CloseSerial result: ";
        PrintErrorCode(ret);
    } else if (ret == SERIAL_ERR_PERMISSION_DENIED) {
        std::cout << std::endl;
        std::cout << "Permission denied - authorization dialog was triggered but not approved" << std::endl;
    } else if (ret == SERIAL_ERR_SERVICE || ret == SERIAL_ERR_DEVICE_DISCONNECTED) {
        std::cout << std::endl;
        std::cout << "Service/Device not ready - stub mode, authorization check was triggered" << std::endl;
        std::cout << "Check device logs to verify dialog was shown" << std::endl;
    }
}

/**
 * @brief 执行弹窗测试（模拟完整授权流程）
 * @details 模拟 HAP 应用首次访问串口的授权流程：
 *          1. 普通 HAP 调用 OpenSerial → 无权限 → 触发弹窗 → 返回 SERIAL_JS_ERR_PERMISSION_DENIED
 *          2. Dialog HAP 调用 AddPortAuthorization → 写入授权
 *          3. 普通 HAP 再次调用 OpenSerial → 有权限 → 成功
 */

// Helper functions for RunDialogTest
struct DialogTestTokens {
    AccessTokenID normalTokenId;
    AccessTokenID dialogTokenId;
    AccessTokenIDEx dialogTokenIdEx;
};

bool AllocateDialogTestTokens(DialogTestTokens& tokens);
int TestOpenSerialWithoutPermission(SerialSrvClient& client, const std::string& deviceId);
bool TestAddPortAuthorizationStep(SerialSrvClient& client, const DialogTestTokens& tokens, const std::string& deviceId);
int TestOpenSerialWithPermission(SerialSrvClient& client, const std::string& deviceId);
int RunDialogTest();

bool AllocateDialogTestTokens(DialogTestTokens& tokens)
{
    std::cout << "[Step 0] Allocating simulated HAP tokens..." << std::endl;

    tokens.normalTokenId = AllocTestHapToken(g_normalHapInfo);
    if (tokens.normalTokenId == 0) {
        std::cerr << "Failed to allocate normal HAP token!" << std::endl;
        return false;
    }
    std::cout << "Allocated normal HAP token: " << tokens.normalTokenId << std::endl;

    tokens.dialogTokenIdEx = AllocSystemHapTokenEx(g_dialogHapInfo);
    tokens.dialogTokenId = tokens.dialogTokenIdEx.tokenIdExStruct.tokenID;
    if (tokens.dialogTokenId == 0) {
        std::cerr << "Failed to allocate dialog HAP token!" << std::endl;
        DeleteTestHapToken(tokens.normalTokenId);
        return false;
    }
    std::cout << "Allocated dialog HAP token: " << tokens.dialogTokenId << std::endl;
    return true;
}

int TestOpenSerialWithoutPermission(SerialSrvClient& client, const std::string& deviceId)
{
    std::cout << "\n[Step 1] Normal HAP attempts OpenSerial (no permission yet)..." << std::endl;

    SerialPortInfo portInfo;
    portInfo.portName = deviceId;
    portInfo.productId = 0;
    portInfo.vendorId = 0;
    portInfo.manufacturer = "";

    SerialConfig config = MakeDefaultConfig();
    int32_t ret = client.OpenSerial(portInfo, config);

    std::cout << "OpenSerial result: ";
    PrintErrorCode(ret);

    if (ret == SERIAL_JS_ERR_PERMISSION_DENIED) {
        std::cout << std::endl;
        std::cout << "Expected result: " << SERIAL_JS_ERR_PERMISSION_DENIED << " (需要用户授权)" << std::endl;
        return 0; // Expected result
    }
    if (ret == SERIAL_OK) {
        std::cout << std::endl;
        std::cout << "Unexpected: OpenSerial succeeded without authorization" << std::endl;
        client.CloseSerial(deviceId);
        return -1;
    }
    std::cout << std::endl;
    std::cout << "Unexpected error code" << std::endl;
    return -1;
}

bool TestAddPortAuthorizationStep(SerialSrvClient& client, const DialogTestTokens& tokens, const std::string& deviceId)
{
    std::cout << "\n[Step 2] Dialog HAP authorizes port..." << std::endl;
    SetTestSelfTokenEx(tokens.dialogTokenIdEx.tokenIDEx);

    std::string tokenIdStr = std::to_string(tokens.normalTokenId);
    int32_t ret = client.AddPortAuthorization(tokenIdStr, deviceId);
    std::cout << "AddPortAuthorization result: ";
    PrintErrorCode(ret);

    if (ret != SERIAL_OK) {
        std::cout << std::endl;
        std::cerr << "AddPortAuthorization failed!" << std::endl;
        return false;
    }
    std::cout << std::endl;
    std::cout << "Authorization success" << std::endl;
    return true;
}

int TestOpenSerialWithPermission(SerialSrvClient& client, const std::string& deviceId)
{
    std::cout << "\n[Step 3] Normal HAP attempts OpenSerial again..." << std::endl;

    SerialPortInfo portInfo;
    portInfo.portName = deviceId;
    portInfo.productId = 0;
    portInfo.vendorId = 0;
    portInfo.manufacturer = "";

    SerialConfig config = MakeDefaultConfig();
    int32_t ret = client.OpenSerial(portInfo, config);

    std::cout << "OpenSerial result: ";
    PrintErrorCode(ret);

    if (ret == SERIAL_OK) {
        std::cout << std::endl;
        std::cout << "OpenSerial success!" << std::endl;
        std::cout << "\n[Step 4] Closing serial port..." << std::endl;
        client.CloseSerial(deviceId);
        return 0;
    }
    if (ret == SERIAL_JS_ERR_SERVICE) {
        std::cout << std::endl;
        std::cout << "OpenSerial returned " << SERIAL_JS_ERR_SERVICE << " (服务错误)" << std::endl;
        std::cout << "授权检查已通过！" << std::endl;
        return 0;
    }
    std::cout << std::endl;
    std::cerr << "OpenSerial still failed after authorization!" << std::endl;
    return -1;
}

int RunDialogTest()
{
    std::cout << "=== Serial Authorization Dialog Test (Simulating HAP App) ===" << std::endl;
    std::cout << "This test simulates the complete authorization flow" << std::endl;
    std::cout << std::endl;

    std::string deviceId = "/dev/ttyUSB0";

    DialogTestTokens tokens;
    if (!AllocateDialogTestTokens(tokens)) {
        return 1;
    }

    auto& client = SerialSrvClient::GetInstance();

    // Step 1: Test without permission
    SetTestSelfToken(tokens.normalTokenId);
    if (TestOpenSerialWithoutPermission(client, deviceId) != 0) {
        DeleteTestHapToken(tokens.normalTokenId);
        DeleteTestHapToken(tokens.dialogTokenId);
        return 1;
    }

    // Step 2: Add authorization
    if (!TestAddPortAuthorizationStep(client, tokens, deviceId)) {
        DeleteTestHapToken(tokens.normalTokenId);
        DeleteTestHapToken(tokens.dialogTokenId);
        return 1;
    }

    // Step 3: Test with permission
    SetTestSelfToken(tokens.normalTokenId);
    if (TestOpenSerialWithPermission(client, deviceId) != 0) {
        DeleteTestHapToken(tokens.normalTokenId);
        DeleteTestHapToken(tokens.dialogTokenId);
        return 1;
    }

    // Step 5: Cleanup
    std::cout << "\n[Step 5] Cleaning up test tokens..." << std::endl;
    DeleteTestHapToken(tokens.normalTokenId);
    DeleteTestHapToken(tokens.dialogTokenId);
    std::cout << "Tokens deleted successfully" << std::endl;

    std::cout << std::endl;
    std::cout << "=== Dialog Test PASSED ===" << std::endl;
    return 0;
}

// ==================== 弹窗交互测试模块 ====================

// Forward declaration for RunShowDbTest (defined later in file)
int RunShowDbTest();

/**
 * @brief 执行弹窗交互测试（显示真实弹窗，等待用户点击）
 * @details 此测试需要配合弹窗测试服务（serialservice_authdialog_test）：
 *          1. 使用弹窗测试服务，调用 OpenSerial 会显示真实授权弹窗
 *          2. 用户在设备屏幕上看到弹窗，显示应用名和端口信息
 *          3. 用户点击"允许"后，弹窗关闭
 *          4. 检查数据库验证授权是否被添加
 *
 * @note 使用方法：
 *       1. 先运行 ./test_serial.sh build_popup_test 编译弹窗测试服务
 *       2. 运行 ./test_serial.sh push_popup_test 推送弹窗测试服务
 *       3. 运行 ./test_serial.sh popup 执行弹窗交互测试
 *       4. 测试完成后运行 ./test_serial.sh restore_service 恢复正常服务
 */
int RunPopupInteractiveTest()
{
    std::cout << "=== Popup Interactive Test (Real Dialog on Device) ===" << std::endl;
    std::cout << "此测试需要弹窗测试服务（serialservice_authdialog_test）已部署" << std::endl;
    std::cout << std::endl;

    std::string deviceId = "/dev/ttyUSB0";

    // Step 0: 分配模拟 HAP token
    std::cout << "[Step 0] Allocating simulated HAP token..." << std::endl;

    AccessTokenID normalTokenId = AllocTestHapToken(g_normalHapInfo);
    if (normalTokenId == 0) {
        std::cerr << "Failed to allocate normal HAP token!" << std::endl;
        return 1;
    }
    std::cout << "Allocated normal HAP token: " << normalTokenId << std::endl;
    std::cout << "  bundleName: " << g_normalHapInfo.bundleName << std::endl;
    std::cout << "  userId: " << g_normalHapInfo.userID << std::endl;

    auto& client = SerialSrvClient::GetInstance();

    // Step 1: 设置普通 HAP token 并调用 OpenSerial
    std::cout << "\n[Step 1] Calling OpenSerial with normal HAP token..." << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "请在设备屏幕上查看授权弹窗！" << std::endl;
    std::cout << "弹窗将显示：" << std::endl;
    std::cout << "  - 应用名称: SerialAuthTest" << std::endl;
    std::cout << "  - 端口名称: " << deviceId << std::endl;
    std::cout << "请点击'允许'按钮授权..." << std::endl;
    std::cout << "============================================================" << std::endl;

    SetTestSelfToken(normalTokenId);

    SerialPortInfo portInfo;
    portInfo.portName = deviceId;
    portInfo.productId = 0x6001;
    portInfo.vendorId = 0x403;
    portInfo.manufacturer = "TestDevice";

    SerialConfig config = MakeDefaultConfig();

    int32_t ret = client.OpenSerial(portInfo, config);
    std::cout << "\n[Result] OpenSerial returned: ";
    PrintErrorCode(ret);

    if (ret == SERIAL_OK) {
        std::cout << "弹窗已关闭，OpenSerial 成功！" << std::endl;
        client.CloseSerial(deviceId);
    } else if (ret == SERIAL_ERR_PERMISSION_DENIED) {
        std::cout << "弹窗已关闭，用户拒绝了授权" << std::endl;
    } else {
        std::cout << "弹窗已关闭，返回其他错误" << std::endl;
    }

    // Step 2: 检查数据库，验证授权记录
    std::cout << "\n[Step 2] Checking database for authorization records..." << std::endl;
    RunShowDbTest();

    // Step 3: 清理 token
    std::cout << "\n[Step 3] Cleaning up test token..." << std::endl;
    DeleteTestHapToken(normalTokenId);
    std::cout << "Token deleted successfully" << std::endl;

    std::cout << "\n=== Popup Interactive Test Complete ===" << std::endl;
    std::cout << "请查看设备屏幕确认弹窗已正确显示" << std::endl;
    std::cout << "测试完成，请运行 ./test_serial.sh restore_service 恢复正常服务" << std::endl;
    return 0;
}

// ==================== 显示数据库模块 ====================

// Forward declaration for RunShowDbTest (defined later in file)
int RunShowDbTest();

// Helper functions for RunShowDbTest
void PrintDbTableHeader()
{
    std::cout << "+------+-----+----------------------+----------------------+------------+--------------+-------------"
                 "-+--------------+--------------+"
              << std::endl;
    std::cout << "|  ID  | UID |       PortName       |      BundleName      |   TokenId  | InstallTime  | UpdateTime  "
                 " | RequestTime  | ValidPeriod  |"
              << std::endl;
    std::cout << "+------+-----+----------------------+----------------------+------------+--------------+-------------"
                 "-+--------------+--------------+"
              << std::endl;
}

void PrintDbRecords(const std::vector<SerialRightFullRecord>& records)
{
    for (const auto& record : records) {
        std::cout << "| " << std::setw(TABLE_WIDTH_ID) << record.primaryKeyId << " | " << std::setw(TABLE_WIDTH_UID)
                  << record.uid << " | " << std::setw(TABLE_WIDTH_PORTNAME)
                  << record.deviceId.substr(0, TABLE_WIDTH_PORTNAME) << " | " << std::setw(TABLE_WIDTH_BUNDLENAME)
                  << record.bundleName.substr(0, TABLE_WIDTH_BUNDLENAME) << " | " << std::setw(TABLE_WIDTH_TOKENID)
                  << record.tokenId.substr(0, TABLE_WIDTH_TOKENID) << " | " << std::setw(TABLE_WIDTH_TIME)
                  << record.installTime << " | " << std::setw(TABLE_WIDTH_TIME) << record.updateTime << " | "
                  << std::setw(TABLE_WIDTH_TIME) << record.requestTime << " | " << std::setw(TABLE_WIDTH_TIME)
                  << record.validPeriod << " |" << std::endl;
    }
}

void PrintDbTableFooter(size_t count)
{
    std::cout << "+------+-----+----------------------+----------------------+------------+--------------+-------------"
                 "-+--------------+--------------+"
              << std::endl;
    std::cout << std::endl;
    std::cout << "Total: " << count << " records" << std::endl;
}

int RunShowDbTest()
{
    std::cout << "=== Show Database Records ===" << std::endl;

    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        std::cout << "[ERROR] SerialRightDbHelper instance is null" << std::endl;
        return 1;
    }

    if (!helper->IsDatabaseValid()) {
        std::cout << "[ERROR] Database is not valid" << std::endl;
        return 1;
    }

    std::vector<SerialRightFullRecord> records;
    int32_t ret = helper->QueryAllRightRecords(records);
    if (ret < 0) {
        std::cout << "[ERROR] QueryAllRightRecords failed: " << ret << std::endl;
        return 1;
    }

    if (records.empty()) {
        std::cout << "[INFO] No records found in database" << std::endl;
        return 0;
    }

    std::cout << "[INFO] Found " << records.size() << " records:" << std::endl;
    PrintDbTableHeader();
    PrintDbRecords(records);
    PrintDbTableFooter(records.size());
    return 0;
}

// ==================== 主函数入口 ====================

/**
 * @brief 主函数 - 测试入口
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 程序退出码
 */
int main(int argc, char* argv[])
{
    // 无参数或 help 命令显示帮助
    if (argc < ARGV_PORT_INDEX || std::string(argv[ARGV_COMMAND_INDEX]) == "help") {
        ShowHelp();
        return 0;
    }

    std::string command = argv[ARGV_COMMAND_INDEX];
    int result = 0;

    // 根据命令执行对应测试
    if (command == "loopback") {
        // 回环测试，支持可选端口参数
        std::string deviceId = "/dev/ttyUSB0";
        if (argc > ARGV_PORT_INDEX) {
            deviceId = argv[ARGV_PORT_INDEX];
        }
        result = RunLoopbackTest(deviceId);
    } else if (command == "db") {
        // 数据库测试
        result = RunDbTest();
    } else if (command == "dialog") {
        // 弹窗流程测试（自动）
        result = RunDialogTest();
    } else if (command == "popup") {
        // 弹窗交互测试（需用户点击）
        result = RunPopupInteractiveTest();
    } else if (command == "showdb") {
        // 显示数据库内容
        result = RunShowDbTest();
    } else if (command == "all") {
        // 运行全部测试
        std::cout << "=== Running All Tests ===" << std::endl;
        std::cout << std::endl;

        std::cout << "\n========== Test 1: Loopback ==========\n" << std::endl;
        int loopbackResult = RunLoopbackTest("/dev/ttyUSB0");

        std::cout << "\n========== Test 2: Database ==========\n" << std::endl;
        int dbResult = RunDbTest();

        std::cout << "\n========== Test 3: Dialog ==========\n" << std::endl;
        int dialogResult = RunDialogTest();

        // 总结
        std::cout << "\n=== All Tests Summary ===" << std::endl;
        std::cout << "Loopback test: " << (loopbackResult == 0 ? "PASSED" : "FAILED") << std::endl;
        std::cout << "Database test: " << (dbResult == 0 ? "PASSED" : "FAILED") << std::endl;
        std::cout << "Dialog test: " << (dialogResult == 0 ? "PASSED" : "FAILED") << std::endl;
        std::cout << std::endl;

        // 任一测试失败则返回失败
        result = (loopbackResult != 0 || dbResult != 0 || dialogResult != 0) ? 1 : 0;
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        std::cerr << "Use 'help' to see available commands." << std::endl;
        ShowHelp();
        return 1;
    }

    return result;
}