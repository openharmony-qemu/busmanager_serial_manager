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
 * @file mock_serial_hdi.h
 * @brief Mock HDI 接口实现 - 用于单元测试
 * @details 本文件提供 Mock 实现的 ISerials 和 ISerialDevice 接口，
 *          允许测试代码控制 HDI 层的行为，验证 SerialService 的逻辑。
 *          使用 gmock 的 ON_CALL + Invoke 设置默认行为，同时支持 EXPECT_CALL。
 * @since 2026-04-23
 */

#ifndef MOCK_SERIAL_HDI_H
#define MOCK_SERIAL_HDI_H

#include "hdf_base.h"
#include "v1_0/iserial_device.h"
#include "v1_0/iserial_device_callback.h"
#include "v1_0/iserials.h"
#include "v1_0/serial_types.h"
#include <gmock/gmock.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace OHOS {
namespace Serial {

/**
 * @class MockSerialDevice
 * @brief Mock 实现的 ISerialDevice 接口
 * @details 使用 gmock 实现所有 ISerialDevice 方法，允许测试设置预期行为。
 * @since 2026-04-23
 */
class MockSerialDevice : public OHOS::HDI::Serial::V1_0::ISerialDevice {
public:
    MockSerialDevice() = default;
    ~MockSerialDevice() override = default;

    // gmock 方法声明
    MOCK_METHOD(int32_t, StartRead, (), (override));
    MOCK_METHOD(int32_t, StopRead, (), (override));
    MOCK_METHOD(int32_t, Close, (), (override));
    MOCK_METHOD(int32_t, Write, (const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten), (override));
    MOCK_METHOD(int32_t, Flush, (), (override));
    MOCK_METHOD(int32_t, Drain, (), (override));
    MOCK_METHOD(int32_t, SendBrkSignal, (), (override));
    MOCK_METHOD(int32_t, SetRtsSignal, (bool rts), (override));
    MOCK_METHOD(int32_t, GetCtsSignal, (bool& cts), (override));
    MOCK_METHOD(int32_t, SetDtrSignal, (bool dtr), (override));
    MOCK_METHOD(int32_t, GetDsrSignal, (bool& dsr), (override));
    MOCK_METHOD(bool, IsProxy, (), (override));
};

/**
 * @class MockSerialsImpl
 * @brief MockSerials 的默认实现辅助类
 * @details 提供默认的 QueryDevices、OpenDevice、IsProxy 实现，
 *          MockSerials 通过 Invoke 调用这些方法。
 */
class MockSerialsImpl {
public:
    MockSerialsImpl() = default;
    ~MockSerialsImpl() = default;

    int32_t QueryDevices(std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo>& devices);
    int32_t OpenDevice(const std::string& portName, const OHOS::HDI::Serial::V1_0::SerialConfig& config,
        const sptr<OHOS::HDI::Serial::V1_0::ISerialDeviceCallback>& cb,
        sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& device);
    bool IsProxy();

    void SetMockDevices(const std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo>& devices);
    void SetOpenDeviceResult(bool success);
    void SetQueryDevicesResult(bool success);
    uint32_t GetOpenedDeviceCount();

private:
    std::mutex mockMutex_;
    std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo> mockDevices_;
    std::map<std::string, sptr<MockSerialDevice>> openedDevices_;
    bool openDeviceSuccess_ = true;
    bool queryDevicesSuccess_ = true;
};

/**
 * @class MockSerials
 * @brief Mock 实现的 ISerials 接口
 * @details 使用 gmock 实现所有 ISerials 方法，允许测试设置预期行为。
 *          默认行为通过 ON_CALL + Invoke 设置，调用 MockSerialsImpl 的实现。
 * @since 2026-04-23
 */
class MockSerials : public OHOS::HDI::Serial::V1_0::ISerials {
public:
    MockSerials();
    ~MockSerials() override;

    // gmock 方法声明
    MOCK_METHOD(int32_t, QueryDevices, (std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo> & devices), (override));
    MOCK_METHOD(int32_t, OpenDevice,
        (const std::string& portName, const OHOS::HDI::Serial::V1_0::SerialConfig& config,
            const sptr<OHOS::HDI::Serial::V1_0::ISerialDeviceCallback>& cb,
            sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& device),
        (override));
    MOCK_METHOD(bool, IsProxy, (), (override));

    // 辅助方法 - 代理到 impl_
    void SetMockDevices(const std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo>& devices);
    void SetOpenDeviceResult(bool success);
    void SetQueryDevicesResult(bool success);
    uint32_t GetOpenedDeviceCount();

private:
    MockSerialsImpl impl_;
};

} // namespace Serial
} // namespace OHOS

#endif // MOCK_SERIAL_HDI_H