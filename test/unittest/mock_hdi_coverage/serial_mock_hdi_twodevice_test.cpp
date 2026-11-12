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
 * @file serial_mock_hdi_twodevice_test.cpp
 * @brief Mock HDI 双设备场景测试（对应覆盖表 develop/twodevice 模块）
 * @details 覆盖双端口并发/隔离场景（全部基于预注入上下文，确定性断言，无需真实设备）：
 *          - 双端口独立写入，各自 Mock 设备收到各自数据
 *          - 端口 0 回环读数据不触发端口 1 回调
 *          - 关闭端口 0 后端口 1 仍可正常写入
 *          - 端口 0 客户端死亡清理后端口 1 上下文存活
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
constexpr int32_t MOCK_VID_PROLIFIC = 2222;
constexpr int32_t MOCK_PID_PROLIFIC = 1111;
constexpr uint8_t PORT0_BYTE_0 = 0xA0;
constexpr uint8_t PORT0_BYTE_1 = 0xA1;
constexpr uint8_t PORT1_BYTE_0 = 0xB0;
constexpr uint8_t PORT1_BYTE_1 = 0xB1;
constexpr int32_t MOCK_CALLBACK_ERR_OK = 0;
constexpr int32_t WRITE_RETURN_TWO_BYTES = 2;
constexpr int32_t WRITE_RETURN_ONE_BYTE = 1;
constexpr const char *PORT_USB0 = "ttyUSB0";
constexpr const char *PORT_USB1 = "ttyUSB1";
constexpr const char *MFR_FTDI = "FTDI";
constexpr const char *MFR_PROLIFIC = "Prolific";
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
 * @class SerialMockHdiTwoDeviceTest
 * @brief 双设备场景测试夹具
 */
class SerialMockHdiTwoDeviceTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiTwoDeviceTest::SetUpTestCase");
    }

    static void TearDownTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiTwoDeviceTest::TearDownTestCase");
    }

    void SetUp() override;
    void TearDown() override;

protected:
    sptr<MockSerials> mockHdi_ = nullptr;
    sptr<SerialService> service_ = nullptr;
    sptr<MockSerialServiceCallback> callback0_ = nullptr;
    sptr<MockSerialServiceCallback> callback1_ = nullptr;

    SerialConfig CreateDefaultConfig() const;
    void InsertBothPorts(sptr<MockSerialDevice> &dev0, sptr<MockSerialDevice> &dev1);
};

void SerialMockHdiTwoDeviceTest::SetUp()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiTwoDeviceTest::SetUp");
    mockHdi_ = new MockSerials();
    ASSERT_NE(mockHdi_, nullptr);

    std::vector<SerialDeviceInfo> mockDevices;
    SerialDeviceInfo usb0;
    usb0.portName = PORT_USB0;
    usb0.productId = MOCK_PID_FTDI;
    usb0.vendorId = MOCK_VID_FTDI;
    usb0.manufacturer = MFR_FTDI;
    SerialDeviceInfo usb1;
    usb1.portName = PORT_USB1;
    usb1.productId = MOCK_PID_PROLIFIC;
    usb1.vendorId = MOCK_VID_PROLIFIC;
    usb1.manufacturer = MFR_PROLIFIC;
    mockDevices.push_back(usb0);
    mockDevices.push_back(usb1);
    mockHdi_->SetMockDevices(mockDevices);

    service_ = new SerialService(SERIAL_SYSTEM_ABILITY_ID, false);
    ASSERT_NE(service_, nullptr);
    service_->serialsProxy_ = mockHdi_;

    callback0_ = new MockSerialServiceCallback();
    callback1_ = new MockSerialServiceCallback();
    ASSERT_NE(callback0_, nullptr);
    ASSERT_NE(callback1_, nullptr);
}

void SerialMockHdiTwoDeviceTest::TearDown()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiTwoDeviceTest::TearDown");
    CleanupTestService(service_);
    callback0_ = nullptr;
    callback1_ = nullptr;
    mockHdi_ = nullptr;
    service_ = nullptr;
}

SerialConfig SerialMockHdiTwoDeviceTest::CreateDefaultConfig() const
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

void SerialMockHdiTwoDeviceTest::InsertBothPorts(sptr<MockSerialDevice> &dev0, sptr<MockSerialDevice> &dev1)
{
    dev0 = InsertTestDeviceContext(service_, PORT_USB0, callback0_, CreateDefaultConfig());
    dev1 = InsertTestDeviceContext(service_, PORT_USB1, callback1_, CreateDefaultConfig());
    ASSERT_NE(dev0, nullptr);
    ASSERT_NE(dev1, nullptr);
    ASSERT_EQ(service_->GetDeviceCount(), 2u);
}

/**
 * @brief 双端口独立写入：各端口数据下发到各自的 Mock 设备
 */
HWTEST_F(SerialMockHdiTwoDeviceTest, TwoDevices_WriteBoth_EachDeviceReceivesOwnData, TestSize.Level1)
{
    sptr<MockSerialDevice> dev0 = nullptr;
    sptr<MockSerialDevice> dev1 = nullptr;
    InsertBothPorts(dev0, dev1);

    std::vector<uint8_t> data0 = {PORT0_BYTE_0, PORT0_BYTE_1};
    std::vector<uint8_t> data1 = {PORT1_BYTE_0, PORT1_BYTE_1};
    int32_t bytesWritten0 = 0;
    int32_t bytesWritten1 = 0;
    EXPECT_CALL(*dev0, Write(data0, TEST_WRITE_TIMEOUT_MS, _))
        .WillOnce(DoAll(SetArgReferee<2>(WRITE_RETURN_TWO_BYTES), Return(HDF_SUCCESS)));
    EXPECT_CALL(*dev1, Write(data1, TEST_WRITE_TIMEOUT_MS, _))
        .WillOnce(DoAll(SetArgReferee<2>(WRITE_RETURN_TWO_BYTES), Return(HDF_SUCCESS)));

    int32_t ret0 = service_->WriteSerial(PORT_USB0, data0, TEST_WRITE_TIMEOUT_MS, bytesWritten0);
    int32_t ret1 = service_->WriteSerial(PORT_USB1, data1, TEST_WRITE_TIMEOUT_MS, bytesWritten1);
    EXPECT_EQ(ret0, SERIAL_OK);
    EXPECT_EQ(ret1, SERIAL_OK);
    EXPECT_EQ(bytesWritten0, WRITE_RETURN_TWO_BYTES);
    EXPECT_EQ(bytesWritten1, WRITE_RETURN_TWO_BYTES);
}

/**
 * @brief 双端口回环读数据隔离：端口 0 的数据不触发端口 1 回调
 */
HWTEST_F(SerialMockHdiTwoDeviceTest, TwoDevices_ReadEcho_IndependentCallbacks, TestSize.Level1)
{
    sptr<MockSerialDevice> dev0 = nullptr;
    sptr<MockSerialDevice> dev1 = nullptr;
    InsertBothPorts(dev0, dev1);

    auto it = service_->deviceContexts_.find(PORT_USB0);
    ASSERT_TRUE(it != service_->deviceContexts_.end());
    sptr<SerialHdiCallback> hdiCb0 = it->second->hdiCallback;
    ASSERT_NE(hdiCb0, nullptr);

    std::vector<uint8_t> port0Data = {PORT0_BYTE_0, PORT0_BYTE_1};
    std::vector<int8_t> echoData(port0Data.begin(), port0Data.end());
    EXPECT_CALL(*callback0_, onDataRead(PORT_USB0, port0Data)).WillOnce(Return(MOCK_CALLBACK_ERR_OK));
    EXPECT_CALL(*callback1_, onDataRead(_, _)).Times(0);

    int32_t echoRet = hdiCb0->OnReadData(echoData, static_cast<uint32_t>(echoData.size()));
    EXPECT_EQ(echoRet, SERIAL_OK);
}

/**
 * @brief 关闭端口 0 后端口 1 仍可正常写入
 */
HWTEST_F(SerialMockHdiTwoDeviceTest, TwoDevices_CloseOne_OtherStillOperable, TestSize.Level1)
{
    sptr<MockSerialDevice> dev0 = nullptr;
    sptr<MockSerialDevice> dev1 = nullptr;
    InsertBothPorts(dev0, dev1);

    int32_t closeRet = service_->CloseSerial(PORT_USB0);
    EXPECT_EQ(closeRet, SERIAL_OK);
    ASSERT_EQ(service_->GetDeviceCount(), 1u);

    std::vector<uint8_t> data1 = {PORT1_BYTE_0};
    int32_t bytesWritten1 = 0;
    EXPECT_CALL(*dev1, Write(data1, TEST_WRITE_TIMEOUT_MS, _))
        .WillOnce(DoAll(SetArgReferee<2>(WRITE_RETURN_ONE_BYTE), Return(HDF_SUCCESS)));

    int32_t ret1 = service_->WriteSerial(PORT_USB1, data1, TEST_WRITE_TIMEOUT_MS, bytesWritten1);
    EXPECT_EQ(ret1, SERIAL_OK);
}

/**
 * @brief 端口 0 客户端死亡清理后，端口 1 上下文存活且可操作
 */
HWTEST_F(SerialMockHdiTwoDeviceTest, TwoDevices_ClientDiedOne_OtherSurvives, TestSize.Level1)
{
    sptr<MockSerialDevice> dev0 = nullptr;
    sptr<MockSerialDevice> dev1 = nullptr;
    InsertBothPorts(dev0, dev1);

    EXPECT_CALL(*dev0, Close()).WillOnce(Return(HDF_SUCCESS));
    service_->OnClientDied(PORT_USB0);
    ASSERT_EQ(service_->GetDeviceCount(), 1u);

    std::vector<uint8_t> data1 = {PORT1_BYTE_0};
    int32_t bytesWritten1 = 0;
    EXPECT_CALL(*dev1, Write(data1, TEST_WRITE_TIMEOUT_MS, _))
        .WillOnce(DoAll(SetArgReferee<2>(WRITE_RETURN_ONE_BYTE), Return(HDF_SUCCESS)));

    int32_t ret1 = service_->WriteSerial(PORT_USB1, data1, TEST_WRITE_TIMEOUT_MS, bytesWritten1);
    EXPECT_EQ(ret1, SERIAL_OK);
}

} // namespace Serial
} // namespace OHOS
