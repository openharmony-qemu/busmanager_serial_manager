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
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtest/hwext/gtest-ext.h>
#include <gtest/hwext/gtest-tag.h>

#include "serial_service_callback.h"

using testing::ext::TestSize;

namespace OHOS {
namespace Serial {

class SerialServiceCallbackTest : public testing::Test {
public:
    static void SetUpTestCase(void);
    static void TearDownTestCase(void);
    void SetUp();
    void TearDown();

protected:
    sptr<SerialServiceCallback> callback_ = nullptr;
};

void SerialServiceCallbackTest::SetUpTestCase(void) {}

void SerialServiceCallbackTest::TearDownTestCase(void) {}

void SerialServiceCallbackTest::SetUp()
{
    callback_ = new (std::nothrow) SerialServiceCallback();
    ASSERT_NE(callback_, nullptr);
}

void SerialServiceCallbackTest::TearDown()
{
    callback_ = nullptr;
}

HWTEST_F(SerialServiceCallbackTest, AddListener_001, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = callback_->AddListener("/dev/ttyS0", cb);
    EXPECT_NE(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialServiceCallbackTest, AddListener_002, TestSize.Level1)
{
    DataReceivedCallback nullCb;
    ListenerHandle handle = callback_->AddListener("/dev/ttyS0", nullCb);
    EXPECT_EQ(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialServiceCallbackTest, AddListener_003, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle1 = callback_->AddListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddListener("/dev/ttyS0", cb);
    EXPECT_NE(handle1, static_cast<ListenerHandle>(0));
    EXPECT_NE(handle2, static_cast<ListenerHandle>(0));
    EXPECT_NE(handle1, handle2);
}

HWTEST_F(SerialServiceCallbackTest, AddListener_004, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle1 = callback_->AddListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddListener("/dev/ttyS1", cb);
    EXPECT_NE(handle1, static_cast<ListenerHandle>(0));
    EXPECT_NE(handle2, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialServiceCallbackTest, RemoveListener_001, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = callback_->AddListener("/dev/ttyS0", cb);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));

    bool result = callback_->RemoveListener("/dev/ttyS0", handle);
    EXPECT_TRUE(result);
}

HWTEST_F(SerialServiceCallbackTest, RemoveListener_002, TestSize.Level1)
{
    bool result = callback_->RemoveListener("/dev/ttyS0", 999);
    EXPECT_FALSE(result);
}

HWTEST_F(SerialServiceCallbackTest, RemoveListener_003, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = callback_->AddListener("/dev/ttyS0", cb);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));

    bool result1 = callback_->RemoveListener("/dev/ttyS0", handle);
    EXPECT_TRUE(result1);

    bool result2 = callback_->RemoveListener("/dev/ttyS0", handle);
    EXPECT_FALSE(result2);
}

HWTEST_F(SerialServiceCallbackTest, RemoveListener_004, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle1 = callback_->AddListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddListener("/dev/ttyS0", cb);
    ASSERT_NE(handle1, static_cast<ListenerHandle>(0));
    ASSERT_NE(handle2, static_cast<ListenerHandle>(0));

    bool result = callback_->RemoveListener("/dev/ttyS0", handle1);
    EXPECT_TRUE(result);
    EXPECT_TRUE(callback_->HasListeners("/dev/ttyS0"));

    result = callback_->RemoveListener("/dev/ttyS0", handle2);
    EXPECT_TRUE(result);
    EXPECT_FALSE(callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialServiceCallbackTest, RemoveListener_005, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle1 = callback_->AddListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddListener("/dev/ttyS1", cb);
    ASSERT_NE(handle1, static_cast<ListenerHandle>(0));
    ASSERT_NE(handle2, static_cast<ListenerHandle>(0));

    bool result = callback_->RemoveListener("/dev/ttyS0", handle2);
    EXPECT_FALSE(result);
}

HWTEST_F(SerialServiceCallbackTest, RemoveAllListeners_001, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    callback_->AddListener("/dev/ttyS0", cb);
    callback_->AddListener("/dev/ttyS0", cb);
    EXPECT_TRUE(callback_->HasListeners("/dev/ttyS0"));

    callback_->RemoveAllListeners("/dev/ttyS0");
    EXPECT_FALSE(callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialServiceCallbackTest, RemoveAllListeners_002, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    callback_->AddListener("/dev/ttyS0", cb);
    callback_->AddListener("/dev/ttyS1", cb);

    callback_->RemoveAllListeners("/dev/ttyS0");
    EXPECT_FALSE(callback_->HasListeners("/dev/ttyS0"));
    EXPECT_TRUE(callback_->HasListeners("/dev/ttyS1"));
}

HWTEST_F(SerialServiceCallbackTest, RemoveAllListeners_003, TestSize.Level1)
{
    callback_->RemoveAllListeners("/dev/ttyNONEXISTENT");
    EXPECT_FALSE(callback_->HasListeners("/dev/ttyNONEXISTENT"));
}

HWTEST_F(SerialServiceCallbackTest, HasListeners_001, TestSize.Level1)
{
    EXPECT_FALSE(callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialServiceCallbackTest, HasListeners_002, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    callback_->AddListener("/dev/ttyS0", cb);
    EXPECT_TRUE(callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialServiceCallbackTest, HasListeners_003, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    ListenerHandle handle = callback_->AddListener("/dev/ttyS0", cb);
    callback_->RemoveListener("/dev/ttyS0", handle);
    EXPECT_FALSE(callback_->HasListeners("/dev/ttyS0"));
}

HWTEST_F(SerialServiceCallbackTest, GetListenerCount_001, TestSize.Level1)
{
    EXPECT_EQ(callback_->GetListenerCount("/dev/ttyS0"), static_cast<size_t>(0));
}

HWTEST_F(SerialServiceCallbackTest, GetListenerCount_002, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    callback_->AddListener("/dev/ttyS0", cb);
    callback_->AddListener("/dev/ttyS0", cb);
    EXPECT_EQ(callback_->GetListenerCount("/dev/ttyS0"), static_cast<size_t>(2));
}

HWTEST_F(SerialServiceCallbackTest, GetListenerCount_003, TestSize.Level1)
{
    auto cb = [](const std::vector<uint8_t>& data) {};
    callback_->AddListener("/dev/ttyS0", cb);
    callback_->AddListener("/dev/ttyS1", cb);
    EXPECT_EQ(callback_->GetListenerCount("/dev/ttyS0"), static_cast<size_t>(1));
    EXPECT_EQ(callback_->GetListenerCount("/dev/ttyS1"), static_cast<size_t>(1));
}

HWTEST_F(SerialServiceCallbackTest, OnDataRead_001, TestSize.Level1)
{
    std::vector<uint8_t> receivedData;
    auto cb = [&receivedData](const std::vector<uint8_t>& data) {
        receivedData = data;
    };
    callback_->AddListener("/dev/ttyS0", cb);

    std::vector<uint8_t> sendData = {0x01, 0x02, 0x03};
    ErrCode ret = callback_->onDataRead("/dev/ttyS0", sendData);
    EXPECT_EQ(ret, ERR_OK);
    EXPECT_EQ(receivedData.size(), sendData.size());
    EXPECT_EQ(receivedData[0], 0x01);
    EXPECT_EQ(receivedData[1], 0x02);
    EXPECT_EQ(receivedData[2], 0x03);
}

HWTEST_F(SerialServiceCallbackTest, OnDataRead_002, TestSize.Level1)
{
    std::vector<uint8_t> sendData = {0x01, 0x02, 0x03};
    ErrCode ret = callback_->onDataRead("/dev/ttyS0", sendData);
    EXPECT_EQ(ret, ERR_OK);
}

HWTEST_F(SerialServiceCallbackTest, OnDataRead_003, TestSize.Level1)
{
    int callCount1 = 0;
    int callCount2 = 0;
    auto cb1 = [&callCount1](const std::vector<uint8_t>& data) {
        callCount1++;
    };
    auto cb2 = [&callCount2](const std::vector<uint8_t>& data) {
        callCount2++;
    };
    callback_->AddListener("/dev/ttyS0", cb1);
    callback_->AddListener("/dev/ttyS0", cb2);

    std::vector<uint8_t> sendData = {0x01};
    ErrCode ret = callback_->onDataRead("/dev/ttyS0", sendData);
    EXPECT_EQ(ret, ERR_OK);
    EXPECT_EQ(callCount1, 1);
    EXPECT_EQ(callCount2, 1);
}

HWTEST_F(SerialServiceCallbackTest, OnDataRead_004, TestSize.Level1)
{
    std::vector<uint8_t> receivedData;
    auto cb = [&receivedData](const std::vector<uint8_t>& data) {
        receivedData = data;
    };
    callback_->AddListener("/dev/ttyS0", cb);

    std::vector<uint8_t> emptyData;
    ErrCode ret = callback_->onDataRead("/dev/ttyS0", emptyData);
    EXPECT_EQ(ret, ERR_OK);
    EXPECT_TRUE(receivedData.empty());
}

HWTEST_F(SerialServiceCallbackTest, OnDataRead_005, TestSize.Level1)
{
    int callCount1 = 0;
    int callCount2 = 0;
    auto cb1 = [&callCount1](const std::vector<uint8_t>& data) {
        callCount1++;
    };
    auto cb2 = [&callCount2](const std::vector<uint8_t>& data) {
        callCount2++;
    };
    callback_->AddListener("/dev/ttyS0", cb1);
    callback_->AddListener("/dev/ttyS1", cb2);

    std::vector<uint8_t> sendData = {0x01};
    callback_->onDataRead("/dev/ttyS0", sendData);
    EXPECT_EQ(callCount1, 1);
    EXPECT_EQ(callCount2, 0);
}

// ============================================================================
// AddDisconnectedListener 测试
// ============================================================================

HWTEST_F(SerialServiceCallbackTest, AddDisconnectedListener_001, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    ListenerHandle handle = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    EXPECT_NE(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialServiceCallbackTest, AddDisconnectedListener_002, TestSize.Level1)
{
    DisconnectedCallback nullCb;
    ListenerHandle handle = callback_->AddDisconnectedListener("/dev/ttyS0", nullCb);
    EXPECT_EQ(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialServiceCallbackTest, AddDisconnectedListener_003, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    ListenerHandle handle1 = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    EXPECT_NE(handle1, static_cast<ListenerHandle>(0));
    EXPECT_NE(handle2, static_cast<ListenerHandle>(0));
    EXPECT_NE(handle1, handle2);
}

HWTEST_F(SerialServiceCallbackTest, AddDisconnectedListener_004, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    ListenerHandle handle1 = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddDisconnectedListener("/dev/ttyS1", cb);
    EXPECT_NE(handle1, static_cast<ListenerHandle>(0));
    EXPECT_NE(handle2, static_cast<ListenerHandle>(0));
}

// ============================================================================
// RemoveDisconnectedListener 测试
// ============================================================================

HWTEST_F(SerialServiceCallbackTest, RemoveDisconnectedListener_001, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    ListenerHandle handle = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));

    bool result = callback_->RemoveDisconnectedListener("/dev/ttyS0", handle);
    EXPECT_TRUE(result);
}

HWTEST_F(SerialServiceCallbackTest, RemoveDisconnectedListener_002, TestSize.Level1)
{
    bool result = callback_->RemoveDisconnectedListener("/dev/ttyS0", 999);
    EXPECT_FALSE(result);
}

HWTEST_F(SerialServiceCallbackTest, RemoveDisconnectedListener_003, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    ListenerHandle handle = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    ASSERT_NE(handle, static_cast<ListenerHandle>(0));

    bool result1 = callback_->RemoveDisconnectedListener("/dev/ttyS0", handle);
    EXPECT_TRUE(result1);

    bool result2 = callback_->RemoveDisconnectedListener("/dev/ttyS0", handle);
    EXPECT_FALSE(result2);
}

HWTEST_F(SerialServiceCallbackTest, RemoveDisconnectedListener_004, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    ListenerHandle handle1 = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    ASSERT_NE(handle1, static_cast<ListenerHandle>(0));
    ASSERT_NE(handle2, static_cast<ListenerHandle>(0));

    bool result = callback_->RemoveDisconnectedListener("/dev/ttyS0", handle1);
    EXPECT_TRUE(result);
}

// ============================================================================
// RemoveAllDisconnectedListeners 测试
// ============================================================================

HWTEST_F(SerialServiceCallbackTest, RemoveAllDisconnectedListeners_001, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    callback_->AddDisconnectedListener("/dev/ttyS0", cb);

    callback_->RemoveAllDisconnectedListeners("/dev/ttyS0");

    ListenerHandle handle = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    EXPECT_NE(handle, static_cast<ListenerHandle>(0));
}

HWTEST_F(SerialServiceCallbackTest, RemoveAllDisconnectedListeners_002, TestSize.Level1)
{
    auto cb = [](const std::string& portName) {};
    callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    callback_->AddDisconnectedListener("/dev/ttyS1", cb);

    callback_->RemoveAllDisconnectedListeners("/dev/ttyS0");

    ListenerHandle handle1 = callback_->AddDisconnectedListener("/dev/ttyS0", cb);
    ListenerHandle handle2 = callback_->AddDisconnectedListener("/dev/ttyS1", cb);
    EXPECT_NE(handle1, handle2);
}

HWTEST_F(SerialServiceCallbackTest, RemoveAllDisconnectedListeners_003, TestSize.Level1)
{
    callback_->RemoveAllDisconnectedListeners("/dev/ttyNONEXISTENT");
}

// ============================================================================
// onDisconnect 测试
// ============================================================================

HWTEST_F(SerialServiceCallbackTest, OnDisconnect_001, TestSize.Level1)
{
    std::string disconnectedPort;
    auto cb = [&disconnectedPort](const std::string& portName) {
        disconnectedPort = portName;
    };
    callback_->AddDisconnectedListener("/dev/ttyS0", cb);

    ErrCode ret = callback_->onDisconnect("/dev/ttyS0");
    EXPECT_EQ(ret, ERR_OK);
    EXPECT_EQ(disconnectedPort, "/dev/ttyS0");
}

HWTEST_F(SerialServiceCallbackTest, OnDisconnect_002, TestSize.Level1)
{
    ErrCode ret = callback_->onDisconnect("/dev/ttyS0");
    EXPECT_EQ(ret, ERR_OK);
}

HWTEST_F(SerialServiceCallbackTest, OnDisconnect_003, TestSize.Level1)
{
    int callCount1 = 0;
    int callCount2 = 0;
    auto cb1 = [&callCount1](const std::string& portName) {
        callCount1++;
    };
    auto cb2 = [&callCount2](const std::string& portName) {
        callCount2++;
    };
    callback_->AddDisconnectedListener("/dev/ttyS0", cb1);
    callback_->AddDisconnectedListener("/dev/ttyS0", cb2);

    ErrCode ret = callback_->onDisconnect("/dev/ttyS0");
    EXPECT_EQ(ret, ERR_OK);
    EXPECT_EQ(callCount1, 1);
    EXPECT_EQ(callCount2, 1);
}

HWTEST_F(SerialServiceCallbackTest, OnDisconnect_004, TestSize.Level1)
{
    int callCount1 = 0;
    int callCount2 = 0;
    auto cb1 = [&callCount1](const std::string& portName) {
        callCount1++;
    };
    auto cb2 = [&callCount2](const std::string& portName) {
        callCount2++;
    };
    callback_->AddDisconnectedListener("/dev/ttyS0", cb1);
    callback_->AddDisconnectedListener("/dev/ttyS1", cb2);

    callback_->onDisconnect("/dev/ttyS0");
    EXPECT_EQ(callCount1, 1);
    EXPECT_EQ(callCount2, 0);
}

HWTEST_F(SerialServiceCallbackTest, OnDisconnect_CleansUpDataListeners, TestSize.Level1)
{
    auto dataCb = [](const std::vector<uint8_t>& data) {};
    callback_->AddListener("/dev/ttyS0", dataCb);
    EXPECT_TRUE(callback_->HasListeners("/dev/ttyS0"));

    callback_->onDisconnect("/dev/ttyS0");

    EXPECT_FALSE(callback_->HasListeners("/dev/ttyS0"));
}

} // namespace Serial
} // namespace OHOS
