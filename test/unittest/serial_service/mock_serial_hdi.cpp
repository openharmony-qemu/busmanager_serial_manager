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

#include "mock_serial_hdi.h"
#include "hilog_wrapper.h"

using namespace testing;

namespace OHOS {
namespace Serial {

constexpr int32_t HDF_SUCCESS = 0;
constexpr int32_t HDF_FAILURE = -1;

// ============================================================================
// MockSerialsImpl 实现
// ============================================================================

int32_t MockSerialsImpl::QueryDevices(std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    if (!queryDevicesSuccess_) {
        devices.clear();
        return HDF_FAILURE;
    }
    devices = mockDevices_;
    return HDF_SUCCESS;
}

int32_t MockSerialsImpl::OpenDevice(const std::string& portName, const OHOS::HDI::Serial::V1_0::SerialConfig& config,
    const sptr<OHOS::HDI::Serial::V1_0::ISerialDeviceCallback>& cb,
    sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& device)
{
    std::lock_guard<std::mutex> lock(mockMutex_);

    if (!openDeviceSuccess_) {
        device = nullptr;
        return HDF_FAILURE;
    }

    sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
    if (mockDevice == nullptr) {
        device = nullptr;
        return HDF_FAILURE;
    }

    openedDevices_[portName] = mockDevice;
    device = mockDevice;
    return HDF_SUCCESS;
}

bool MockSerialsImpl::IsProxy()
{
    return false;
}

void MockSerialsImpl::SetMockDevices(const std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    mockDevices_ = devices;
}

void MockSerialsImpl::SetOpenDeviceResult(bool success)
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    openDeviceSuccess_ = success;
}

void MockSerialsImpl::SetQueryDevicesResult(bool success)
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    queryDevicesSuccess_ = success;
}

uint32_t MockSerialsImpl::GetOpenedDeviceCount()
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    return static_cast<uint32_t>(openedDevices_.size());
}

// ============================================================================
// MockSerials 实现
// ============================================================================

MockSerials::MockSerials()
{
    // 设置默认行为：调用 impl_ 的方法
    ON_CALL(*this, QueryDevices(_)).WillByDefault(Invoke(&impl_, &MockSerialsImpl::QueryDevices));
    ON_CALL(*this, OpenDevice(_, _, _, _)).WillByDefault(Invoke(&impl_, &MockSerialsImpl::OpenDevice));
    ON_CALL(*this, IsProxy()).WillByDefault(Invoke(&impl_, &MockSerialsImpl::IsProxy));

    // 设置默认的 NiceMock 行为（不报错未预期的调用）
    ::testing::Mock::AllowLeak(this);
}

MockSerials::~MockSerials()
{
    // 析构时清理资源
}

// 辅助方法代理
void MockSerials::SetMockDevices(const std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo>& devices)
{
    impl_.SetMockDevices(devices);
}

void MockSerials::SetOpenDeviceResult(bool success)
{
    impl_.SetOpenDeviceResult(success);
}

void MockSerials::SetQueryDevicesResult(bool success)
{
    impl_.SetQueryDevicesResult(success);
}

uint32_t MockSerials::GetOpenedDeviceCount()
{
    return impl_.GetOpenedDeviceCount();
}

} // namespace Serial
} // namespace OHOS