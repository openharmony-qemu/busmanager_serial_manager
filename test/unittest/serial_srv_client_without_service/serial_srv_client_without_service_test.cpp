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

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtest/hwext/gtest-ext.h>
#include <gtest/hwext/gtest-tag.h>

#include "serial_error_code.h"
#include "serial_srv_client.h"

using testing::ext::TestSize;

namespace OHOS {
namespace Serial {
namespace {
constexpr int32_t SERIAL_JS_ERR_NOT_SUPPORTED = 801;
constexpr int32_t DEFAULT_BAUD_RATE = 115200;
constexpr int32_t DEFAULT_DATA_BITS = 8;
constexpr uint8_t TEST_DATA_BYTE = 0x01;
constexpr int32_t TEST_WRITE_BYTE_COUNT = 3;
constexpr int32_t TEST_WRITE_TIMEOUT_MS = 1000;
constexpr int32_t INITIAL_BYTES_WRITTEN = -1;
constexpr ListenerHandle NONEXISTENT_HANDLE = 999;
} // namespace

class SerialSrvClientWithoutServiceTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();

protected:
    SerialPortInfo MakeDefaultPortInfo() const;
    SerialConfig MakeDefaultConfig() const;
};

void SerialSrvClientWithoutServiceTest::SetUpTestCase(void) {}

void SerialSrvClientWithoutServiceTest::TearDownTestCase(void) {}

void SerialSrvClientWithoutServiceTest::SetUp() {}

void SerialSrvClientWithoutServiceTest::TearDown() {}

SerialPortInfo SerialSrvClientWithoutServiceTest::MakeDefaultPortInfo() const
{
    SerialPortInfo info;
    info.portName = "/dev/ttyS0";
    info.productId = 1;
    info.vendorId = 1;
    info.manufacturer = "TestMfg";
    return info;
}

SerialConfig SerialSrvClientWithoutServiceTest::MakeDefaultConfig() const
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

HWTEST_F(SerialSrvClientWithoutServiceTest, GetInstance_NotNull, TestSize.Level1)
{
    auto& instance = SerialSrvClient::GetInstance();
    EXPECT_NE(&instance, nullptr);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, GetInstance_Singleton, TestSize.Level1)
{
    auto& inst1 = SerialSrvClient::GetInstance();
    auto& inst2 = SerialSrvClient::GetInstance();
    EXPECT_EQ(&inst1, &inst2);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, QuerySerialDevices_ReturnsSuccessWithEmptyList, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    std::vector<SerialPortInfo> devices;
    SerialPortInfo dummy;
    dummy.portName = "/dev/ttyDummy";
    devices.push_back(dummy);
    int32_t ret = client.QuerySerialDevices(devices);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_TRUE(devices.empty());
}

HWTEST_F(SerialSrvClientWithoutServiceTest, OpenSerial_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig());
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, CloseSerial_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.CloseSerial("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, CloseSerial_EmptyPort_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.CloseSerial("");
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, WriteSerial_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    std::vector<uint8_t> data(TEST_WRITE_BYTE_COUNT, TEST_DATA_BYTE);
    int32_t bytesWritten = INITIAL_BYTES_WRITTEN;
    int32_t ret = client.WriteSerial("/dev/ttyS0", data, TEST_WRITE_TIMEOUT_MS, bytesWritten);
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
    EXPECT_EQ(bytesWritten, 0);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, Flush_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Flush("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, Drain_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.Drain("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, SendBrkSignal_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SendBrkSignal("/dev/ttyS0");
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, SetRtsSignal_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("/dev/ttyS0", true);
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, SetRtsSignal_False_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.SetRtsSignal("/dev/ttyS0", false);
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, GetCtsSignal_ReturnsNotSupported, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    bool cts = true;
    int32_t ret = client.GetCtsSignal("/dev/ttyS0", cts);
    EXPECT_EQ(ret, SERIAL_JS_ERR_NOT_SUPPORTED);
    EXPECT_FALSE(cts);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, AddPortAuthorization_ReturnsSuccess, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("token123", "device456");
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, AddPortAuthorization_EmptyParams_ReturnsSuccess, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.AddPortAuthorization("", "");
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, RegisterDataListener_ValidCallback_ReturnsNonZero, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t ret = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_NE(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialSrvClientWithoutServiceTest, RegisterDataListener_NullCallback_ReturnsZero, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    DataReceivedCallback nullCallback;
    ListenerHandle handle = 0;
    int32_t ret = client.RegisterDataListener("/dev/ttyS0", nullCallback, handle);
    EXPECT_EQ(ret, SERIAL_JS_ERR_INVALID_PARAM);
    EXPECT_EQ(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialSrvClientWithoutServiceTest, UnregisterDataListener_ReturnsSuccess, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.UnregisterDataListener("/dev/ttyS0", NONEXISTENT_HANDLE);
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, UnregisterDataListener_ZeroHandle_ReturnsSuccess, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();
    int32_t ret = client.UnregisterDataListener("/dev/ttyS0", 0);
    EXPECT_EQ(ret, SERIAL_OK);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, AllNotSupportedMethods_Return801, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();

    EXPECT_EQ(client.OpenSerial(MakeDefaultPortInfo(), MakeDefaultConfig()), SERIAL_JS_ERR_NOT_SUPPORTED);
    EXPECT_EQ(client.CloseSerial("/dev/ttyS0"), SERIAL_JS_ERR_NOT_SUPPORTED);

    std::vector<uint8_t> data = {TEST_DATA_BYTE};
    int32_t bytesWritten = 0;
    EXPECT_EQ(client.WriteSerial("/dev/ttyS0", data, 0, bytesWritten), SERIAL_JS_ERR_NOT_SUPPORTED);

    EXPECT_EQ(client.Flush("/dev/ttyS0"), SERIAL_JS_ERR_NOT_SUPPORTED);
    EXPECT_EQ(client.Drain("/dev/ttyS0"), SERIAL_JS_ERR_NOT_SUPPORTED);
    EXPECT_EQ(client.SendBrkSignal("/dev/ttyS0"), SERIAL_JS_ERR_NOT_SUPPORTED);
    EXPECT_EQ(client.SetRtsSignal("/dev/ttyS0", true), SERIAL_JS_ERR_NOT_SUPPORTED);

    bool cts = true;
    EXPECT_EQ(client.GetCtsSignal("/dev/ttyS0", cts), SERIAL_JS_ERR_NOT_SUPPORTED);
}

HWTEST_F(SerialSrvClientWithoutServiceTest, AllSuccessMethods_DoNotReturn801, TestSize.Level1)
{
    auto& client = SerialSrvClient::GetInstance();

    std::vector<SerialPortInfo> devices;
    EXPECT_EQ(client.QuerySerialDevices(devices), SERIAL_OK);

    EXPECT_EQ(client.AddPortAuthorization("t", "d"), SERIAL_OK);

    auto callback = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = 0;
    int32_t ret = client.RegisterDataListener("/dev/ttyS0", callback, handle);
    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_NE(handle, static_cast<ListenerHandle>(0));

    EXPECT_EQ(client.UnregisterDataListener("/dev/ttyS0", handle), SERIAL_OK);
}

} // namespace Serial
} // namespace OHOS
