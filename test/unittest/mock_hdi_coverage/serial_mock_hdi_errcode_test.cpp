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
 * @file serial_mock_hdi_errcode_test.cpp
 * @brief Mock HDI 错误注入矩阵测试（对应覆盖表 develop/errcodetest 模块）
 * @details 通过 MockSerialDevice/MockSerials 按用例注入 HDI 错误码，
 *          验证 14 个 IDL 方法的 HDI→内部错误码映射（SerialHdiToInternalCode）：
 *          HDF_ERR_IO→断开、HDF_ERR_TIMEOUT→超时、HDF_ERR_INVALID_PARAM→参数错误、
 *          HDF_ERR_DEVICE_BUSY→设备忙、HDF_ERR_BAD_FD→未打开、HDF_ERR_NOPERM→权限拒绝、
 *          其余 HDF 失败→服务错误；Close 失败仅告警仍清理上下文。
 *          全部用例基于预注入上下文（tokenId 与调用方一致）确定性断言，
 *          可在无真实设备、无真实权限环境执行；无真实设备时默认成功语义见 RealDeviceProbe。
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
constexpr int32_t TEST_DATA_BITS = 8;
constexpr int32_t TEST_STOP_BITS = 1;
constexpr int32_t TEST_PARITY_NONE = 0;
constexpr int32_t TEST_WRITE_TIMEOUT_MS = 1000;
constexpr int32_t MOCK_VID_FTDI = 5678;
constexpr int32_t MOCK_PID_FTDI = 1234;
constexpr int32_t WRITE_RETURN_ALL_BYTES = 5;
constexpr const char *PORT_USB0 = "ttyUSB0";
constexpr const char *MFR_FTDI = "FTDI";
constexpr uint8_t LOOPBACK_BYTE_0 = 0x01;
constexpr uint8_t LOOPBACK_BYTE_1 = 0x02;
constexpr uint8_t LOOPBACK_BYTE_2 = 0x03;
constexpr uint8_t LOOPBACK_BYTE_3 = 0x04;
constexpr uint8_t LOOPBACK_BYTE_4 = 0x05;
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
 * @class SerialMockHdiErrcodeTest
 * @brief HDI 错误注入矩阵测试夹具
 */
class SerialMockHdiErrcodeTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiErrcodeTest::SetUpTestCase");
    }

    static void TearDownTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiErrcodeTest::TearDownTestCase");
    }

    void SetUp() override;
    void TearDown() override;

protected:
    sptr<MockSerials> mockHdi_ = nullptr;
    sptr<SerialService> service_ = nullptr;
    sptr<MockSerialServiceCallback> mockCallback_ = nullptr;

    SerialConfig CreateDefaultConfig() const;
    sptr<MockSerialDevice> InsertOpenedPort(const std::string &portName);
};

void SerialMockHdiErrcodeTest::SetUp()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiErrcodeTest::SetUp");
    // NiceMock：抑制未预期调用的 GMOCK WARNING，未显式声明的调用仍执行 ON_CALL 默认动作
    mockHdi_ = new NiceMock<MockSerials>();
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

void SerialMockHdiErrcodeTest::TearDown()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiErrcodeTest::TearDown");
    CleanupTestService(service_);
    mockCallback_ = nullptr;
    mockHdi_ = nullptr;
    service_ = nullptr;
}

SerialConfig SerialMockHdiErrcodeTest::CreateDefaultConfig() const
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

sptr<MockSerialDevice> SerialMockHdiErrcodeTest::InsertOpenedPort(const std::string &portName)
{
    return InsertTestDeviceContext(service_, portName, mockCallback_, CreateDefaultConfig());
}

// ============================================================================
// 1. WriteSerial HDI 错误注入（5 种 HDI 错误码 → 内部错误码映射）
// ============================================================================

/**
 * @brief HDI Write 返回 HDF_ERR_IO → 映射为设备断开
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Write_HdfIo_ReturnsDisconnected, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    std::vector<uint8_t> data = {LOOPBACK_BYTE_0, LOOPBACK_BYTE_1, LOOPBACK_BYTE_2};
    int32_t bytesWritten = 0;
    EXPECT_CALL(*mockDev, Write(data, TEST_WRITE_TIMEOUT_MS, _)).WillOnce(Return(HDF_ERR_IO));

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_DISCONNECTED);
}

/**
 * @brief HDI Write 返回 HDF_ERR_TIMEOUT → 映射为超时
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Write_HdfTimeout_ReturnsTimeout, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    std::vector<uint8_t> data = {LOOPBACK_BYTE_0};
    int32_t bytesWritten = 0;
    EXPECT_CALL(*mockDev, Write(data, TEST_WRITE_TIMEOUT_MS, _)).WillOnce(Return(HDF_ERR_TIMEOUT));

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_TIMEOUT);
}

/**
 * @brief HDI Write 返回 HDF_ERR_INVALID_PARAM → 映射为参数错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Write_HdfInvalidParam_ReturnsInvalidParam, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    std::vector<uint8_t> data = {LOOPBACK_BYTE_0};
    int32_t bytesWritten = 0;
    EXPECT_CALL(*mockDev, Write(data, TEST_WRITE_TIMEOUT_MS, _)).WillOnce(Return(HDF_ERR_INVALID_PARAM));

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_INVALID_PARAM);
}

/**
 * @brief HDI Write 返回 HDF_ERR_DEVICE_BUSY → 映射为设备忙
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Write_HdfDeviceBusy_ReturnsBusy, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    std::vector<uint8_t> data = {LOOPBACK_BYTE_0};
    int32_t bytesWritten = 0;
    EXPECT_CALL(*mockDev, Write(data, TEST_WRITE_TIMEOUT_MS, _)).WillOnce(Return(HDF_ERR_DEVICE_BUSY));

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_BUSY);
}

/**
 * @brief HDI Write 返回 HDF_ERR_BAD_FD → 映射为设备未打开
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Write_HdfBadFd_ReturnsNotOpened, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    std::vector<uint8_t> data = {LOOPBACK_BYTE_0};
    int32_t bytesWritten = 0;
    EXPECT_CALL(*mockDev, Write(data, TEST_WRITE_TIMEOUT_MS, _)).WillOnce(Return(HDF_ERR_BAD_FD));

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_NOT_OPENED);
}

/**
 * @brief 正向对照：HDI Write 成功时返回实际写入字节数
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Write_Success_ReturnsBytesWritten, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    std::vector<uint8_t> data = {LOOPBACK_BYTE_0, LOOPBACK_BYTE_1, LOOPBACK_BYTE_2, LOOPBACK_BYTE_3, LOOPBACK_BYTE_4};
    int32_t bytesWritten = 0;
    EXPECT_CALL(*mockDev, Write(data, TEST_WRITE_TIMEOUT_MS, _))
        .WillOnce(DoAll(SetArgReferee<2>(WRITE_RETURN_ALL_BYTES), Return(HDF_SUCCESS)));

    int32_t ret = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_EQ(bytesWritten, WRITE_RETURN_ALL_BYTES);
}

// ============================================================================
// 2. 读写控制 HDI 错误注入
// ============================================================================

/**
 * @brief HDI StartRead 返回 HDF_FAILURE → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_StartRead_HdfFailure_ReturnsService, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, StartRead()).WillOnce(Return(HDF_FAILURE));

    int32_t ret = service_->StartRead(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
}

/**
 * @brief HDI StopRead 返回 HDF_FAILURE → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_StopRead_HdfFailure_ReturnsService, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, StopRead()).WillOnce(Return(HDF_FAILURE));

    int32_t ret = service_->StopRead(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
}

// ============================================================================
// 3. 缓冲区与断帧信号 HDI 错误注入
// ============================================================================

/**
 * @brief HDI Flush 返回 HDF_FAILURE → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Flush_HdfFailure_ReturnsService, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, Flush()).WillOnce(Return(HDF_FAILURE));

    int32_t ret = service_->Flush(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
}

/**
 * @brief HDI Drain 返回 HDF_FAILURE → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Drain_HdfFailure_ReturnsService, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, Drain()).WillOnce(Return(HDF_FAILURE));

    int32_t ret = service_->Drain(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
}

/**
 * @brief HDI SendBrkSignal 返回 HDF_FAILURE → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_SendBrk_HdfFailure_ReturnsService, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, SendBrkSignal()).WillOnce(Return(HDF_FAILURE));

    int32_t ret = service_->SendBrkSignal(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
}

// ============================================================================
// 4. 流控/调制解调器信号 HDI 错误注入
// ============================================================================

/**
 * @brief HDI SetRtsSignal 返回 HDF_ERR_NOPERM → 映射为权限拒绝
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_SetRts_HdfNoPerm_ReturnsPermissionDenied, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, SetRtsSignal(true)).WillOnce(Return(HDF_ERR_NOPERM));

    int32_t ret = service_->SetRtsSignal(PORT_USB0, true);
    EXPECT_EQ(ret, SERIAL_ERR_PERMISSION_DENIED);
}

/**
 * @brief HDI GetCtsSignal 返回 HDF_FAILURE → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_GetCts_HdfFailure_ReturnsService, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    bool cts = false;
    EXPECT_CALL(*mockDev, GetCtsSignal(_)).WillOnce(Return(HDF_FAILURE));

    int32_t ret = service_->GetCtsSignal(PORT_USB0, cts);
    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
}

/**
 * @brief HDI SetDtrSignal 返回 HDF_FAILURE → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_SetDtr_HdfFailure_ReturnsService, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, SetDtrSignal(false)).WillOnce(Return(HDF_FAILURE));

    int32_t ret = service_->SetDtrSignal(PORT_USB0, false);
    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
}

/**
 * @brief HDI GetDsrSignal 返回 HDF_ERR_IO → 映射为设备断开
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_GetDsr_HdfIo_ReturnsDisconnected, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    bool dsr = false;
    EXPECT_CALL(*mockDev, GetDsrSignal(_)).WillOnce(Return(HDF_ERR_IO));

    int32_t ret = service_->GetDsrSignal(PORT_USB0, dsr);
    EXPECT_EQ(ret, SERIAL_ERR_DEVICE_DISCONNECTED);
}

// ============================================================================
// 5. 设备枚举与关闭路径错误注入
// ============================================================================

/**
 * @brief HDI QueryDevices 失败 → 映射为服务错误
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_QueryDevices_HdfFailure_ReturnsService, TestSize.Level1)
{
    mockHdi_->SetQueryDevicesResult(false);

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_ERR_SERVICE);
    EXPECT_EQ(devices.size(), 0u);
}

/**
 * @brief HDI Close 失败仅告警，上下文仍被清理且返回成功
 */
HWTEST_F(SerialMockHdiErrcodeTest, ErrInject_Close_HdfFailure_StillCleansContext, TestSize.Level1)
{
    sptr<MockSerialDevice> mockDev = InsertOpenedPort(PORT_USB0);
    ASSERT_NE(mockDev, nullptr);

    EXPECT_CALL(*mockDev, Close()).WillOnce(Return(HDF_FAILURE));

    // HDI Close 失败仅告警，关闭仍返回成功
    int32_t ret = service_->CloseSerial(PORT_USB0);
    EXPECT_EQ(ret, SERIAL_OK);

    // 以公共 API 行为验证上下文已清理：写入返回设备未打开
    std::vector<uint8_t> data = {LOOPBACK_BYTE_0};
    int32_t bytesWritten = 0;
    int32_t writeRet = service_->WriteSerial(PORT_USB0, data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(writeRet, SERIAL_ERR_DEVICE_NOT_OPENED);
}

} // namespace Serial
} // namespace OHOS
