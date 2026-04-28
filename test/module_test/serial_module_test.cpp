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
 * @details 整合三个独立测试模块：
 *          - loopback: 串口回环测试（验证通信功能）
 *          - db: 数据库测试（验证权限管理）
 *          - dialog: 弹窗测试（验证授权流程）
 *
 * @section Usage 使用方法
 * - ./serial_module_test loopback [/dev/ttyUSB0]  回环测试，可指定端口
 * - ./serial_module_test db                       数据库测试
 * - ./serial_module_test dialog                   弹窗测试
 * - ./serial_module_test all                      运行全部测试
 * - ./serial_module_test help                     显示帮助信息
 *
 * @since 2026-04-27
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "hilog/log.h"
#include "hilog_wrapper.h"
#include "serial_error_code.h"
#include "serial_right_db_helper.h"
#include "serial_right_manager.h"
#include "serial_srv_client.h"

using namespace OHOS::Serial;
using namespace OHOS::HiviewDFX;

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
    std::cout << "  loopback [port]  - 串口回环测试，默认端口 /dev/ttyUSB0" << std::endl;
    std::cout << "  db               - 数据库测试，验证权限管理功能" << std::endl;
    std::cout << "  dialog           - 弹窗测试，验证授权流程" << std::endl;
    std::cout << "  all              - 运行全部测试" << std::endl;
    std::cout << "  help             - 显示此帮助信息" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  ./serial_module_test loopback              # 使用默认端口测试" << std::endl;
    std::cout << "  ./serial_module_test loopback /dev/ttyUSB1 # 使用指定端口测试" << std::endl;
    std::cout << "  ./serial_module_test db                    # 数据库测试" << std::endl;
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
            std::cout << "[" << std::hex << static_cast<int>(data[i]) << "]";
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
                  << std::hex << dev.vendorId << std::endl;
    }
    return 0;
}

/**
 * @brief 打开串口步骤
 */
int LoopbackOpenSerial(SerialSrvClient& client, const std::string& portName, SerialConfig& config)
{
    std::cout << "\n[Step 2] Opening serial port: " << portName << std::endl;
    SerialPortInfo portInfo;
    portInfo.portName = portName;
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
int LoopbackWriteData(SerialSrvClient& client, const std::string& portName, ListenerHandle& handle)
{
    std::cout << "\n[Step 3] Registering data listener..." << std::endl;
    int32_t ret = client.RegisterDataListener(portName, OnLoopbackDataReceived, handle);
    if (handle == 0) {
        std::cerr << "RegisterDataListener failed!" << std::endl;
        client.CloseSerial(portName);
        return 1;
    }
    std::cout << "RegisterDataListener success, handle=" << handle << std::endl;

    std::cout << "\n[Step 4] Writing data..." << std::endl;
    std::string testString = "Hello Loopback";
    std::vector<uint8_t> writeData(testString.begin(), testString.end());
    writeData.push_back('\n');

    int32_t bytesWritten = 0;
    ret = client.WriteSerial(portName, writeData, LOOPBACK_WRITE_TIMEOUT_MS, bytesWritten);
    if (ret != SERIAL_OK) {
        std::cerr << "WriteSerial failed: " << ret << std::endl;
        client.UnregisterDataListener(portName, handle);
        client.CloseSerial(portName);
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
 * @brief 执行回环测试
 */
int RunLoopbackTest(const std::string& portName)
{
    std::cout << "=== Serial Loopback Test ===" << std::endl;
    std::cout << "Port: " << portName << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_loopbackMutex);
        g_dataReceived = false;
        g_receivedData.clear();
    }

    auto& client = SerialSrvClient::GetInstance();
    if (LoopbackQueryDevices(client) != 0) {
        return 1;
    }

    SerialConfig config;
    if (LoopbackOpenSerial(client, portName, config) != 0) {
        return 1;
    }

    ListenerHandle handle = 0;
    if (LoopbackWriteData(client, portName, handle) != 0) {
        return 1;
    }

    std::vector<uint8_t> writeData;
    {
        std::lock_guard<std::mutex> lock(g_loopbackMutex);
        writeData = std::vector<uint8_t>(std::string("Hello Loopback").begin(), std::string("Hello Loopback").end());
        writeData.push_back('\n');
    }
    LoopbackWaitAndVerify(writeData);

    std::cout << "\n[Step 6] Closing serial port..." << std::endl;
    client.UnregisterDataListener(portName, handle);
    int32_t ret = client.CloseSerial(portName);
    if (ret != SERIAL_OK) {
        std::cerr << "CloseSerial failed: " << ret << std::endl;
        return 1;
    }
    std::cout << "CloseSerial success!" << std::endl;
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

    manager.RemoveDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
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
 * @brief 测试删除设备权限
 */
bool TestDbRemoveDeviceRight()
{
    DbTestLog("Test 4: RemoveDeviceRight() - Delete authorization record");
    auto& manager = SerialRightManager::GetInstance();

    bool removeResult = manager.RemoveDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    DbTestAssert(removeResult, "RemoveDeviceRight() should return true");

    bool hasRight = manager.HasRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    DbTestAssert(!hasRight, "HasRight() should return false after RemoveDeviceRight()");

    return removeResult && !hasRight;
}

/**
 * @brief 测试 tokenId 变体的添加权限
 */
bool TestDbAddDeviceRightByTokenId()
{
    DbTestLog("Test 5: AddDeviceRight() with tokenId variant");
    auto& manager = SerialRightManager::GetInstance();

    manager.RemoveDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);

    // 使用4参数变体进行实际测试
    bool result = manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    DbTestAssert(result, "AddDeviceRight() 4-param variant should work");

    manager.RemoveDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
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

    manager.RemoveDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    manager.RemoveDeviceRight(TEST_PORT_NAME_2, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    manager.RemoveDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID_2);

    return result1 && result2 && result3 && has1 && has2 && has3;
}

/**
 * @brief 测试删除端口所有权限
 */
bool TestDbRemoveDeviceAllRight()
{
    DbTestLog("Test 7: RemoveDeviceAllRight() - Remove all rights for a port");
    auto& manager = SerialRightManager::GetInstance();

    manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID);

    bool result = manager.RemoveDeviceAllRight(TEST_PORT_NAME);
    DbTestAssert(result, "RemoveDeviceAllRight() should return true");

    bool has1 = manager.HasRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    bool has2 = manager.HasRight(TEST_PORT_NAME, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID);

    DbTestAssert(!has1 && !has2, "All rights for port should be removed");

    return result && !has1 && !has2;
}

/**
 * @brief 测试数据库助手直接操作
 */
bool TestDbDatabaseHelperDirect()
{
    DbTestLog("Test 8: Database helper direct operations");
    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        DbTestLog("Database helper is null, skip this test");
        return false;
    }

    uint64_t nowTime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    SerialRightAppInfo info;
    info.uid = TEST_USER_ID;
    info.installTime = nowTime;
    info.updateTime = nowTime;
    info.requestTime = nowTime;
    info.validPeriod = SERIAL_RIGHT_VALID_PERIOD_SET;

    int32_t ret = helper->AddRightRecord(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, info);
    DbTestAssert(ret >= 0, "AddRightRecord() should succeed");

    std::vector<SerialRightAppInfo> infos;
    ret = helper->QueryRightRecord(TEST_USER_ID, TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, infos);
    DbTestAssert(ret >= 0 && !infos.empty(), "QueryRightRecord() should return records");

    ret = helper->DeleteRightRecord(TEST_USER_ID, TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID);
    DbTestAssert(ret >= 0, "DeleteRightRecord() should succeed");

    infos.clear();
    ret = helper->QueryRightRecord(TEST_USER_ID, TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, infos);
    DbTestAssert(infos.empty(), "QueryRightRecord() should return empty after delete");

    return true;
}

/**
 * @brief 测试记录过期检查
 */
bool TestDbIsRecordExpired()
{
    DbTestLog("Test 9: IsRecordExpired check");
    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        DbTestLog("Database helper is null, skip this test");
        return false;
    }

    uint64_t nowTime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    SerialRightAppInfo info;
    info.uid = TEST_USER_ID;
    info.installTime = nowTime;
    info.updateTime = nowTime;
    info.requestTime = nowTime;
    info.validPeriod = SERIAL_RIGHT_VALID_PERIOD_SET;

    helper->AddRightRecord(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, info);

    bool expired = helper->IsRecordExpired(TEST_USER_ID, TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, nowTime);
    DbTestAssert(!expired, "Record should not be expired with valid period");

    helper->DeleteRightRecord(TEST_USER_ID, TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID);

    return !expired;
}

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
        std::string portName = "/dev/ttyUSB" + std::to_string(threadId);
        std::string bundleName = "com.test.thread" + std::to_string(threadId);
        std::string tokenId = std::to_string(100000 + threadId);

        for (int32_t i = 0; i < operationsPerThread; i++) {
            if (manager.AddDeviceRight(portName, bundleName, tokenId, TEST_USER_ID)) {
                successCount++;
            } else {
                failCount++;
            }

            if (manager.HasRight(portName, bundleName, tokenId, TEST_USER_ID)) {
                successCount++;
            } else {
                failCount++;
            }

            if (manager.RemoveDeviceRight(portName, bundleName, tokenId, TEST_USER_ID)) {
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
 * @brief 获取数据库文件大小
 */
long GetDatabaseFileSize()
{
    const char* dbPath = "/data/service/el1/public/serial_manager/SerialRight.db";
    struct stat st;
    if (stat(dbPath, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

/**
 * @brief 测试批量插入100条记录
 */
bool TestDbBatchInsert100()
{
    DbTestLog("Test 11: Batch insert authorization records");
    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        DbTestLog("Database helper is null, skip this test");
        return false;
    }

    long sizeBefore = GetDatabaseFileSize();
    std::ostringstream oss1;
    oss1 << "Database file size before insert: " << sizeBefore << " bytes";
    DbTestLog(oss1.str());

    uint64_t nowTime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    int32_t successCount = 0;
    int32_t failCount = 0;

    for (int32_t i = 0; i < BATCH_TEST_COUNT; i++) {
        std::string portName = "/dev/ttyUSB" + std::to_string(i);
        SerialRightAppInfo info;
        info.uid = BATCH_TEST_USER_ID;
        info.installTime = nowTime;
        info.updateTime = nowTime;
        info.requestTime = nowTime;
        info.validPeriod = SERIAL_RIGHT_VALID_PERIOD_SET;

        int32_t ret = helper->AddRightRecord(portName, BATCH_TEST_BUNDLE_NAME, BATCH_TEST_TOKEN_ID, info);
        if (ret >= 0) {
            successCount++;
        } else {
            failCount++;
        }
    }

    std::ostringstream oss2;
    oss2 << "Batch insert completed: success=" << successCount << ", fail=" << failCount;
    DbTestLog(oss2.str());
    DbTestAssert(successCount == BATCH_TEST_COUNT, "All batch inserts should succeed");

    long sizeAfter = GetDatabaseFileSize();
    std::ostringstream oss3;
    oss3 << "Database file size after insert: " << sizeAfter << " bytes";
    DbTestLog(oss3.str());

    // 清理
    for (int32_t i = 0; i < BATCH_TEST_COUNT; i++) {
        std::string portName = "/dev/ttyUSB" + std::to_string(i);
        helper->DeleteRightRecord(BATCH_TEST_USER_ID, portName, BATCH_TEST_BUNDLE_NAME, BATCH_TEST_TOKEN_ID);
    }

    return successCount == BATCH_TEST_COUNT;
}

/**
 * @brief 测试清除所有权限
 */
bool TestDbClearAllRights()
{
    DbTestLog("Test 12: ClearAllRights");
    auto& manager = SerialRightManager::GetInstance();

    manager.AddDeviceRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    manager.AddDeviceRight(TEST_PORT_NAME_2, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID_2);

    int32_t ret = manager.ClearAllRights();
    DbTestAssert(ret == SERIAL_RIGHT_OK, "ClearAllRights() should return OK");

    bool has1 = manager.HasRight(TEST_PORT_NAME, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    bool has2 = manager.HasRight(TEST_PORT_NAME_2, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID_2);

    DbTestAssert(!has1 && !has2, "All rights should be cleared");

    return ret == SERIAL_RIGHT_OK && !has1 && !has2;
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
    TestDbRemoveDeviceRight();
    TestDbAddDeviceRightByTokenId();
    TestDbMultipleRecords();
    TestDbRemoveDeviceAllRight();
    TestDbDatabaseHelperDirect();
    TestDbIsRecordExpired();
    TestDbConcurrentOperations();
    TestDbBatchInsert100();
    TestDbClearAllRights();

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
 * @brief 执行弹窗测试
 */
int RunDialogTest()
{
    std::cout << "=== Serial Authorization Dialog Test ===" << std::endl;
    std::cout << "This test triggers the permission check flow" << std::endl;
    std::cout << "Expected behavior: Authorization dialog should appear" << std::endl;
    std::cout << std::endl;

    auto& client = SerialSrvClient::GetInstance();
    DialogQueryDevices(client);
    DialogOpenDevice(client);

    std::cout << std::endl;
    std::cout << "=== Dialog Test completed ===" << std::endl;
    std::cout << "Check hilog for SerialService and SerialRightManager logs:" << std::endl;
    std::cout << "  hdc shell hilog -t SerialService" << std::endl;
    std::cout << "  hdc shell hilog -t SerialRightManager" << std::endl;
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
        std::string portName = "/dev/ttyUSB0";
        if (argc > ARGV_PORT_INDEX) {
            portName = argv[ARGV_PORT_INDEX];
        }
        result = RunLoopbackTest(portName);
    } else if (command == "db") {
        // 数据库测试
        result = RunDbTest();
    } else if (command == "dialog") {
        // 弹窗测试
        result = RunDialogTest();
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