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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtest/hwext/gtest-ext.h>
#include <gtest/hwext/gtest-tag.h>

#include "if_system_ability_manager_mock.h"
#include "iserial_server.h"

using testing::ext::TestSize;
#include "iserial_service_callback.h"
#include "iservice_registry.h"
#include "mock_serial_server.h"
#include "serial_error_code.h"
#include "serial_server_types.h"
#include "serial_service_callback.h"
#include "serial_srv_client.h"

namespace OHOS {
namespace Serial {

constexpr int32_t UNKNOWN_ERROR_CODE = -9999;
constexpr size_t MAX_WRITE_DATA_SIZE = 4096;
constexpr int32_t MAX_WRITE_TIMEOUT_MS = 300000;
constexpr int32_t INVALID_DATA_BITS_VALUE = 0;
constexpr int32_t INVALID_STOP_BITS_VALUE = 3;
constexpr int32_t INVALID_PARITY_VALUE = 5;
constexpr int32_t DEFAULT_BAUD_RATE = 115200;
constexpr int32_t DEFAULT_DATA_BITS = 8;
constexpr uint8_t TEST_DATA_BYTE = 0x01;
constexpr int32_t TEST_WRITE_BYTE_COUNT = 5;
constexpr int32_t TEST_WRITE_TIMEOUT_MS = 1000;
constexpr int32_t INVALID_TIMEOUT_VALUE = -1;
constexpr ListenerHandle NONEXISTENT_HANDLE_FIRST = 999;
constexpr ListenerHandle NONEXISTENT_HANDLE_SECOND = 12345;

class SerialSrvClientTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();

protected:
    sptr<MockSerialServer> mockProxy_ = nullptr;
    SerialConfig MakeDefaultConfig() const;
    SerialPortInfo MakeDefaultPortInfo() const;

    static inline sptr<ISystemAbilityManagerMock> samgrMock_ = nullptr;
    static inline sptr<ISystemAbilityManager> originalSamgr_ = nullptr;
};

void SerialSrvClientTest::SetUpTestCase(void)
{
    originalSamgr_ = SystemAbilityManagerClient::GetInstance().systemAbilityManager_;
    samgrMock_ = new (std::nothrow) ISystemAbilityManagerMock();
    ASSERT_NE(samgrMock_, nullptr);
    SystemAbilityManagerClient::GetInstance().systemAbilityManager_ =
        sptr<ISystemAbilityManager>(samgrMock_.GetRefPtr());
}

void SerialSrvClientTest::TearDownTestCase(void)
{
    SystemAbilityManagerClient::GetInstance().systemAbilityManager_ = originalSamgr_;
    samgrMock_ = nullptr;
    originalSamgr_ = nullptr;
}

void SerialSrvClientTest::SetUp()
{
    mockProxy_ = new (std::nothrow) MockSerialServer();
    ASSERT_NE(mockProxy_, nullptr);

    auto& client = SerialSrvClient::GetInstance();
    client.proxy_ = iface_cast<ISerialServer>(mockProxy_->AsObject());
    ASSERT_NE(client.proxy_, nullptr);
}

void SerialSrvClientTest::TearDown()
{
    auto& client = SerialSrvClient::GetInstance();
    if (client.callback_ != nullptr) {
        client.callback_->RemoveAllListeners("/dev/ttyS0");
        client.callback_->RemoveAllListeners("/dev/ttyS1");
        client.callback_->RemoveAllListeners("/dev/ttyUSB0");
    }
    client.proxy_ = nullptr;
    mockProxy_ = nullptr;
}

SerialConfig SerialSrvClientTest::MakeDefaultConfig() const
{
    SerialConfig config;
    config.baudRate = DEFAULT_BAUD_RATE;
    config.dataBits = DEFAULT_DATA_BITS;
    config.stopBits = 1;
    config.parity = 0;
    config.rtscts = false;
    config.xon = false;
    config.xoff = false;
    config.xany = false;
    return config;
}

SerialPortInfo SerialSrvClientTest::MakeDefaultPortInfo() const
{
    SerialPortInfo info;
    info.portName = "/dev/ttyS0";
    info.productId = 1;
    info.vendorId = 1;
    info.manufacturer = "TestMfg";
    return info;
}

HWTEST_F(SerialSrvClientTest, GetInstance_001, TestSize.Level1)
{
    auto& instance = SerialSrvClient::GetInstance();
    EXPECT_NE(&instance, nullptr);
}

HWTEST_F(SerialSrvClientTest, GetInstance_002, TestSize.Level1)
{
    auto& inst1 = SerialSrvClient::GetInstance();
    auto& inst2 = SerialSrvClient::GetInstance();
    EXPECT_EQ(&inst1, &inst2);
}

HWTEST_F(SerialSrvClientTest, QuerySerialDevices_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, QuerySerialDevices(testing::_))
        .WillOnce(testing::DoAll(testing::SetArgReferee<0>(std::vector<SerialPortInfo> {MakeDefaultPortInfo()}),
            testing::Return(SERIAL_OK)));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_EQ(devices.size(), static_cast<size_t>(1));
    EXPECT_EQ(devices[0].portName, "/dev/ttyS0");
}

HWTEST_F(SerialSrvClientTest, QuerySerialDevices_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, QuerySerialDevices(testing::_)).WillOnce(testing::Return(SERIAL_ERR_ENTERPRISE_POLICY));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    EXPECT_EQ(ret, SERIAL_JS_ERR_ENTERPRISE_POLICY);
}

HWTEST_F(SerialSrvClientTest, QuerySerialDevices_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, QuerySerialDevices(testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, QuerySerialDevices_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, QuerySerialDevices(testing::_)).WillOnce(testing::Return(UNKNOWN_ERROR_CODE));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_INVALID_PARAM));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_DISCONNECTED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_005, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_006, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_PERMISSION_DENIED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_PERMISSION_DENIED);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_007, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    SerialConfig config = MakeDefaultConfig();
    config.baudRate = 0;
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), config);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_008, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    SerialConfig config = MakeDefaultConfig();
    config.dataBits = INVALID_DATA_BITS_VALUE;
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), config);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_009, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    SerialConfig config = MakeDefaultConfig();
    config.stopBits = INVALID_STOP_BITS_VALUE;
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), config);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_010, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    SerialConfig config = MakeDefaultConfig();
    config.parity = INVALID_PARITY_VALUE;
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), config);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_011, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    SerialPortInfo info;
    info.portName = "";
    info.productId = 0;
    info.vendorId = 0;
    info.manufacturer = "";
    int32_t ret = client.OpenSerial(info, MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_012, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_TIMEOUT));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_TIMEOUT);
}

HWTEST_F(SerialSrvClientTest, OpenSerial_RemovesListeners_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t regRet = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    ASSERT_EQ(regRet, SERIAL_OK);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));
    ASSERT_NE(client.callback_, nullptr);
    EXPECT_TRUE(client.callback_->HasListeners("/dev/ttyS0"));

    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_FALSE(client.callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialSrvClientTest, OpenSerial_NotRemovesListenersOnFailure_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t regRet = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    ASSERT_EQ(regRet, SERIAL_OK);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));
    ASSERT_NE(client.callback_, nullptr);
    EXPECT_TRUE(client.callback_->HasListeners("/dev/ttyS0"));

    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
    EXPECT_TRUE(client.callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialSrvClientTest, CloseSerial_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, CloseSerial(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.CloseSerial("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, CloseSerial_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, CloseSerial(testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.CloseSerial("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, CloseSerial_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, CloseSerial(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_NOT_OPENED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.CloseSerial("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialSrvClientTest, CloseSerial_004, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.CloseSerial("");
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, CloseSerial_005, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, CloseSerial(testing::_)).WillOnce(testing::Return(UNKNOWN_ERROR_CODE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.CloseSerial("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, WriteSerial(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::SetArgReferee<3>(TEST_WRITE_BYTE_COUNT), testing::Return(SERIAL_OK)));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data(TEST_WRITE_BYTE_COUNT, TEST_DATA_BYTE);
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_EQ(bytesWritten, TEST_WRITE_BYTE_COUNT);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, WriteSerial(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, WriteSerial(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_INVALID_PARAM));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, WriteSerial(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_DISCONNECTED));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_005, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, WriteSerial(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_NOT_OPENED));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_006, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, WriteSerial(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_TIMEOUT));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_TIMEOUT);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_007, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_008, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data;
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_009, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data(MAX_WRITE_DATA_SIZE + 1, TEST_DATA_BYTE);
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_010, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, INVALID_TIMEOUT_VALUE, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_011, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, MAX_WRITE_TIMEOUT_MS + 1, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, WriteSerial_012, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, WriteSerial(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, Flush_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Flush(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Flush("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, Flush_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Flush(testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Flush("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, Flush_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Flush(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_DISCONNECTED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Flush("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
}

HWTEST_F(SerialSrvClientTest, Flush_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Flush(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_NOT_OPENED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Flush("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialSrvClientTest, Flush_005, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Flush("");
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, Flush_006, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Flush(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Flush("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, Drain_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Drain(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Drain("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, Drain_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Drain(testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Drain("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, Drain_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Drain(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_DISCONNECTED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Drain("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
}

HWTEST_F(SerialSrvClientTest, Drain_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Drain(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_NOT_OPENED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Drain("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialSrvClientTest, Drain_005, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Drain("");
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, Drain_006, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, Drain(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Drain("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, SendBrkSignal_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SendBrkSignal(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SendBrkSignal("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, SendBrkSignal_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SendBrkSignal(testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SendBrkSignal("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, SendBrkSignal_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SendBrkSignal(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_DISCONNECTED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SendBrkSignal("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
}

HWTEST_F(SerialSrvClientTest, SendBrkSignal_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SendBrkSignal(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_NOT_OPENED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SendBrkSignal("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialSrvClientTest, SendBrkSignal_005, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SendBrkSignal("");
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, SendBrkSignal_006, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SendBrkSignal(testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SendBrkSignal("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, SetRtsSignal_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SetRtsSignal(testing::_, testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("/dev/ttyS0", true);
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, SetRtsSignal_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SetRtsSignal(testing::_, testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("/dev/ttyS0", true);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, SetRtsSignal_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SetRtsSignal(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_DISCONNECTED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("/dev/ttyS0", true);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
}

HWTEST_F(SerialSrvClientTest, SetRtsSignal_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SetRtsSignal(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_NOT_OPENED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("/dev/ttyS0", true);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialSrvClientTest, SetRtsSignal_005, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("", true);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, SetRtsSignal_006, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, SetRtsSignal(testing::_, testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("/dev/ttyS0", true);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, GetCtsSignal_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, GetCtsSignal(testing::_, testing::_))
        .WillOnce(testing::DoAll(testing::SetArgReferee<1>(true), testing::Return(SERIAL_OK)));

    auto& client = SerialSrvClient::GetInstance();
    bool cts = false;
    int32_t ret = client.GetCtsSignal("/dev/ttyS0", cts);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_TRUE(cts);
}

HWTEST_F(SerialSrvClientTest, GetCtsSignal_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, GetCtsSignal(testing::_, testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    bool cts = false;
    int32_t ret = client.GetCtsSignal("/dev/ttyS0", cts);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, GetCtsSignal_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, GetCtsSignal(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_DISCONNECTED));

    auto& client = SerialSrvClient::GetInstance();
    bool cts = false;
    int32_t ret = client.GetCtsSignal("/dev/ttyS0", cts);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
}

HWTEST_F(SerialSrvClientTest, GetCtsSignal_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, GetCtsSignal(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_NOT_OPENED));

    auto& client = SerialSrvClient::GetInstance();
    bool cts = false;
    int32_t ret = client.GetCtsSignal("/dev/ttyS0", cts);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_NOT_OPENED);
}

HWTEST_F(SerialSrvClientTest, GetCtsSignal_005, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    bool cts = false;
    int32_t ret = client.GetCtsSignal("", cts);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, GetCtsSignal_006, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, GetCtsSignal(testing::_, testing::_)).WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    bool cts = false;
    int32_t ret = client.GetCtsSignal("/dev/ttyS0", cts);
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, AddPortAuthorization(testing::_, testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "device456");
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_002, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, AddPortAuthorization(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_NOT_SYSTEM_APP));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "device456");
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SYSTEM_APP);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, AddPortAuthorization(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "device456");
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, AddPortAuthorization(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_INVALID_PARAM));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "device456");
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_005, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, AddPortAuthorization(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_NOT_ALLOWED));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "device456");
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_ALLOWED);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_006, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("", "device456");
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_007, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "");
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
}

HWTEST_F(SerialSrvClientTest, AddPortAuthorization_008, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, AddPortAuthorization(testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "device456");
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, RegisterDataListener_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    bool callbackCalled = false;
    auto callback = [&callbackCalled](const std::vector<uint8_t>& data) {
        callbackCalled = true;
    };

    ListenerHandle handle = 0;
    int32_t ret = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_NE(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialSrvClientTest, RegisterDataListener_002, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    DataReceivedCallback nullCallback;
    ListenerHandle handle = 0;
    int32_t ret = client.RegisterDataListener("/dev/ttyS0", nullCallback, handle);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
    EXPECT_EQ(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialSrvClientTest, RegisterDataListener_003, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t ret = client.RegisterDataListener("/dev/ttyS1", callback, handle);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
    EXPECT_EQ(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialSrvClientTest, RegisterDataListener_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};

    ListenerHandle handle1 = 0;
    int32_t ret1 = client.RegisterDataListener("/dev/ttyUSB0", callback, handle1);
    EXPECT_EQ(ret1, SERIAL_OK);
    EXPECT_NE(handle1, static_cast<ListenerHandle>(0));

    EXPECT_CALL(*mockProxy_, StopRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));
    int32_t ret = client.UnregisterDataListener("/dev/ttyUSB0", handle1);
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, RegisterDataListener_005, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};

    ListenerHandle handle1 = 0;
    int32_t ret1 = client.RegisterDataListener("/dev/ttyS0", callback, handle1);
    EXPECT_EQ(ret1, SERIAL_OK);
    EXPECT_NE(handle1, static_cast<ListenerHandle>(0));

    ListenerHandle handle2 = 0;
    int32_t ret2 = client.RegisterDataListener("/dev/ttyS0", callback, handle2);
    EXPECT_EQ(ret2, SERIAL_OK);
    EXPECT_NE(handle2, static_cast<ListenerHandle>(0));
    EXPECT_NE(handle1, handle2);
}

HWTEST_F(SerialSrvClientTest, UnregisterDataListener_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t regRet = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    ASSERT_EQ(regRet, SERIAL_OK);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));

    EXPECT_CALL(*mockProxy_, StopRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));
    int32_t ret = client.UnregisterDataListener("/dev/ttyS0", handle);
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, UnregisterDataListener_002, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.UnregisterDataListener("/dev/ttyS0", NONEXISTENT_HANDLE_FIRST);
    EXPECT_EQ(ret, 0);
}

HWTEST_F(SerialSrvClientTest, UnregisterDataListener_003, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.UnregisterDataListener("/dev/ttyS0", NONEXISTENT_HANDLE_SECOND);
    EXPECT_EQ(ret, 0);
}

HWTEST_F(SerialSrvClientTest, UnregisterDataListener_004, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t regRet = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    ASSERT_EQ(regRet, SERIAL_OK);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));

    EXPECT_CALL(*mockProxy_, StopRead(testing::_)).WillOnce(testing::Return(SERIAL_ERR_SERVICE));
    int32_t ret = client.UnregisterDataListener("/dev/ttyS0", handle);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, UnregisterDataListener_005, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};

    ListenerHandle handle1 = 0;
    ListenerHandle handle2 = 0;
    int32_t regRet1 = client.RegisterDataListener("/dev/ttyS0", callback, handle1);
    int32_t regRet2 = client.RegisterDataListener("/dev/ttyS0", callback, handle2);
    ASSERT_EQ(regRet1, SERIAL_OK);
    ASSERT_EQ(regRet2, SERIAL_OK);
    ASSERT_NE(handle1, static_cast<ListenerHandle>(0));
    ASSERT_NE(handle2, static_cast<ListenerHandle>(0));

    int32_t ret = client.UnregisterDataListener("/dev/ttyS0", handle1);
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientTest, ResetProxy_001, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    ASSERT_NE(client.proxy_, nullptr);

    sptr<IRemoteObject> remote = client.proxy_->AsObject();
    ASSERT_NE(remote, nullptr);

    client.deathRecipient_->OnRemoteDied(remote);
    EXPECT_EQ(client.proxy_, nullptr);
}

HWTEST_F(SerialSrvClientTest, ResetProxy_002, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    ASSERT_NE(client.proxy_, nullptr);

    sptr<MockSerialServer> otherMock = new (std::nothrow) MockSerialServer();
    ASSERT_NE(otherMock, nullptr);
    sptr<IRemoteObject> wrongRemote = otherMock->AsObject();

    client.deathRecipient_->OnRemoteDied(wrongRemote);
    EXPECT_NE(client.proxy_, nullptr);
}

HWTEST_F(SerialSrvClientTest, DeathReconnect_QuerySerialDevices_001, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    sptr<IRemoteObject> remote = client.proxy_->AsObject();
    client.deathRecipient_->OnRemoteDied(remote);
    ASSERT_EQ(client.proxy_, nullptr);

    EXPECT_CALL(*samgrMock_, LoadSystemAbility(testing::An<int32_t>(), testing::An<int32_t>()))
        .WillOnce(testing::Return(mockProxy_->AsObject()));

    EXPECT_CALL(*mockProxy_, QuerySerialDevices(testing::_))
        .WillOnce(testing::DoAll(testing::SetArgReferee<0>(std::vector<SerialPortInfo> {MakeDefaultPortInfo()}),
            testing::Return(SERIAL_OK)));

    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_EQ(devices.size(), static_cast<size_t>(1));
}

HWTEST_F(SerialSrvClientTest, DeathReconnect_QuerySerialDevices_002, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    sptr<IRemoteObject> remote = client.proxy_->AsObject();
    client.deathRecipient_->OnRemoteDied(remote);
    ASSERT_EQ(client.proxy_, nullptr);

    EXPECT_CALL(*samgrMock_, LoadSystemAbility(testing::An<int32_t>(), testing::An<int32_t>()))
        .WillOnce(testing::Return(nullptr));

    std::vector<SerialPortInfo> devices;
    int32_t ret = client.QuerySerialDevices(devices);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, DeathReconnect_OpenSerial_001, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();

    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t regRet = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    ASSERT_EQ(regRet, SERIAL_OK);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));
    ASSERT_NE(client.callback_, nullptr);
    EXPECT_TRUE(client.callback_->HasListeners("/dev/ttyS0"));

    sptr<IRemoteObject> remote = client.proxy_->AsObject();
    client.deathRecipient_->OnRemoteDied(remote);

    EXPECT_CALL(*samgrMock_, LoadSystemAbility(testing::An<int32_t>(), testing::An<int32_t>()))
        .WillOnce(testing::Return(mockProxy_->AsObject()));
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_)).WillOnce(testing::Return(SERIAL_OK));

    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_FALSE(client.callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialSrvClientTest, DeathReconnect_OpenSerial_002, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    sptr<IRemoteObject> remote = client.proxy_->AsObject();
    client.deathRecipient_->OnRemoteDied(remote);
    ASSERT_EQ(client.proxy_, nullptr);

    EXPECT_CALL(*samgrMock_, LoadSystemAbility(testing::An<int32_t>(), testing::An<int32_t>()))
        .WillOnce(testing::Return(nullptr));

    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
}

HWTEST_F(SerialSrvClientTest, DeathReconnect_OpenSerial_003, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    sptr<IRemoteObject> remote = client.proxy_->AsObject();
    client.deathRecipient_->OnRemoteDied(remote);
    ASSERT_EQ(client.proxy_, nullptr);

    EXPECT_CALL(*samgrMock_, LoadSystemAbility(testing::An<int32_t>(), testing::An<int32_t>()))
        .WillOnce(testing::Return(mockProxy_->AsObject()));
    EXPECT_CALL(*mockProxy_, OpenSerial(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(SERIAL_ERR_DEVICE_BUSY));

    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_DEVICE_BUSY);
}

HWTEST_F(SerialSrvClientTest, ConnectFail_001, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    sptr<ISerialServer> savedProxy = client.proxy_;
    client.proxy_ = nullptr;

    EXPECT_CALL(*samgrMock_, LoadSystemAbility(testing::An<int32_t>(), testing::An<int32_t>()))
        .WillRepeatedly(testing::Return(nullptr));

    std::vector<SerialPortInfo> devices;
    EXPECT_EQ(client.QuerySerialDevices(devices), SERIAL_JS_ERR_SERVICE);

    EXPECT_EQ(client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig()), SERIAL_JS_ERR_SERVICE);

    EXPECT_EQ(client.CloseSerial("/dev/ttyS0"), SERIAL_JS_ERR_SERVICE);

    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    EXPECT_EQ(client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten), SERIAL_JS_ERR_SERVICE);

    EXPECT_EQ(client.Flush("/dev/ttyS0"), SERIAL_JS_ERR_SERVICE);

    EXPECT_EQ(client.Drain("/dev/ttyS0"), SERIAL_JS_ERR_SERVICE);

    EXPECT_EQ(client.SendBrkSignal("/dev/ttyS0"), SERIAL_JS_ERR_SERVICE);

    EXPECT_EQ(client.SetRtsSignal("/dev/ttyS0", true), SERIAL_JS_ERR_SERVICE);

    bool cts = false;
    EXPECT_EQ(client.GetCtsSignal("/dev/ttyS0", cts), SERIAL_JS_ERR_SERVICE);

    EXPECT_EQ(client.AddPortAuthorization("t", "d"), SERIAL_JS_ERR_SERVICE);

    client.proxy_ = savedProxy;
}

HWTEST_F(SerialSrvClientTest, CallbackNull_001, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    sptr<SerialServiceCallback> savedCallback = client.callback_;
    client.callback_ = nullptr;

    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t ret = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    EXPECT_EQ(ret, SERIAL_JS_ERR_SERVICE);
    EXPECT_EQ(handle, static_cast<ListenerHandle>(0));

    client.callback_ = savedCallback;
}

HWTEST_F(SerialSrvClientTest, CloseSerial_RemovesListeners_001, TestSize.Level1)
{
    EXPECT_CALL(*mockProxy_, StartRead(testing::_)).WillOnce(testing::Return(SERIAL_OK));
    EXPECT_CALL(*mockProxy_, CloseSerial(testing::_)).WillOnce(testing::Return(SERIAL_OK));

    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t regRet = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    ASSERT_EQ(regRet, SERIAL_OK);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));
    ASSERT_NE(client.callback_, nullptr);
    EXPECT_TRUE(client.callback_->HasListeners("/dev/ttyS0"));

    int32_t ret = client.CloseSerial("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_FALSE(client.callback_->HasListeners("/dev/ttyS0"));
}

} // namespace Serial
} // namespace OHOS
