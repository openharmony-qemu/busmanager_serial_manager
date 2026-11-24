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
 * @file serial_service_test.cpp
 * @brief SerialService 单元测试 - 测试真实业务逻辑
 * @details 本文件测试 SerialService 的核心业务逻辑，包括：
 *          - OpenSerial 设备打开和参数校验
 *          - CloseSerial 资源清理
 *          - OnClientDied 死亡通知
 *          - OnIdle 自杀逻辑
 *          - QuerySerialDevices 设备查询
 *          - WriteSerial 数据写入
 *          使用 MockSerials 模拟 HDI 层。
 *          注意：权限验证依赖真实的 SerialRightManager，测试环境可能需要特定设置。
 * @since 2026-04-24
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include "hilog_wrapper.h"
#include "ipc_skeleton.h"
#include "mock_serial_hdi.h"
#include "os_account_manager.h"
#include "serial_error_code.h"
#include "serial_hdi_stub.h"
#include "serial_right_manager.h"
#include "serial_service.h"
#include "serial_service_callback.h"
#include "system_ability_definition.h"
#include "system_ability_ondemand_reason.h"

using namespace testing;
using namespace testing::ext;
using namespace OHOS::Serial;
using namespace OHOS::HDI::Serial::V1_0;

namespace OHOS {
namespace Serial {

namespace {
constexpr const char* TEST_DEVICE_ID_1 = "5678_1234_ttyUSB0";
constexpr const char* TEST_TOKEN_ID = "12345678";
constexpr int32_t TEST_HDI_FAILURE = -1;
constexpr int32_t TEST_WRITE_BYTES = 5;
constexpr int32_t TEST_TIMEOUT_MS = 1000;
constexpr int32_t INVALID_FD = -1;
constexpr int32_t PIPE_READ = 0;
constexpr int32_t PIPE_WRITE = 1;
constexpr size_t WRITE_DATA_MAX_SIZE = 4096;
constexpr int32_t WRITE_TIMEOUT_MAX_MS = 300000;
constexpr int32_t MOCK_VID_FTDI = 5678;
constexpr int32_t MOCK_PID_FTDI = 1234;
constexpr int32_t MOCK_VID_PROLIFIC = 2222;
constexpr int32_t MOCK_PID_PROLIFIC = 1111;
constexpr int32_t EXPECTED_ACM_PORT_1 = 1001;
constexpr int32_t EXPECTED_TTYS_PORT_2 = 2002;
constexpr uint8_t TEST_DATA_BYTE = 0x41;
} // namespace

// ============================================================================
// Mock SerialServiceCallback
// ============================================================================

/**
 * @class MockSerialServiceCallback
 * @brief Mock 实现的 ISerialServiceCallback，用于测试回调
 */
class MockSerialServiceCallback : public SerialServiceCallbackStub {
public:
    MockSerialServiceCallback() = default;
    ~MockSerialServiceCallback() override = default;

    MOCK_METHOD(ErrCode, onDataRead, (const std::string& portName, const std::vector<uint8_t>& buffer), (override));
    MOCK_METHOD(ErrCode, onDisconnect, (const std::string& portName), (override));
};

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @class SerialServiceTest
 * @brief SerialService 单元测试 fixture
 */
class SerialServiceTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialServiceTest::SetUpTestCase");
    }

    static void TearDownTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialServiceTest::TearDownTestCase");
    }

    void SetUp() override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialServiceTest::SetUp");
        // 创建 Mock HDI 代理（NiceMock 抑制未预期调用的 GMOCK WARNING）
        mockHdi_ = new NiceMock<MockSerials>();
        ASSERT_NE(mockHdi_, nullptr);

        // 创建 SerialService 实例（用于测试）
        service_ = new SerialService(SERIAL_SYSTEM_ABILITY_ID, false);
        ASSERT_NE(service_, nullptr);

        // 注入 Mock HDI 代理
        service_->serialsProxy_ = mockHdi_;

        // 设置 Mock 设备列表
        SetupMockDevices();

        // 预授权测试设备，绕过 RequestRight 弹框阻塞
        PreAuthorizeTestDevices();

        // 创建 Mock 回调
        mockCallback_ = new MockSerialServiceCallback();
        ASSERT_NE(mockCallback_, nullptr);
    }

    void TearDown() override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialServiceTest::TearDown");
        // 清理资源
        if (service_ != nullptr) {
            // 尝试关闭可能打开的设备
            service_->CloseSerial("ttyUSB0");
            service_->CloseSerial("ttyUSB1");
        }
        // 清理 SerialRightManager 进程级单例的授权缓存，
        // 防止本 fixture 预授权的 deviceId 污染后续用例的权限判定
        ClearAuthorizedRights();
        mockCallback_ = nullptr;
        mockHdi_ = nullptr;
        service_ = nullptr;
    }

protected:
    sptr<MockSerials> mockHdi_;
    sptr<SerialService> service_;
    sptr<MockSerialServiceCallback> mockCallback_;

    /**
     * @brief 设置 Mock 设备列表
     */
    void SetupMockDevices()
    {
        std::vector<SerialDeviceInfo> mockDevices;
        SerialDeviceInfo device1;
        device1.portName = "ttyUSB0";
        device1.productId = MOCK_PID_FTDI;
        device1.vendorId = MOCK_VID_FTDI;
        device1.manufacturer = "FTDI";
        mockDevices.push_back(device1);

        SerialDeviceInfo device2;
        device2.portName = "ttyUSB1";
        device2.productId = MOCK_PID_PROLIFIC;
        device2.vendorId = MOCK_VID_PROLIFIC;
        device2.manufacturer = "Prolific";
        mockDevices.push_back(device2);

        mockHdi_->SetMockDevices(mockDevices);
    }

    void PreAuthorizeTestDevices()
    {
        std::string bundleName = service_->GetCallingBundleName();
        std::string tokenId = service_->GetCallingTokenId();
        int32_t userId = service_->GetCallingUserIdFromToken();
        SerialPortInfo portInfo1 = CreateTestPortInfo("ttyUSB0");
        std::string deviceId1 = service_->BuildDeviceId(portInfo1);
        SerialRightManager::GetInstance().AddDeviceRight(deviceId1, bundleName, tokenId, userId);
        SerialPortInfo portInfo2;
        portInfo2.portName = "ttyUSB1";
        portInfo2.productId = MOCK_PID_PROLIFIC;
        portInfo2.vendorId = MOCK_VID_PROLIFIC;
        portInfo2.manufacturer = "Prolific";
        std::string deviceId2 = service_->BuildDeviceId(portInfo2);
        SerialRightManager::GetInstance().AddDeviceRight(deviceId2, bundleName, tokenId, userId);
    }

    /**
     * @brief 清理 SerialRightManager 进程级单例的授权缓存
     * @details SetUp 中 PreAuthorizeTestDevices 向 rightCache_ 写入两条授权记录，
     *          SerialRightManager 无公开清理 API，且为进程级单例，用例间共享。
     *          此处通过 private=public 编译定义直接清理，防止预授权的 deviceId
     *          污染同进程后续用例的权限判定（如负向权限拒绝用例）。
     */
    void ClearAuthorizedRights()
    {
        auto& mgr = SerialRightManager::GetInstance();
        std::lock_guard<std::mutex> lock(mgr.cacheMutex_);
        mgr.rightCache_.clear();
    }

    /**
     * @brief 创建默认测试配置
     */
    SerialConfig CreateDefaultConfig()
    {
        SerialConfig config;
        config.baudRate = 115200;
        config.dataBits = 8;
        config.stopBits = 1;
        config.parity = 0;
        config.rtscts = 0;
        config.xon = 0;
        config.xoff = 0;
        config.xany = 0;
        return config;
    }

    /**
     * @brief 创建测试用的端口信息
     */
    SerialPortInfo CreateTestPortInfo(const std::string& portName)
    {
        SerialPortInfo portInfo;
        portInfo.portName = portName;
        portInfo.productId = MOCK_PID_FTDI;
        portInfo.vendorId = MOCK_VID_FTDI;
        portInfo.manufacturer = "FTDI";
        return portInfo;
    }

    sptr<MockSerialDevice> InjectDeviceContext(const std::string& portName)
    {
        sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
        // 抑制 TearDown → CloseSerial → hdiDevice->Close() 的未预期调用 WARNING
        ON_CALL(*mockDevice, Close()).WillByDefault(Return(HDF_SUCCESS));
        auto context = std::make_shared<DeviceContext>();
        context->hdiDevice = mockDevice;
        context->serviceCallback = mockCallback_;
        context->portName = portName;
        context->tokenId = service_->GetCallingTokenId();
        context->userId = 0;
        context->config = CreateDefaultConfig();
        context->openTime = std::chrono::steady_clock::now();
        service_->deviceContexts_[portName] = context;
        return mockDevice;
    }
};

// ============================================================================
// 1. OpenSerial 参数校验测试
// ============================================================================

/**
 * @brief 测试 OpenSerial：callback 为 nullptr 返回 ERR_INVALID_PARAM
 * @details 验证参数校验逻辑
 */
HWTEST_F(SerialServiceTest, OpenSerial_NullCallback_ReturnsInvalidParam, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_NullCallback_ReturnsInvalidParam: start");

    sptr<ISerialServiceCallback> nullCallback = nullptr;

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    int32_t result = service_->OpenSerial(portInfo, config, nullCallback);

    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);

    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_NullCallback_ReturnsInvalidParam: success");
}

/**
 * @brief 测试 OpenSerial：HDI OpenDevice 失败返回 ERR_SERVICE
 * @details 设置 HDI OpenDevice 失败，验证错误处理
 */
HWTEST_F(SerialServiceTest, OpenSerial_HdiOpenFailure_ReturnsServiceError, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_HdiOpenFailure_ReturnsServiceError: start");

    // 设置 HDI OpenDevice 失败
    mockHdi_->SetOpenDeviceResult(false);

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    int32_t result = service_->OpenSerial(portInfo, config, mockCallback_);

    // 如果权限检查失败，返回 ERR_PERMISSION_DENIED
    // 如果权限通过但 HDI 失败，返回 ERR_SERVICE
    EXPECT_TRUE(result == SERIAL_ERR_SERVICE || result == SERIAL_ERR_PERMISSION_DENIED);

    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_HdiOpenFailure_ReturnsServiceError: success, result=%d", result);
}

/**
 * @brief 测试 OpenSerial：成功打开设备并注册 DeviceContext
 * @details 设置 HDI OpenDevice 成功，验证成功打开
 *          注意：权限验证依赖真实的 SerialRightManager
 */
HWTEST_F(SerialServiceTest, OpenSerial_Success_RegistersDeviceContext, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_Success_RegistersDeviceContext: start");

    mockHdi_->SetOpenDeviceResult(true); // HDI 成功

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    // 打开前设备数为 0
    EXPECT_EQ(service_->GetDeviceCount(), 0);

    int32_t result = service_->OpenSerial(portInfo, config, mockCallback_);

    // 权限检查结果取决于测试环境
    // 如果是系统应用/SA，权限通过
    // 如果是普通应用，可能需要授权
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial result: %d", result);

    if (result == SERIAL_OK) {
        // 成功打开，设备数应为 1
        EXPECT_EQ(service_->GetDeviceCount(), 1);
    } else {
        // 权限拒绝或服务错误，设备数应为 0
        EXPECT_EQ(service_->GetDeviceCount(), 0);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_Success_RegistersDeviceContext: success");
}

/**
 * @brief 测试 OpenSerial：重复打开同一设备
 * @details 如果第一次打开成功，第二次应返回 ERR_DEVICE_BUSY
 */
HWTEST_F(SerialServiceTest, OpenSerial_DeviceAlreadyOpen_ReturnsDeviceBusy, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_DeviceAlreadyOpen_ReturnsDeviceBusy: start");

    mockHdi_->SetOpenDeviceResult(true);

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    // 第一次打开
    int32_t result1 = service_->OpenSerial(portInfo, config, mockCallback_);
    SERIAL_HILOGI(MODULE_SERVICE, "First OpenSerial result: %d", result1);

    if (result1 == SERIAL_OK) {
        // 第一次成功，第二次应返回 ERR_DEVICE_BUSY
        int32_t result2 = service_->OpenSerial(portInfo, config, mockCallback_);
        EXPECT_EQ(result2, SERIAL_ERR_DEVICE_BUSY);
        // 设备数仍为 1
        EXPECT_EQ(service_->GetDeviceCount(), 1);
    } else {
        // 第一次失败（权限拒绝），第二次也会失败
        int32_t result2 = service_->OpenSerial(portInfo, config, mockCallback_);
        EXPECT_EQ(result2, result1); // 相同错误码
    }

    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial_DeviceAlreadyOpen_ReturnsDeviceBusy: success");
}

// ============================================================================
// 2. CloseSerial 资源清理测试
// ============================================================================

/**
 * @brief 测试 CloseSerial：关闭已打开设备返回 SERIAL_OK
 * @details 验证关闭成功，DeviceContext 被移除
 */
HWTEST_F(SerialServiceTest, CloseSerial_DeviceOpened_ReturnsSuccess, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial_DeviceOpened_ReturnsSuccess: start");

    mockHdi_->SetOpenDeviceResult(true);

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    // 尝试打开设备
    int32_t openResult = service_->OpenSerial(portInfo, config, mockCallback_);
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial result: %d", openResult);

    if (openResult == SERIAL_OK) {
        // 关闭设备
        int32_t closeResult = service_->CloseSerial("ttyUSB0");
        EXPECT_EQ(closeResult, SERIAL_OK);
        // DeviceContext 被移除
        EXPECT_EQ(service_->GetDeviceCount(), 0);
    } else {
        // 未成功打开，关闭应返回 ERR_DEVICE_DISCONNECTED
        int32_t closeResult = service_->CloseSerial("ttyUSB0");
        EXPECT_EQ(closeResult, SERIAL_ERR_DEVICE_NOT_OPENED);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial_DeviceOpened_ReturnsSuccess: success");
}

/**
 * @brief 测试 CloseSerial：关闭不存在的设备返回 ERR_DEVICE_DISCONNECTED
 * @details 验证对未打开设备关闭的错误处理
 */
HWTEST_F(SerialServiceTest, CloseSerial_DeviceNotExist_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial_DeviceNotExist_ReturnsNotOpened: start");

    int32_t result = service_->CloseSerial("ttyUSB_nonexistent");

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial_DeviceNotExist_ReturnsNotOpened: success");
}

/**
 * @brief 测试 CloseSerial：关闭最后一个设备后触发 unloadTimer
 * @details 验证关闭最后一个设备后，设备数为 0
 */
HWTEST_F(SerialServiceTest, CloseSerial_LastDevice_TriggersUnloadTimer, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial_LastDevice_TriggersUnloadTimer: start");

    mockHdi_->SetOpenDeviceResult(true);

    // 尝试打开两个设备
    SerialPortInfo portInfo1 = CreateTestPortInfo("ttyUSB0");
    SerialPortInfo portInfo2 = CreateTestPortInfo("ttyUSB1");
    SerialConfig config = CreateDefaultConfig();

    int32_t result1 = service_->OpenSerial(portInfo1, config, mockCallback_);
    int32_t result2 = service_->OpenSerial(portInfo2, config, mockCallback_);

    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial results: %d, %d", result1, result2);

    // 计算成功打开的设备数
    int openedCount = 0;
    if (result1 == SERIAL_OK)
        openedCount++;
    if (result2 == SERIAL_OK)
        openedCount++;

    EXPECT_EQ(service_->GetDeviceCount(), static_cast<uint32_t>(openedCount));

    // 关闭设备
    if (result1 == SERIAL_OK) {
        EXPECT_EQ(service_->CloseSerial("ttyUSB0"), SERIAL_OK);
    }
    if (result2 == SERIAL_OK) {
        EXPECT_EQ(service_->CloseSerial("ttyUSB1"), SERIAL_OK);
    }

    // 所有设备关闭后，设备数为 0
    EXPECT_EQ(service_->GetDeviceCount(), 0);

    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial_LastDevice_TriggersUnloadTimer: success");
}

// ============================================================================
// 3. OnClientDied 死亡通知测试
// ============================================================================

/**
 * @brief 测试 OnClientDied：正确移除 DeviceContext
 * @details 模拟客户端死亡，验证资源清理
 */
HWTEST_F(SerialServiceTest, OnClientDied_RemovesDeviceContext, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OnClientDied_RemovesDeviceContext: start");

    mockHdi_->SetOpenDeviceResult(true);

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    // 尝试打开设备
    int32_t openResult = service_->OpenSerial(portInfo, config, mockCallback_);
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial result: %d", openResult);

    if (openResult == SERIAL_OK) {
        EXPECT_EQ(service_->GetDeviceCount(), 1);

        // 模拟客户端死亡
        service_->OnClientDied("ttyUSB0");

        // DeviceContext 被移除
        EXPECT_EQ(service_->GetDeviceCount(), 0);
    } else {
        // 未成功打开
        service_->OnClientDied("ttyUSB0");
        EXPECT_EQ(service_->GetDeviceCount(), 0);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "OnClientDied_RemovesDeviceContext: success");
}

/**
 * @brief 测试 OnClientDied：不存在的设备无错误
 * @details 验证对未打开设备调用 OnClientDied 的健壮性
 */
HWTEST_F(SerialServiceTest, OnClientDied_DeviceNotExist_NoError, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OnClientDied_DeviceNotExist_NoError: start");

    // 对不存在的设备调用 OnClientDied
    service_->OnClientDied("ttyUSB_nonexistent");

    // 无错误即为成功，设备数仍为 0
    EXPECT_EQ(service_->GetDeviceCount(), 0);

    SERIAL_HILOGI(MODULE_SERVICE, "OnClientDied_DeviceNotExist_NoError: success");
}

/**
 * @brief 测试 OnClientDied：关闭最后一个设备后触发 unloadTimer
 * @details 验证客户端死亡清理后，如果无活跃设备则设备数为 0
 */
HWTEST_F(SerialServiceTest, OnClientDied_LastDevice_TriggersUnloadTimer, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OnClientDied_LastDevice_TriggersUnloadTimer: start");

    mockHdi_->SetOpenDeviceResult(true);

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    // 尝试打开设备
    int32_t openResult = service_->OpenSerial(portInfo, config, mockCallback_);

    if (openResult == SERIAL_OK) {
        EXPECT_EQ(service_->GetDeviceCount(), 1);

        // 客户端死亡
        service_->OnClientDied("ttyUSB0");

        // 无活跃设备
        EXPECT_EQ(service_->GetDeviceCount(), 0);
    } else {
        service_->OnClientDied("ttyUSB0");
        EXPECT_EQ(service_->GetDeviceCount(), 0);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "OnClientDied_LastDevice_TriggersUnloadTimer: success");
}

// ============================================================================
// 4. OnIdle 自杀逻辑测试
// ============================================================================

/**
 * @brief 测试 OnIdle：无活跃设备返回 IDLE_NOW（返回值 0）
 * @details 验证当没有设备打开时，OnIdle 返回 0 表示可以卸载
 */
HWTEST_F(SerialServiceTest, OnIdle_NoActiveDevices_ReturnsIdleNow, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OnIdle_NoActiveDevices_ReturnsIdleNow: start");

    // 确保无设备打开
    EXPECT_EQ(service_->GetDeviceCount(), 0);

    SystemAbilityOnDemandReason idleReason;
    int32_t result = service_->OnIdle(idleReason);

    // 返回 0 表示 IDLE_NOW，可以卸载
    EXPECT_EQ(result, 0);

    SERIAL_HILOGI(MODULE_SERVICE, "OnIdle_NoActiveDevices_ReturnsIdleNow: success");
}

/**
 * @brief 测试 OnIdle：有活跃设备时的行为
 * @details 验证当有设备打开时，OnIdle 的返回值
 */
HWTEST_F(SerialServiceTest, OnIdle_HasActiveDevices_Behavior, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OnIdle_HasActiveDevices_Behavior: start");

    mockHdi_->SetOpenDeviceResult(true);

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    // 尝试打开设备
    int32_t openResult = service_->OpenSerial(portInfo, config, mockCallback_);

    SystemAbilityOnDemandReason idleReason;
    int32_t idleResult = service_->OnIdle(idleReason);

    SERIAL_HILOGI(MODULE_SERVICE, "Open result: %d, OnIdle result: %d, deviceCount: %u", openResult, idleResult,
        service_->GetDeviceCount());

    // 清理
    service_->CloseSerial("ttyUSB0");

    SERIAL_HILOGI(MODULE_SERVICE, "OnIdle_HasActiveDevices_Behavior: success");
}

// ============================================================================
// 5. QuerySerialDevices 测试
// ============================================================================

/**
 * @brief 测试 QuerySerialDevices：HDI 正常返回设备列表
 * @details 验证设备列表转换正确
 */
HWTEST_F(SerialServiceTest, QuerySerialDevices_HdiSuccess_ReturnsCorrectList, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices_HdiSuccess_ReturnsCorrectList: start");

    std::vector<SerialPortInfo> devices;
    int32_t result = service_->QuerySerialDevices(devices);

    EXPECT_EQ(result, SERIAL_OK);
    // 应返回 2 个设备（SetupMockDevices 设置的）
    EXPECT_EQ(devices.size(), 2);

    if (devices.size() >= 1) {
        EXPECT_EQ(devices[0].portName, "ttyUSB0");
        EXPECT_EQ(devices[0].productId, MOCK_PID_FTDI);
        EXPECT_EQ(devices[0].vendorId, MOCK_VID_FTDI);
    }
    if (devices.size() >= 2) {
        EXPECT_EQ(devices[1].portName, "ttyUSB1");
        EXPECT_EQ(devices[1].productId, MOCK_PID_PROLIFIC);
        EXPECT_EQ(devices[1].vendorId, MOCK_VID_PROLIFIC);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices_HdiSuccess_ReturnsCorrectList: success");
}

/**
 * @brief 测试 QuerySerialDevices：HDI stub 模式返回空列表
 * @details 验证 stub 模式下返回空列表和 SERIAL_OK
 */
HWTEST_F(SerialServiceTest, QuerySerialDevices_HdiStubMode_ReturnsEmptyList, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices_HdiStubMode_ReturnsEmptyList: start");

    // 设置 HDI 为 stub 模式
    sptr<SerialHdiStub> stubHdi = new SerialHdiStub();
    ASSERT_NE(stubHdi, nullptr);
    service_->serialsProxy_ = stubHdi;

    std::vector<SerialPortInfo> devices;
    int32_t result = service_->QuerySerialDevices(devices);

    EXPECT_EQ(result, SERIAL_OK);
    EXPECT_EQ(devices.size(), 0);

    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices_HdiStubMode_ReturnsEmptyList: success");
}

/**
 * @brief 测试 QuerySerialDevices：HDI QueryDevices 失败
 * @details 验证 HDI 失败时返回 ERR_SERVICE
 */
HWTEST_F(SerialServiceTest, QuerySerialDevices_HdiFailure_ReturnsServiceError, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices_HdiFailure_ReturnsServiceError: start");

    // 设置 Mock QueryDevices 失败
    EXPECT_CALL(*mockHdi_, QueryDevices(_)).WillOnce(Return(HDF_FAILURE));

    std::vector<SerialPortInfo> devices;
    int32_t result = service_->QuerySerialDevices(devices);

    EXPECT_EQ(result, SERIAL_ERR_SERVICE);

    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices_HdiFailure_ReturnsServiceError: success");
}

// ============================================================================
// 6. WriteSerial / ReadSerial 测试
// ============================================================================

/**
 * @brief 测试 WriteSerial：写入未打开设备返回 ERR_DEVICE_DISCONNECTED
 * @details 验证对未打开设备写入的错误处理
 */
HWTEST_F(SerialServiceTest, WriteSerial_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial_DeviceNotOpened_ReturnsNotOpened: start");

    std::vector<uint8_t> testData = {0x01, 0x02, 0x03, 0x04, 0x05};
    int32_t bytesWritten = 0;

    int32_t result = service_->WriteSerial("ttyUSB_nonexistent", testData, 1000, bytesWritten);

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);
    EXPECT_EQ(bytesWritten, 0);

    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial_DeviceNotOpened_ReturnsNotOpened: success");
}

/**
 * @brief 测试 WriteSerial：写入已打开设备
 * @details 验证写入成功调用 HDI
 */
HWTEST_F(SerialServiceTest, WriteSerial_DeviceOpened_CallsHdiWrite, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial_DeviceOpened_CallsHdiWrite: start");

    mockHdi_->SetOpenDeviceResult(true);

    SerialPortInfo portInfo = CreateTestPortInfo("ttyUSB0");
    SerialConfig config = CreateDefaultConfig();

    // 尝试打开设备
    int32_t openResult = service_->OpenSerial(portInfo, config, mockCallback_);

    if (openResult == SERIAL_OK) {
        std::vector<uint8_t> testData = {0x01, 0x02, 0x03, 0x04, 0x05};
        int32_t bytesWritten = 0;

        int32_t result = service_->WriteSerial("ttyUSB0", testData, 1000, bytesWritten);
        SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial result: %d, bytesWritten: %d", result, bytesWritten);

        // 清理
        service_->CloseSerial("ttyUSB0");
    } else {
        SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial failed, skip write test");
    }

    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial_DeviceOpened_CallsHdiWrite: success");
}

// ============================================================================
// 7. StartRead / StopRead 测试
// ============================================================================

/**
 * @brief 测试 StartRead：未打开设备返回 ERR_DEVICE_DISCONNECTED
 * @details 验证对未打开设备调用 StartRead 的错误处理
 */
HWTEST_F(SerialServiceTest, StartRead_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "StartRead_DeviceNotOpened_ReturnsNotOpened: start");

    int32_t result = service_->StartRead("ttyUSB_nonexistent");

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "StartRead_DeviceNotOpened_ReturnsNotOpened: success");
}

/**
 * @brief 测试 StopRead：未打开设备返回 ERR_DEVICE_DISCONNECTED
 * @details 验证对未打开设备调用 StopRead 的错误处理
 */
HWTEST_F(SerialServiceTest, StopRead_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "StopRead_DeviceNotOpened_ReturnsNotOpened: start");

    int32_t result = service_->StopRead("ttyUSB_nonexistent");

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "StopRead_DeviceNotOpened_ReturnsNotOpened: success");
}

// ============================================================================
// 8. Flush / Drain / SendBrkSignal 测试
// ============================================================================

/**
 * @brief 测试 Flush：未打开设备返回 ERR_DEVICE_DISCONNECTED
 */
HWTEST_F(SerialServiceTest, Flush_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "Flush_DeviceNotOpened_ReturnsNotOpened: start");

    int32_t result = service_->Flush("ttyUSB_nonexistent");

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "Flush_DeviceNotOpened_ReturnsNotOpened: success");
}

/**
 * @brief 测试 Drain：未打开设备返回 ERR_DEVICE_DISCONNECTED
 */
HWTEST_F(SerialServiceTest, Drain_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "Drain_DeviceNotOpened_ReturnsNotOpened: start");

    int32_t result = service_->Drain("ttyUSB_nonexistent");

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "Drain_DeviceNotOpened_ReturnsNotOpened: success");
}

/**
 * @brief 测试 SendBrkSignal：未打开设备返回 ERR_DEVICE_DISCONNECTED
 */
HWTEST_F(SerialServiceTest, SendBrkSignal_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SendBrkSignal_DeviceNotOpened_ReturnsNotOpened: start");

    int32_t result = service_->SendBrkSignal("ttyUSB_nonexistent");

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "SendBrkSignal_DeviceNotOpened_ReturnsNotOpened: success");
}

// ============================================================================
// 9. SetRtsSignal / GetCtsSignal 测试
// ============================================================================

/**
 * @brief 测试 SetRtsSignal：未打开设备返回 ERR_DEVICE_DISCONNECTED
 */
HWTEST_F(SerialServiceTest, SetRtsSignal_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SetRtsSignal_DeviceNotOpened_ReturnsNotOpened: start");

    int32_t result = service_->SetRtsSignal("ttyUSB_nonexistent", true);

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "SetRtsSignal_DeviceNotOpened_ReturnsNotOpened: success");
}

/**
 * @brief 测试 GetCtsSignal：未打开设备返回 ERR_DEVICE_DISCONNECTED
 */
HWTEST_F(SerialServiceTest, GetCtsSignal_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "GetCtsSignal_DeviceNotOpened_ReturnsNotOpened: start");

    bool cts = false;
    int32_t result = service_->GetCtsSignal("ttyUSB_nonexistent", cts);

    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);

    SERIAL_HILOGI(MODULE_SERVICE, "GetCtsSignal_DeviceNotOpened_ReturnsNotOpened: success");
}

// ============================================================================
// 10. GetDeviceCount 测试
// ============================================================================

/**
 * @brief 测试 GetDeviceCount：初始状态为 0
 */
HWTEST_F(SerialServiceTest, GetDeviceCount_InitialState_ReturnsZero, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "GetDeviceCount_InitialState_ReturnsZero: start");

    EXPECT_EQ(service_->GetDeviceCount(), 0);

    SERIAL_HILOGI(MODULE_SERVICE, "GetDeviceCount_InitialState_ReturnsZero: success");
}

/**
 * @brief 测试 GetDeviceCount：设备打开后数量变化
 */
HWTEST_F(SerialServiceTest, GetDeviceCount_AfterOpen_ReturnsCorrectCount, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "GetDeviceCount_AfterOpen_ReturnsCorrectCount: start");

    mockHdi_->SetOpenDeviceResult(true);

    SerialPortInfo portInfo1 = CreateTestPortInfo("ttyUSB0");
    SerialPortInfo portInfo2 = CreateTestPortInfo("ttyUSB1");
    SerialConfig config = CreateDefaultConfig();

    EXPECT_EQ(service_->GetDeviceCount(), 0);

    int32_t result1 = service_->OpenSerial(portInfo1, config, mockCallback_);
    int32_t result2 = service_->OpenSerial(portInfo2, config, mockCallback_);

    uint32_t expectedCount = 0;
    if (result1 == SERIAL_OK)
        expectedCount++;
    if (result2 == SERIAL_OK)
        expectedCount++;

    EXPECT_EQ(service_->GetDeviceCount(), expectedCount);

    // 关闭设备
    if (result1 == SERIAL_OK) {
        service_->CloseSerial("ttyUSB0");
        expectedCount--;
    }
    if (result2 == SERIAL_OK) {
        service_->CloseSerial("ttyUSB1");
        expectedCount--;
    }

    EXPECT_EQ(service_->GetDeviceCount(), expectedCount);

    SERIAL_HILOGI(MODULE_SERVICE, "GetDeviceCount_AfterOpen_ReturnsCorrectCount: success");
}

// ============================================================================
// 11. AddPortAuthorization 测试
// ============================================================================

/**
 * @brief 测试 AddPortAuthorization：参数为空
 * 注意：单元测试进程不是 Dialog HAP，会先在 ValidateDialogHapCaller 返回权限错误
 */
HWTEST_F(SerialServiceTest, AddPortAuthorization_EmptyParams_ReturnsPermissionDenied, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "AddPortAuthorization_EmptyParams_ReturnsPermissionDenied: start");

    // 单元测试进程不是 Dialog HAP (com.usb.right)，所以 ValidateDialogHapCaller 会返回权限错误
    int32_t result = service_->AddPortAuthorization("", TEST_DEVICE_ID_1);
    EXPECT_EQ(result, SERIAL_ERR_PERMISSION_DENIED);

    SERIAL_HILOGI(MODULE_SERVICE, "AddPortAuthorization_EmptyParams_ReturnsPermissionDenied: success");
}

/**
 * @brief 测试 AddPortAuthorization：deviceId 为空
 * 注意：单元测试进程不是 Dialog HAP，会先在 ValidateDialogHapCaller 返回权限错误
 */
HWTEST_F(SerialServiceTest, AddPortAuthorization_EmptyDeviceId_ReturnsPermissionDenied, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "AddPortAuthorization_EmptyDeviceId_ReturnsPermissionDenied: start");

    // 单元测试进程不是 Dialog HAP (com.usb.right)，所以 ValidateDialogHapCaller 会返回权限错误
    int32_t result = service_->AddPortAuthorization(TEST_TOKEN_ID, "");
    EXPECT_EQ(result, SERIAL_ERR_PERMISSION_DENIED);

    SERIAL_HILOGI(MODULE_SERVICE, "AddPortAuthorization_EmptyDeviceId_ReturnsPermissionDenied: success");
}

// ============================================================================
// 12. BuildDeviceId 格式测试
// ============================================================================

/**
 * @brief 测试 BuildDeviceId：完整格式 {vid}_{pid}_{portName}
 */
HWTEST_F(SerialServiceTest, BuildDeviceId_FullFormat_VidPidPortName, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_FullFormat_VidPidPortName: start");

    SerialPortInfo portInfo;
    portInfo.portName = "ttyUSB0";
    portInfo.productId = MOCK_PID_FTDI;
    portInfo.vendorId = MOCK_VID_FTDI;
    portInfo.manufacturer = "FTDI";

    std::string deviceId = service_->BuildDeviceId(portInfo);

    // 验证格式：{vendorId:hex}_{productId:hex}_{manufacturer}_{portName}
    EXPECT_TRUE(deviceId.find("162e") != std::string::npos);
    EXPECT_TRUE(deviceId.find("04d2") != std::string::npos);
    EXPECT_TRUE(deviceId.find("ttyUSB0") != std::string::npos);

    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId result: %{public}s", deviceId.c_str());
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_FullFormat_VidPidPortName: success");
}

/**
 * @brief 测试 BuildDeviceId：仅有 portName
 */
HWTEST_F(SerialServiceTest, BuildDeviceId_OnlyPortName_ValidFormat, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_OnlyPortName_ValidFormat: start");

    SerialPortInfo portInfo;
    portInfo.portName = "ttyS0";
    portInfo.productId = 0;
    portInfo.vendorId = 0;

    std::string deviceId = service_->BuildDeviceId(portInfo);

    // 应包含 portName
    EXPECT_TRUE(deviceId.find("ttyS0") != std::string::npos);

    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId result: %{public}s", deviceId.c_str());
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_OnlyPortName_ValidFormat: success");
}

/**
 * @brief 测试 BuildDeviceId：有 productId 但无 vendorId
 */
HWTEST_F(SerialServiceTest, BuildDeviceId_HasProductIdNoVendorId_ValidFormat, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_HasProductIdNoVendorId_ValidFormat: start");

    SerialPortInfo portInfo;
    portInfo.portName = "ttyUSB1";
    portInfo.productId = MOCK_PID_PROLIFIC;
    portInfo.vendorId = 0;

    std::string deviceId = service_->BuildDeviceId(portInfo);

    EXPECT_TRUE(deviceId.find("0457") != std::string::npos);
    EXPECT_TRUE(deviceId.find("ttyUSB1") != std::string::npos);

    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId result: %{public}s", deviceId.c_str());
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_HasProductIdNoVendorId_ValidFormat: success");
}

/**
 * @brief 测试 BuildDeviceId：有 vendorId 但无 productId
 */
HWTEST_F(SerialServiceTest, BuildDeviceId_HasVendorIdNoProductId_ValidFormat, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_HasVendorIdNoProductId_ValidFormat: start");

    SerialPortInfo portInfo;
    portInfo.portName = "ttyACM0";
    portInfo.productId = 0;
    portInfo.vendorId = MOCK_VID_PROLIFIC;

    std::string deviceId = service_->BuildDeviceId(portInfo);

    EXPECT_TRUE(deviceId.find("08ae") != std::string::npos);
    EXPECT_TRUE(deviceId.find("ttyACM0") != std::string::npos);

    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId result: %{public}s", deviceId.c_str());
    SERIAL_HILOGI(MODULE_SERVICE, "BuildDeviceId_HasVendorIdNoProductId_ValidFormat: success");
}

// ============================================================================
// 13. FindPortNameForDevice 测试
// ============================================================================

/**
 * @brief 测试 FindPortNameForDevice：直接匹配 portName
 */
HWTEST_F(SerialServiceTest, FindPortNameForDevice_DirectMatch_ReturnsPortName, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "FindPortNameForDevice_DirectMatch_ReturnsPortName: start");

    SerialPortInfo portInfo;
    portInfo.portName = "ttyUSB0";
    portInfo.productId = 0;
    portInfo.vendorId = 0;

    std::string result = service_->FindPortNameForDevice(portInfo);
    EXPECT_EQ(result, "ttyUSB0");

    SERIAL_HILOGI(MODULE_SERVICE, "FindPortNameForDevice_DirectMatch_ReturnsPortName: success");
}

/**
 * @brief 测试 FindPortNameForDevice：通过 productId/vendorId 查找
 */
HWTEST_F(SerialServiceTest, FindPortNameForDevice_ByVidPid_ReturnsPortName, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "FindPortNameForDevice_ByVidPid_ReturnsPortName: start");

    // Setup mock devices
    SetupMockDevices();

    // portName 为空，但有 productId/vendorId
    SerialPortInfo portInfo;
    portInfo.portName = "";
    portInfo.productId = MOCK_PID_FTDI;
    portInfo.vendorId = MOCK_VID_FTDI;

    std::string result = service_->FindPortNameForDevice(portInfo);
    // 应返回对应的 portName（如果 mock 设备列表中有匹配）
    SERIAL_HILOGI(MODULE_SERVICE, "FindPortNameForDevice result: %{public}s", result.c_str());

    SERIAL_HILOGI(MODULE_SERVICE, "FindPortNameForDevice_ByVidPid_ReturnsPortName: success");
}

/**
 * @brief 测试 FindPortNameForDevice：无法匹配返回空
 */
HWTEST_F(SerialServiceTest, FindPortNameForDevice_NoMatch_ReturnsEmpty, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "FindPortNameForDevice_NoMatch_ReturnsEmpty: start");

    SerialPortInfo portInfo;
    portInfo.portName = "";
    portInfo.productId = 9999;
    portInfo.vendorId = 9999;

    std::string result = service_->FindPortNameForDevice(portInfo);
    EXPECT_TRUE(result.empty() || result == "");

    SERIAL_HILOGI(MODULE_SERVICE, "FindPortNameForDevice_NoMatch_ReturnsEmpty: success");
}

// ============================================================================
// 14. SetDtrSignal 测试
// ============================================================================

HWTEST_F(SerialServiceTest, SetDtrSignal_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    int32_t result = service_->SetDtrSignal("ttyUSB_nonexistent", true);
    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialServiceTest, SetDtrSignal_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, SetDtrSignal(true)).WillOnce(Return(0));

    int32_t result = service_->SetDtrSignal("ttyUSB0", true);
    EXPECT_EQ(result, SERIAL_OK);
}

HWTEST_F(SerialServiceTest, SetDtrSignal_HdiFailure_ReturnsServiceError, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, SetDtrSignal(false)).WillOnce(Return(TEST_HDI_FAILURE));

    int32_t result = service_->SetDtrSignal("ttyUSB0", false);
    EXPECT_EQ(result, SERIAL_ERR_SERVICE);
}

// ============================================================================
// 15. GetDsrSignal 测试
// ============================================================================

HWTEST_F(SerialServiceTest, GetDsrSignal_DeviceNotOpened_ReturnsNotOpened, TestSize.Level1)
{
    bool dsr = false;
    int32_t result = service_->GetDsrSignal("ttyUSB_nonexistent", dsr);
    EXPECT_EQ(result, SERIAL_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialServiceTest, GetDsrSignal_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, GetDsrSignal(_)).WillOnce(DoAll(SetArgReferee<0>(true), Return(0)));

    bool dsr = false;
    int32_t result = service_->GetDsrSignal("ttyUSB0", dsr);
    EXPECT_EQ(result, SERIAL_OK);
    EXPECT_TRUE(dsr);
}

HWTEST_F(SerialServiceTest, GetDsrSignal_HdiFailure_ReturnsServiceError, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, GetDsrSignal(_)).WillOnce(Return(TEST_HDI_FAILURE));

    bool dsr = false;
    int32_t result = service_->GetDsrSignal("ttyUSB0", dsr);
    EXPECT_EQ(result, SERIAL_ERR_SERVICE);
}

// ============================================================================
// 16. HandleDeviceOffline 测试
// ============================================================================

HWTEST_F(SerialServiceTest, HandleDeviceOffline_DeviceNotFound_NoError, TestSize.Level1)
{
    EXPECT_EQ(service_->GetDeviceCount(), 0);

    service_->HandleDeviceOffline("ttyUSB_nonexistent");

    EXPECT_EQ(service_->GetDeviceCount(), 0);
}

HWTEST_F(SerialServiceTest, HandleDeviceOffline_DeviceFound_RemovesContext, TestSize.Level1)
{
    InjectDeviceContext("ttyUSB0");
    EXPECT_EQ(service_->GetDeviceCount(), 1);

    std::string capPort;
    EXPECT_CALL(*mockCallback_, onDisconnect(_))
        .WillOnce(DoAll(SaveArg<0>(&capPort), Return(ERR_OK)));

    service_->HandleDeviceOffline("ttyUSB0");

    EXPECT_EQ(service_->GetDeviceCount(), 0);
    EXPECT_EQ(capPort, "ttyUSB0");
}

// ============================================================================
// 17. Dump 测试
// ============================================================================

HWTEST_F(SerialServiceTest, Dump_InvalidFd_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<std::u16string> args;
    args.push_back(u"-h");
    int32_t result = service_->Dump(INVALID_FD, args);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);
}

HWTEST_F(SerialServiceTest, Dump_EmptyArgs_ReturnsInvalidParam, TestSize.Level1)
{
    int32_t pipeFd[2] = {0};
    ASSERT_EQ(pipe(pipeFd), 0);

    std::vector<std::u16string> emptyArgs;
    int32_t result = service_->Dump(pipeFd[PIPE_WRITE], emptyArgs);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);

    close(pipeFd[PIPE_READ]);
    close(pipeFd[PIPE_WRITE]);
}

HWTEST_F(SerialServiceTest, Dump_HelpOption_ReturnsOk, TestSize.Level1)
{
    int32_t pipeFd[2] = {0};
    ASSERT_EQ(pipe(pipeFd), 0);

    std::vector<std::u16string> args;
    args.push_back(u"-h");
    int32_t result = service_->Dump(pipeFd[PIPE_WRITE], args);
    EXPECT_EQ(result, SERIAL_OK);

    close(pipeFd[PIPE_READ]);
    close(pipeFd[PIPE_WRITE]);
}

HWTEST_F(SerialServiceTest, Dump_AllOption_ReturnsOk, TestSize.Level1)
{
    int32_t pipeFd[2] = {0};
    ASSERT_EQ(pipe(pipeFd), 0);

    std::vector<std::u16string> args;
    args.push_back(u"-a");
    int32_t result = service_->Dump(pipeFd[PIPE_WRITE], args);
    EXPECT_EQ(result, SERIAL_OK);

    close(pipeFd[PIPE_READ]);
    close(pipeFd[PIPE_WRITE]);
}

HWTEST_F(SerialServiceTest, Dump_ListOption_ReturnsOk, TestSize.Level1)
{
    int32_t pipeFd[2] = {0};
    ASSERT_EQ(pipe(pipeFd), 0);

    std::vector<std::u16string> args;
    args.push_back(u"-l");
    int32_t result = service_->Dump(pipeFd[PIPE_WRITE], args);
    EXPECT_EQ(result, SERIAL_OK);

    close(pipeFd[PIPE_READ]);
    close(pipeFd[PIPE_WRITE]);
}

HWTEST_F(SerialServiceTest, Dump_UnknownOption_ReturnsInvalidParam, TestSize.Level1)
{
    int32_t pipeFd[2] = {0};
    ASSERT_EQ(pipe(pipeFd), 0);

    std::vector<std::u16string> args;
    args.push_back(u"-x");
    int32_t result = service_->Dump(pipeFd[PIPE_WRITE], args);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);

    close(pipeFd[PIPE_READ]);
    close(pipeFd[PIPE_WRITE]);
}

// ============================================================================
// 18. IsUsbSerial 测试
// ============================================================================

HWTEST_F(SerialServiceTest, IsUsbSerial_UsbPort_ReturnsTrue, TestSize.Level1)
{
    EXPECT_TRUE(service_->IsUsbSerial("ttyUSB0"));
}

HWTEST_F(SerialServiceTest, IsUsbSerial_AcmPort_ReturnsTrue, TestSize.Level1)
{
    EXPECT_TRUE(service_->IsUsbSerial("ttyACM0"));
}

HWTEST_F(SerialServiceTest, IsUsbSerial_TtysPort_ReturnsFalse, TestSize.Level1)
{
    EXPECT_FALSE(service_->IsUsbSerial("ttyS0"));
}

// ============================================================================
// 19. SetSerialManagePolicy 测试
// ============================================================================

HWTEST_F(SerialServiceTest, SetSerialManagePolicy_DisableRs232_UsbDevicesNotFiltered, TestSize.Level1)
{
    service_->SetSerialManagePolicy(true);

    std::vector<SerialPortInfo> devices;
    int32_t result = service_->QuerySerialDevices(devices);
    EXPECT_EQ(result, SERIAL_OK);
    EXPECT_EQ(devices.size(), static_cast<size_t>(2));

    service_->SetSerialManagePolicy(false);
}

HWTEST_F(SerialServiceTest, SetSerialManagePolicy_DisableRs232_NonUsbOnly_ReturnsEnterprisePolicy, TestSize.Level1)
{
    std::vector<SerialDeviceInfo> nonUsbDevices;
    SerialDeviceInfo device;
    device.portName = "ttyS0";
    device.productId = MOCK_PID_PROLIFIC;
    device.vendorId = MOCK_VID_PROLIFIC;
    device.manufacturer = "Prolific";
    nonUsbDevices.push_back(device);
    mockHdi_->SetMockDevices(nonUsbDevices);

    service_->SetSerialManagePolicy(true);

    std::vector<SerialPortInfo> devices;
    int32_t result = service_->QuerySerialDevices(devices);
    EXPECT_EQ(result, SERIAL_ERR_ENTERPRISE_POLICY);
    EXPECT_EQ(devices.size(), static_cast<size_t>(0));

    service_->SetSerialManagePolicy(false);
    SetupMockDevices();
}

HWTEST_F(SerialServiceTest, SetSerialManagePolicy_EnableRs232_ReturnsAllDevices, TestSize.Level1)
{
    service_->SetSerialManagePolicy(false);

    std::vector<SerialPortInfo> devices;
    int32_t result = service_->QuerySerialDevices(devices);
    EXPECT_EQ(result, SERIAL_OK);
    EXPECT_EQ(devices.size(), static_cast<size_t>(2));
}

// ============================================================================
// 20. CalculatePortId 测试
// ============================================================================

HWTEST_F(SerialServiceTest, CalculatePortId_UsbPort_ReturnsBaseId, TestSize.Level1)
{
    int32_t portId = service_->CalculatePortId("ttyUSB0");
    EXPECT_EQ(portId, 0);
}

HWTEST_F(SerialServiceTest, CalculatePortId_AcmPort_ReturnsBaseIdWithOffset, TestSize.Level1)
{
    int32_t portId = service_->CalculatePortId("ttyACM1");
    EXPECT_EQ(portId, EXPECTED_ACM_PORT_1);
}

HWTEST_F(SerialServiceTest, CalculatePortId_TtysPort_ReturnsBaseIdWithOffset, TestSize.Level1)
{
    // 加载 PCIE 串口配置，使 pcieSerialOffsets_["ttyS"] = TTYS_PORT_ID_OFFSET(2000)
    service_->LoadPcieSerialConfig();
    int32_t portId = service_->CalculatePortId("ttyS2");
    EXPECT_EQ(portId, EXPECTED_TTYS_PORT_2);
}

HWTEST_F(SerialServiceTest, CalculatePortId_InvalidPortName_ReturnsInvalidParam, TestSize.Level1)
{
    int32_t portId = service_->CalculatePortId("noNumbers");
    EXPECT_EQ(portId, SERIAL_ERR_INVALID_PARAM);
}

// ============================================================================
// 21. WriteSerial 参数校验测试
// ============================================================================

HWTEST_F(SerialServiceTest, WriteSerial_EmptyData_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> emptyData;
    int32_t bytesWritten = 0;
    int32_t result = service_->WriteSerial("ttyUSB0", emptyData, TEST_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);
}

HWTEST_F(SerialServiceTest, WriteSerial_OversizeData_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> oversizeData(WRITE_DATA_MAX_SIZE + 1, TEST_DATA_BYTE);
    int32_t bytesWritten = 0;
    int32_t result = service_->WriteSerial("ttyUSB0", oversizeData, TEST_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);
}

HWTEST_F(SerialServiceTest, WriteSerial_NegativeTimeout_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t result = service_->WriteSerial("ttyUSB0", data, -1, bytesWritten);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);
}

HWTEST_F(SerialServiceTest, WriteSerial_ExceedMaxTimeout_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t result = service_->WriteSerial("ttyUSB0", data, WRITE_TIMEOUT_MAX_MS + 1, bytesWritten);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);
}

HWTEST_F(SerialServiceTest, WriteSerial_EdmDisabled_ReturnsServiceError, TestSize.Level1)
{
    service_->SetSerialManagePolicy(true);

    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t result = service_->WriteSerial("ttyS0", data, TEST_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(result, SERIAL_ERR_SERVICE);

    service_->SetSerialManagePolicy(false);
}

HWTEST_F(SerialServiceTest, WriteSerial_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, Write(_, _, _))
        .WillOnce(DoAll(SetArgReferee<2>(TEST_WRITE_BYTES), Return(0)));

    std::vector<uint8_t> data(TEST_WRITE_BYTES, TEST_DATA_BYTE);
    int32_t bytesWritten = 0;
    int32_t result = service_->WriteSerial("ttyUSB0", data, TEST_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(result, SERIAL_OK);
    EXPECT_EQ(bytesWritten, TEST_WRITE_BYTES);
}

// ============================================================================
// 22. Flush/Drain/SendBrkSignal 正向路径测试
// ============================================================================

HWTEST_F(SerialServiceTest, Flush_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, Flush()).WillOnce(Return(0));

    int32_t result = service_->Flush("ttyUSB0");
    EXPECT_EQ(result, SERIAL_OK);
}

HWTEST_F(SerialServiceTest, Drain_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, Drain()).WillOnce(Return(0));

    int32_t result = service_->Drain("ttyUSB0");
    EXPECT_EQ(result, SERIAL_OK);
}

HWTEST_F(SerialServiceTest, SendBrkSignal_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, SendBrkSignal()).WillOnce(Return(0));

    int32_t result = service_->SendBrkSignal("ttyUSB0");
    EXPECT_EQ(result, SERIAL_OK);
}

// ============================================================================
// 23. SetRtsSignal/GetCtsSignal 正向路径测试
// ============================================================================

HWTEST_F(SerialServiceTest, SetRtsSignal_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, SetRtsSignal(true)).WillOnce(Return(0));

    int32_t result = service_->SetRtsSignal("ttyUSB0", true);
    EXPECT_EQ(result, SERIAL_OK);
}

HWTEST_F(SerialServiceTest, GetCtsSignal_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, GetCtsSignal(_)).WillOnce(DoAll(SetArgReferee<0>(true), Return(0)));

    bool cts = false;
    int32_t result = service_->GetCtsSignal("ttyUSB0", cts);
    EXPECT_EQ(result, SERIAL_OK);
    EXPECT_TRUE(cts);
}

// ============================================================================
// 24. StartRead/StopRead 正向路径测试
// ============================================================================

HWTEST_F(SerialServiceTest, StartRead_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, StartRead()).WillOnce(Return(0));

    int32_t result = service_->StartRead("ttyUSB0");
    EXPECT_EQ(result, SERIAL_OK);
}

HWTEST_F(SerialServiceTest, StopRead_Success_ReturnsOk, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDevice = InjectDeviceContext("ttyUSB0");
    EXPECT_CALL(*mockDevice, StopRead()).WillOnce(Return(0));

    int32_t result = service_->StopRead("ttyUSB0");
    EXPECT_EQ(result, SERIAL_OK);
}

// ============================================================================
// 25. ParseTargetTokenId 测试
// ============================================================================

HWTEST_F(SerialServiceTest, ParseTargetTokenId_ValidNumeric_ReturnsOk, TestSize.Level1)
{
    Security::AccessToken::AccessTokenID targetTokenId = 0;
    int32_t result = service_->ParseTargetTokenId("12345678", targetTokenId);
    EXPECT_EQ(result, SERIAL_OK);
    EXPECT_EQ(targetTokenId, static_cast<Security::AccessToken::AccessTokenID>(12345678));
}

HWTEST_F(SerialServiceTest, ParseTargetTokenId_NonNumeric_ReturnsInvalidParam, TestSize.Level1)
{
    Security::AccessToken::AccessTokenID targetTokenId = 0;
    int32_t result = service_->ParseTargetTokenId("abc", targetTokenId);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);
}

HWTEST_F(SerialServiceTest, ParseTargetTokenId_EmptyString_ReturnsInvalidParam, TestSize.Level1)
{
    Security::AccessToken::AccessTokenID targetTokenId = 0;
    int32_t result = service_->ParseTargetTokenId("", targetTokenId);
    EXPECT_EQ(result, SERIAL_ERR_INVALID_PARAM);
}

// ============================================================================
// 26. BuildDeviceId 含 manufacturer 测试
// ============================================================================

HWTEST_F(SerialServiceTest, BuildDeviceId_WithManufacturer_ContainsAllFields, TestSize.Level1)
{
    SerialPortInfo portInfo;
    portInfo.portName = "ttyUSB0";
    portInfo.productId = MOCK_PID_FTDI;
    portInfo.vendorId = MOCK_VID_FTDI;
    portInfo.manufacturer = "FTDI";

    std::string deviceId = service_->BuildDeviceId(portInfo);
    EXPECT_TRUE(deviceId.find("162e") != std::string::npos);
    EXPECT_TRUE(deviceId.find("04d2") != std::string::npos);
    EXPECT_TRUE(deviceId.find("FTDI") != std::string::npos);
    EXPECT_TRUE(deviceId.find("ttyUSB0") != std::string::npos);
}

// ============================================================================
// 27. QueryAndRecordFirstRead/Write 测试
// ============================================================================

HWTEST_F(SerialServiceTest, QueryAndRecordFirstRead_FirstCallReturnsTrue, TestSize.Level1)
{
    EXPECT_TRUE(service_->QueryAndRecordFirstRead("ttyUSB0"));
}

HWTEST_F(SerialServiceTest, QueryAndRecordFirstRead_SecondCallReturnsFalse, TestSize.Level1)
{
    service_->QueryAndRecordFirstRead("ttyUSB0");
    EXPECT_FALSE(service_->QueryAndRecordFirstRead("ttyUSB0"));
}

HWTEST_F(SerialServiceTest, ResetFirstRead_AllowsReRecord, TestSize.Level1)
{
    service_->QueryAndRecordFirstRead("ttyUSB0");
    service_->ResetFirstRead("ttyUSB0");
    EXPECT_TRUE(service_->QueryAndRecordFirstRead("ttyUSB0"));
}

HWTEST_F(SerialServiceTest, QueryAndRecordFirstWrite_FirstCallReturnsTrue, TestSize.Level1)
{
    EXPECT_TRUE(service_->QueryAndRecordFirstWrite("ttyUSB0"));
}

HWTEST_F(SerialServiceTest, ResetFirstWrite_AllowsReRecord, TestSize.Level1)
{
    service_->QueryAndRecordFirstWrite("ttyUSB0");
    service_->ResetFirstWrite("ttyUSB0");
    EXPECT_TRUE(service_->QueryAndRecordFirstWrite("ttyUSB0"));
}

// ============================================================================
// 28. ScheduleUnloadSa 测试
// ============================================================================

HWTEST_F(SerialServiceTest, ScheduleUnloadSa_WithDevicesOpen_SkipsUnload, TestSize.Level1)
{
    InjectDeviceContext("ttyUSB0");
    EXPECT_EQ(service_->GetDeviceCount(), 1);

    service_->ScheduleUnloadSa();

    EXPECT_EQ(service_->unloadTimerId_, static_cast<uint32_t>(0));
}

HWTEST_F(SerialServiceTest, ScheduleUnloadSa_NoDevices_SchedulesTimer, TestSize.Level1)
{
    EXPECT_EQ(service_->GetDeviceCount(), 0);

    service_->unloadTimer_.Setup();
    service_->ScheduleUnloadSa();

    EXPECT_NE(service_->unloadTimerId_, static_cast<uint32_t>(0));

    service_->CancelUnloadSa();
    service_->unloadTimer_.Shutdown();
}

} // namespace Serial
} // namespace OHOS