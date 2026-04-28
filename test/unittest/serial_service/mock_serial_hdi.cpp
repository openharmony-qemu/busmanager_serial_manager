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
 * @file mock_serial_hdi.cpp
 * @brief Mock HDI 接口实现 - 用于单元测试
 * @details 本文件实现 MockSerials 类，提供可控制的 HDI 模拟行为。
 * @since 2026-04-23
 */

#include "mock_serial_hdi.h"
#include "hilog_wrapper.h"

namespace OHOS {
namespace Serial {

// HDI error codes
constexpr int32_t HDF_SUCCESS = 0;
constexpr int32_t HDF_FAILURE = -1;

MockSerials::MockSerials()
{
    // 初始化默认模拟设备
    mockDevices_.clear();
}

MockSerials::~MockSerials()
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    openedDevices_.clear();
}

void MockSerials::SetMockDevices(const std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    mockDevices_ = devices;
}

void MockSerials::SetOpenDeviceResult(bool success)
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    openDeviceSuccess_ = success;
}

uint32_t MockSerials::GetOpenedDeviceCount()
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    return static_cast<uint32_t>(openedDevices_.size());
}

int32_t MockSerials::DefaultQueryDevices(std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo>& devices)
{
    std::lock_guard<std::mutex> lock(mockMutex_);
    devices = mockDevices_;
    return HDF_SUCCESS;
}

int32_t MockSerials::DefaultOpenDevice(const std::string& portName,
    const OHOS::HDI::Serials::V1_0::SerialConfig& config,
    const sptr<OHOS::HDI::Serials::V1_0::ISerialDeviceCallback>& cb,
    sptr<OHOS::HDI::Serials::V1_0::ISerialDevice>& device)
{
    std::lock_guard<std::mutex> lock(mockMutex_);

    if (!openDeviceSuccess_) {
        device = nullptr;
        return HDF_FAILURE;
    }

    // 创建新的 MockSerialDevice
    sptr<MockSerialDevice> mockDevice = new MockSerialDevice();
    if (mockDevice == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "MockSerials::DefaultOpenDevice: failed to create MockSerialDevice");
        device = nullptr;
        return HDF_FAILURE;
    }

    // 存储已打开设备
    openedDevices_[portName] = mockDevice;
    device = mockDevice;

    SERIAL_HILOGI(MODULE_SERVICE, "MockSerials::DefaultOpenDevice: successfully opened %{public}s", portName.c_str());
    return HDF_SUCCESS;
}

} // namespace Serial
} // namespace OHOS