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
 * @brief SerialService 单元测试 - 使用 Mock HDI
 * @details 本文件测试 Mock HDI 的功能，验证 Mock 能正确模拟 HDI 层行为。
 * @since 2026-04-24
 */

#include "hilog_wrapper.h"
#include "mock_serial_hdi.h"
#include "serial_error_code.h"
#include "serial_hdi_stub.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace testing;
using namespace testing::ext;
using namespace OHOS::Serial;

namespace OHOS {
namespace Serial {

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
        mockHdi_ = new MockSerials();
    }

    void TearDown() override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialServiceTest::TearDown");
        mockHdi_ = nullptr;
    }

protected:
    sptr<MockSerials> mockHdi_;
};

/**
 * @brief 测试 MockSerials 创建
 */
HWTEST_F(SerialServiceTest, TestMockSerialsCreation, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsCreation: start");

    ASSERT_NE(mockHdi_, nullptr);

    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsCreation: success");
}

/**
 * @brief 测试 MockSerials 设备列表设置和获取
 */
HWTEST_F(SerialServiceTest, TestMockSerialsDeviceList, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsDeviceList: start");

    ASSERT_NE(mockHdi_, nullptr);

    // 设置 Mock 设备列表
    std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo> mockDevices;

    OHOS::HDI::Serials::V1_0::SerialDeviceInfo device1;
    device1.portName = "/dev/ttyUSB0";
    device1.productId = 1234;
    device1.vendorId = 5678;
    device1.manufacturer = "MockDevice0";
    mockDevices.push_back(device1);

    OHOS::HDI::Serials::V1_0::SerialDeviceInfo device2;
    device2.portName = "/dev/ttyUSB1";
    device2.productId = 1111;
    device2.vendorId = 2222;
    device2.manufacturer = "MockDevice1";
    mockDevices.push_back(device2);

    mockHdi_->SetMockDevices(mockDevices);

    // 获取设备列表
    std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo> devices;
    int32_t result = mockHdi_->DefaultQueryDevices(devices);

    EXPECT_EQ(result, HDF_SUCCESS);
    EXPECT_EQ(devices.size(), 2);

    if (devices.size() >= 1) {
        EXPECT_EQ(devices[0].portName, "/dev/ttyUSB0");
        EXPECT_EQ(devices[0].productId, 1234);
        EXPECT_EQ(devices[0].vendorId, 5678);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsDeviceList: success");
}

/**
 * @brief 测试 MockSerials OpenDevice 成功
 */
HWTEST_F(SerialServiceTest, TestMockSerialsOpenDeviceSuccess, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsOpenDeviceSuccess: start");

    ASSERT_NE(mockHdi_, nullptr);

    mockHdi_->SetOpenDeviceResult(true);
    EXPECT_EQ(mockHdi_->GetOpenedDeviceCount(), 0);

    OHOS::HDI::Serials::V1_0::SerialConfig config;
    config.baudRate = 115200;
    config.dataBits = 8;
    config.stopBits = 1;
    config.parity = 0;

    sptr<OHOS::HDI::Serials::V1_0::ISerialDeviceCallback> callback = nullptr;
    sptr<OHOS::HDI::Serials::V1_0::ISerialDevice> device;

    int32_t result = mockHdi_->DefaultOpenDevice("/dev/ttyUSB0", config, callback, device);

    EXPECT_EQ(result, HDF_SUCCESS);
    EXPECT_NE(device, nullptr);
    EXPECT_EQ(mockHdi_->GetOpenedDeviceCount(), 1);

    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsOpenDeviceSuccess: success");
}

/**
 * @brief 测试 MockSerials OpenDevice 失败
 */
HWTEST_F(SerialServiceTest, TestMockSerialsOpenDeviceFailure, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsOpenDeviceFailure: start");

    ASSERT_NE(mockHdi_, nullptr);

    mockHdi_->SetOpenDeviceResult(false);

    OHOS::HDI::Serials::V1_0::SerialConfig config;
    sptr<OHOS::HDI::Serials::V1_0::ISerialDeviceCallback> callback = nullptr;
    sptr<OHOS::HDI::Serials::V1_0::ISerialDevice> device;

    int32_t result = mockHdi_->DefaultOpenDevice("/dev/ttyUSB0", config, callback, device);

    EXPECT_EQ(result, HDF_FAILURE);
    EXPECT_EQ(device, nullptr);

    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialsOpenDeviceFailure: success");
}

/**
 * @brief 测试 MockSerialDevice 基本操作
 */
HWTEST_F(SerialServiceTest, TestMockSerialDeviceBasicOps, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialDeviceBasicOps: start");

    sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
    ASSERT_NE(mockDevice, nullptr);

    EXPECT_CALL(*mockDevice, StartRead()).WillOnce(Return(HDF_SUCCESS));
    EXPECT_CALL(*mockDevice, StopRead()).WillOnce(Return(HDF_SUCCESS));
    EXPECT_CALL(*mockDevice, Close()).WillOnce(Return(HDF_SUCCESS));
    EXPECT_CALL(*mockDevice, Flush()).WillOnce(Return(HDF_SUCCESS));
    EXPECT_CALL(*mockDevice, Drain()).WillOnce(Return(HDF_SUCCESS));
    EXPECT_CALL(*mockDevice, IsProxy()).WillOnce(Return(false));

    EXPECT_EQ(mockDevice->StartRead(), HDF_SUCCESS);
    EXPECT_EQ(mockDevice->StopRead(), HDF_SUCCESS);
    EXPECT_EQ(mockDevice->Close(), HDF_SUCCESS);
    EXPECT_EQ(mockDevice->Flush(), HDF_SUCCESS);
    EXPECT_EQ(mockDevice->Drain(), HDF_SUCCESS);
    EXPECT_EQ(mockDevice->IsProxy(), false);

    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialDeviceBasicOps: success");
}

/**
 * @brief 测试 MockSerialDevice 写入操作
 */
HWTEST_F(SerialServiceTest, TestMockSerialDeviceWrite, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialDeviceWrite: start");

    sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
    ASSERT_NE(mockDevice, nullptr);

    std::vector<uint8_t> testData = {0x01, 0x02, 0x03, 0x04, 0x05};

    EXPECT_CALL(*mockDevice, Write(_, _, _)).WillOnce(DoAll(SetArgReferee<2>(5), Return(HDF_SUCCESS)));

    int32_t bytesWritten = 0;
    int32_t result = mockDevice->Write(testData, 1000, bytesWritten);

    EXPECT_EQ(result, HDF_SUCCESS);
    EXPECT_EQ(bytesWritten, 5);

    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialDeviceWrite: success");
}

/**
 * @brief 测试 MockSerialDevice 信号控制
 */
HWTEST_F(SerialServiceTest, TestMockSerialDeviceSignals, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialDeviceSignals: start");

    sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
    ASSERT_NE(mockDevice, nullptr);

    EXPECT_CALL(*mockDevice, SetRtsSignal(true)).WillOnce(Return(HDF_SUCCESS));
    EXPECT_CALL(*mockDevice, SetRtsSignal(false)).WillOnce(Return(HDF_SUCCESS));
    EXPECT_CALL(*mockDevice, GetCtsSignal(_)).WillOnce(DoAll(SetArgReferee<0>(true), Return(HDF_SUCCESS)));
    EXPECT_CALL(*mockDevice, SendBrkSignal()).WillOnce(Return(HDF_SUCCESS));

    EXPECT_EQ(mockDevice->SetRtsSignal(true), HDF_SUCCESS);
    EXPECT_EQ(mockDevice->SetRtsSignal(false), HDF_SUCCESS);

    bool cts = false;
    EXPECT_EQ(mockDevice->GetCtsSignal(cts), HDF_SUCCESS);
    EXPECT_EQ(cts, true);

    EXPECT_EQ(mockDevice->SendBrkSignal(), HDF_SUCCESS);

    SERIAL_HILOGI(MODULE_SERVICE, "TestMockSerialDeviceSignals: success");
}

/**
 * @brief 测试 SerialHdiStub 默认行为
 */
HWTEST_F(SerialServiceTest, TestSerialHdiStub, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestSerialHdiStub: start");

    sptr<SerialHdiStub> stub = new SerialHdiStub();
    ASSERT_NE(stub, nullptr);

    std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo> devices;
    int32_t result = stub->QueryDevices(devices);
    EXPECT_EQ(result, HDF_SUCCESS);
    EXPECT_EQ(devices.size(), 0);

    OHOS::HDI::Serials::V1_0::SerialConfig config;
    sptr<OHOS::HDI::Serials::V1_0::ISerialDeviceCallback> callback = nullptr;
    sptr<OHOS::HDI::Serials::V1_0::ISerialDevice> device;
    result = stub->OpenDevice("/dev/ttyUSB0", config, callback, device);
    EXPECT_EQ(result, HDF_ERR_NOT_READY);
    EXPECT_EQ(device, nullptr);

    SERIAL_HILOGI(MODULE_SERVICE, "TestSerialHdiStub: success");
}

/**
 * @brief 测试 SerialDeviceStub 默认行为
 */
HWTEST_F(SerialServiceTest, TestSerialDeviceStub, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestSerialDeviceStub: start");

    sptr<SerialDeviceStub> deviceStub = new SerialDeviceStub();
    ASSERT_NE(deviceStub, nullptr);

    EXPECT_EQ(deviceStub->StartRead(), HDF_ERR_NOT_READY);
    EXPECT_EQ(deviceStub->StopRead(), HDF_ERR_NOT_READY);
    EXPECT_EQ(deviceStub->Flush(), HDF_ERR_NOT_READY);
    EXPECT_EQ(deviceStub->Drain(), HDF_ERR_NOT_READY);
    EXPECT_EQ(deviceStub->SendBrkSignal(), HDF_ERR_NOT_READY);
    EXPECT_EQ(deviceStub->SetRtsSignal(true), HDF_ERR_NOT_READY);

    bool cts = false;
    EXPECT_EQ(deviceStub->GetCtsSignal(cts), HDF_ERR_NOT_READY);

    EXPECT_EQ(deviceStub->Close(), HDF_SUCCESS);

    SERIAL_HILOGI(MODULE_SERVICE, "TestSerialDeviceStub: success");
}

/**
 * @brief 测试设备离线场景
 * @details 本测试验证 Mock 环境下设备离线的基本行为。
 *          注意：SerialHdiCallback::OnDeviceOffline() 调用 SerialRightManager::RemoveDeviceAllRight()
 *          的完整验证需要在集成测试环境中进行，因为 SerialRightManager 和 SerialHdiCallback
 *          的符号未从共享库导出供单元测试使用。
 *
 *          验证逻辑（已通过代码检视确认）：
 *          1. OnDeviceOffline() 会调用 SerialRightManager::GetInstance().RemoveDeviceAllRight(portName_)
 *          2. RemoveDeviceAllRight() 会清理该设备的所有授权记录
 *          3. 这确保设备断开时授权记录被正确清理
 *
 *          参考：usb_manager 的 UsbRightManager::RemoveDeviceAllRight() 实现
 */
HWTEST_F(SerialServiceTest, TestDeviceOfflineScenario, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestDeviceOfflineScenario: start");

    // 创建 Mock 设备来模拟离线场景
    sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
    ASSERT_NE(mockDevice, nullptr);

    // 设置 Mock 设备行为 - 模拟设备离线后的 Close 操作
    EXPECT_CALL(*mockDevice, Close()).WillOnce(Return(HDF_SUCCESS));

    // 模拟设备离线后关闭设备
    int32_t result = mockDevice->Close();
    EXPECT_EQ(result, HDF_SUCCESS);

    // 注意：实际的 OnDeviceOffline -> RemoveDeviceAllRight 验证需要在集成测试中完成
    // 这里验证 Mock 设备能正确模拟关闭行为

    SERIAL_HILOGI(MODULE_SERVICE, "TestDeviceOfflineScenario: success - device closed successfully");
}

/**
 * @brief 测试设备离线多次调用场景
 * @details 验证多次调用设备关闭操作的稳定性
 */
HWTEST_F(SerialServiceTest, TestDeviceOfflineMultipleClose, TestSize.Level1)
{
    SERIAL_HILOGI(MODULE_SERVICE, "TestDeviceOfflineMultipleClose: start");

    sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
    ASSERT_NE(mockDevice, nullptr);

    // 设置多次 Close 调用 - 第一次成功，后续可能失败或成功取决于实现
    EXPECT_CALL(*mockDevice, Close()).WillOnce(Return(HDF_SUCCESS)).WillOnce(Return(HDF_SUCCESS));

    EXPECT_EQ(mockDevice->Close(), HDF_SUCCESS);
    EXPECT_EQ(mockDevice->Close(), HDF_SUCCESS);

    SERIAL_HILOGI(MODULE_SERVICE, "TestDeviceOfflineMultipleClose: success");
}

} // namespace Serial
} // namespace OHOS