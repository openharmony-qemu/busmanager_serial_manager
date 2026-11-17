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
 * @file serial_mock_hdi_param_test.cpp
 * @brief Mock HDI 参数校验矩阵测试（对应覆盖表 develop/paramtest 模块）
 * @details 覆盖参数校验的状态机路径（全部确定性断言，无需真实设备）：
 *          - OpenSerial：baudRate/dataBits/stopBits/parity 越界、空回调 token
 *          - WriteSerial：空数据/超大数据/负超时/超时上限、未打开端口、非授权调用方
 *          - CloseSerial/StartRead/GetCtsSignal：未打开端口
 *          - OpenSerial 端口不在线（枚举列表无此端口）
 *          校验失败路径先于权限检查执行，与权限环境无关，可在无真实设备环境执行。
 * @since 2026-05-12
 */

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hilog_wrapper.h"
#include "mock_serial_hdi.h"
#include "mock_hdi_test_helper.h"
#include "serial_error_code.h"
#include "serial_service.h"
#include "serial_service_callback.h"
#include "system_ability_definition.h"

using namespace testing;
using namespace testing::ext;
using namespace OHOS::Serial;
using namespace OHOS::HDI::Serial::V1_0;

namespace OHOS {
namespace Serial {

namespace {
constexpr int32_t TEST_BAUD_RATE = 115200;
constexpr int32_t BAUD_RATE_INVALID_ZERO = 0;
constexpr int32_t TEST_DATA_BITS = 8;
constexpr int32_t DATA_BITS_BELOW_MIN = 4;
constexpr int32_t DATA_BITS_ABOVE_MAX = 9;
constexpr int32_t TEST_STOP_BITS = 1;
constexpr int32_t STOP_BITS_BELOW_MIN = 0;
constexpr int32_t STOP_BITS_ABOVE_MAX = 3;
constexpr int32_t TEST_PARITY_NONE = 0;
constexpr int32_t PARITY_BELOW_MIN = -1;
constexpr int32_t PARITY_ABOVE_MAX = 5;
constexpr int32_t TEST_WRITE_TIMEOUT_MS = 1000;
constexpr int32_t TIMEOUT_NEGATIVE = -1;
constexpr int32_t TIMEOUT_ABOVE_MAX = 300001;
constexpr size_t WRITE_DATA_OVER_LIMIT = 4097; // 服务端 WRITE_DATA_MAX_SIZE(4096) + 1
constexpr uint8_t FILL_BYTE = 0x41;
constexpr int32_t MOCK_VID_FTDI = 5678;
constexpr int32_t MOCK_PID_FTDI = 1234;
constexpr const char *PORT_USB0 = "ttyUSB0";
constexpr const char *PORT_NOT_EXIST = "ttyNonExist";
constexpr const char *MFR_FTDI = "FTDI";
constexpr const char *TOKEN_MISMATCH = "999999999";
} // namespace

/**
 * @class MockSerialServiceCallback
 * @brief Mock 的 ISerialServiceCallback（服务端→客户端回调）
 */
class MockSerialServiceCallback : public SerialServiceCallbackStub {
public:
    MockSerialServiceCallback() = default;
    ~MockSerialServiceCallback() override = default;

    MOCK_METHOD(ErrCode, onDataRead, (const std::string &portName, const std::vector<uint8_t> &buffer), (override));
    MOCK_METHOD(ErrCode, onDisconnect, (const std::string &portName), (override));
};

/**
 * @class SerialMockHdiParamTest
 * @brief 参数校验矩阵测试夹具
 */
class SerialMockHdiParamTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiParamTest::SetUpTestCase");
    }

    static void TearDownTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiParamTest::TearDownTestCase");
    }

    void SetUp() override;
    void TearDown() override;

protected:
    sptr<MockSerials> mockHdi_ = nullptr;
    sptr<SerialService> service_ = nullptr;
    sptr<MockSerialServiceCallback> mockCallback_ = nullptr;

    SerialConfig CreateValidConfig() const;
    SerialConfig CreateConfigWithBaudRate(int32_t baudRate) const;
    SerialPortInfo CreateUsbPortInfo() const;
};

void SerialMockHdiParamTest::SetUp()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiParamTest::SetUp");
    mockHdi_ = new MockSerials();
    ASSERT_NE(mockHdi_, nullptr);

    std::vector<SerialDeviceInfo> mockDevices;
    SerialDeviceInfo usb0;
    usb0.portName = PORT_USB0;
    usb0.productId = MOCK_PID_FTDI;
    usb0.vendorId = MOCK_VID_FTDI;
    usb0.manufacturer = MFR_FTDI;
    mockDevices.push_back(usb0);
    mockHdi_->SetMockDevices(mockDevices);

    service_ = new SerialService(SERIAL_SYSTEM_ABILITY_ID, false);
    ASSERT_NE(service_, nullptr);
    service_->serialsProxy_ = mockHdi_;

    mockCallback_ = new MockSerialServiceCallback();
    ASSERT_NE(mockCallback_, nullptr);
}

void SerialMockHdiParamTest::TearDown()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiParamTest::TearDown");
    CleanupTestService(service_);
    mockCallback_ = nullptr;
    mockHdi_ = nullptr;
    service_ = nullptr;
}

SerialConfig SerialMockHdiParamTest::CreateValidConfig() const
{
    SerialConfig config;
    config.baudRate = TEST_BAUD_RATE;
    config.dataBits = TEST_DATA_BITS;
    config.stopBits = TEST_STOP_BITS;
    config.parity = TEST_PARITY_NONE;
    config.rtscts = false;
    config.xon = false;
    config.xoff = false;
    config.xany = false;
    return config;
}

SerialConfig SerialMockHdiParamTest::CreateConfigWithBaudRate(int32_t baudRate) const
{
    SerialConfig config = CreateValidConfig();
    config.baudRate = baudRate;
    return config;
}

SerialPortInfo SerialMockHdiParamTest::CreateUsbPortInfo() const
{
    SerialPortInfo info;
    info.portName = PORT_USB0;
    info.productId = MOCK_PID_FTDI;
    info.vendorId = MOCK_VID_FTDI;
    info.manufacturer = MFR_FTDI;
    return info;
}

// ============================================================================
// 1. OpenSerial 参数校验
// ============================================================================

/**
 * @brief baudRate 为 0 时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_BaudRateZero_ReturnsInvalidParam, TestSize.Level1)
{
    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), CreateConfigWithBaudRate(BAUD_RATE_INVALID_ZERO),
        mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief dataBits 低于下限（4）时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_DataBitsBelowMin_ReturnsInvalidParam, TestSize.Level1)
{
    SerialConfig config = CreateValidConfig();
    config.dataBits = DATA_BITS_BELOW_MIN;

    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), config, mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief dataBits 高于上限（9）时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_DataBitsAboveMax_ReturnsInvalidParam, TestSize.Level1)
{
    SerialConfig config = CreateValidConfig();
    config.dataBits = DATA_BITS_ABOVE_MAX;

    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), config, mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief stopBits 为 0 时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_StopBitsZero_ReturnsInvalidParam, TestSize.Level1)
{
    SerialConfig config = CreateValidConfig();
    config.stopBits = STOP_BITS_BELOW_MIN;

    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), config, mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief stopBits 为 3 时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_StopBitsAboveMax_ReturnsInvalidParam, TestSize.Level1)
{
    SerialConfig config = CreateValidConfig();
    config.stopBits = STOP_BITS_ABOVE_MAX;

    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), config, mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief parity 为负值时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_ParityNegative_ReturnsInvalidParam, TestSize.Level1)
{
    SerialConfig config = CreateValidConfig();
    config.parity = PARITY_BELOW_MIN;

    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), config, mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief parity 高于上限（5）时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_ParityAboveMax_ReturnsInvalidParam, TestSize.Level1)
{
    SerialConfig config = CreateValidConfig();
    config.parity = PARITY_ABOVE_MAX;

    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), config, mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief 回调 token 为空时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_NullToken_ReturnsInvalidParam, TestSize.Level1)
{
    int32_t ret = service_->OpenSerial(CreateUsbPortInfo(), CreateValidConfig(), nullptr);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief 请求端口不在枚举结果中时返回设备断开
 */
HWTEST_F(SerialMockHdiParamTest, Param_Open_PortNotInEnum_ReturnsDisconnected, TestSize.Level1)
{
    SerialPortInfo info;
    info.portName = PORT_NOT_EXIST;
    info.productId = MOCK_PID_FTDI;
    info.vendorId = MOCK_VID_FTDI;
    info.manufacturer = MFR_FTDI;

    int32_t ret = service_->OpenSerial(info, CreateValidConfig(), mockCallback_);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_DISCONNECTED);
}

// ============================================================================
// 2. WriteSerial 参数校验
// ============================================================================

/**
 * @brief 写入空数据时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Write_EmptyData_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> emptyData;
    int32_t bytesWritten = 0;

    int32_t ret = service_->WriteSerial(PORT_USB0, emptyData, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief 写入数据超过 4096 上限时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Write_DataTooLarge_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> bigData(WRITE_DATA_OVER_LIMIT, FILL_BYTE);
    int32_t bytesWritten = 0;

    int32_t ret = service_->WriteSerial(PORT_USB0, bigData, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief 写入超时为负值时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Write_NegativeTimeout_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> data = {FILL_BYTE};
    int32_t bytesWritten = 0;

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TIMEOUT_NEGATIVE, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief 写入超时超过 300000ms 上限时返回参数错误
 */
HWTEST_F(SerialMockHdiParamTest, Param_Write_TimeoutAboveMax_ReturnsInvalidParam, TestSize.Level1)
{
    std::vector<uint8_t> data = {FILL_BYTE};
    int32_t bytesWritten = 0;

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TIMEOUT_ABOVE_MAX, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief 端口未打开时写入返回设备未打开
 */
HWTEST_F(SerialMockHdiParamTest, Param_Write_NotOpened_ReturnsDeviceNotOpened, TestSize.Level1)
{
    std::vector<uint8_t> data = {FILL_BYTE};
    int32_t bytesWritten = 0;

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_NOT_OPENED);
}

/**
 * @brief 非授权调用方（tokenId 不匹配）写入返回 NOT_ALLOWED
 */
HWTEST_F(SerialMockHdiParamTest, Param_Write_TokenMismatch_ReturnsNotAllowed, TestSize.Level1)
{
    // 复用共享 helper 注入 tokenId 不匹配的上下文（唯一差异点为 tokenId）
    InsertTestDeviceContextWithToken(service_, PORT_USB0, mockCallback_, CreateValidConfig(), TOKEN_MISMATCH);

    std::vector<uint8_t> data = {FILL_BYTE};
    int32_t bytesWritten = 0;

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_NOT_ALLOWED);
}

// ============================================================================
// 3. 其他方法「端口未打开」路径
// ============================================================================

/**
 * @brief 关闭未打开端口返回设备未打开
 */
HWTEST_F(SerialMockHdiParamTest, Param_Close_NotOpened_ReturnsDeviceNotOpened, TestSize.Level1)
{
    int32_t ret = service_->CloseSerial(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_NOT_OPENED);
}

/**
 * @brief 启动读取未打开端口返回设备未打开
 */
HWTEST_F(SerialMockHdiParamTest, Param_StartRead_NotOpened_ReturnsDeviceNotOpened, TestSize.Level1)
{
    int32_t ret = service_->StartRead(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_NOT_OPENED);
}

/**
 * @brief 读取 CTS 未打开端口返回设备未打开
 */
HWTEST_F(SerialMockHdiParamTest, Param_GetCts_NotOpened_ReturnsDeviceNotOpened, TestSize.Level1)
{
    bool cts = false;

    int32_t ret = service_->GetCtsSignal(PORT_USB0, cts);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_NOT_OPENED);
}

} // namespace Serial
} // namespace OHOS
