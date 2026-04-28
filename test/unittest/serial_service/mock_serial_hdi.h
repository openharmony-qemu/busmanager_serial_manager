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
class MockSerialDevice : public OHOS::HDI::Serials::V1_0::ISerialDevice {
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
    MOCK_METHOD(bool, IsProxy, (), (override));
};

/**
 * @class MockSerials
 * @brief Mock 实现的 ISerials 接口
 * @details 使用 gmock 实现所有 ISerials 方法，允许测试设置预期行为。
 *          同时提供设备管理功能，模拟真实的 HDI 服务行为。
 * @since 2026-04-23
 */
class MockSerials : public OHOS::HDI::Serials::V1_0::ISerials {
public:
    MockSerials();
    ~MockSerials() override;

    // gmock 方法声明
    MOCK_METHOD(int32_t, QueryDevices, (std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo> & devices), (override));
    MOCK_METHOD(int32_t, OpenDevice,
        (const std::string& portName, const OHOS::HDI::Serials::V1_0::SerialConfig& config,
            const sptr<OHOS::HDI::Serials::V1_0::ISerialDeviceCallback>& cb,
            sptr<OHOS::HDI::Serials::V1_0::ISerialDevice>& device),
        (override));
    MOCK_METHOD(bool, IsProxy, (), (override));

    /**
     * @brief 设置模拟的设备列表
     * @param devices 设备信息列表
     * @details QueryDevices 将返回此列表
     */
    void SetMockDevices(const std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo>& devices);

    /**
     * @brief 设置 OpenDevice 的默认行为
     * @param success 是否成功打开设备
     * @details 设置 OpenDevice 返回成功或失败
     */
    void SetOpenDeviceResult(bool success);

    /**
     * @brief 获取已打开设备数量
     * @return uint32_t 已打开设备数量
     */
    uint32_t GetOpenedDeviceCount();

    /**
     * @brief 获取默认成功行为的 QueryDevices 实现
     * @param devices 输出设备列表
     * @return int32_t HDF_SUCCESS
     * @details 返回预设的设备列表
     */
    int32_t DefaultQueryDevices(std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo>& devices);

    /**
     * @brief 获取默认成功行为的 OpenDevice 实现
     * @param portName 端口名称
     * @param config 配置参数
     * @param cb 回调对象
     * @param device 输出设备对象
     * @return int32_t HDF_SUCCESS 或 HDF_FAILURE
     * @details 根据设置返回成功（创建 MockSerialDevice）或失败
     */
    int32_t DefaultOpenDevice(const std::string& portName, const OHOS::HDI::Serials::V1_0::SerialConfig& config,
        const sptr<OHOS::HDI::Serials::V1_0::ISerialDeviceCallback>& cb,
        sptr<OHOS::HDI::Serials::V1_0::ISerialDevice>& device);

private:
    std::mutex mockMutex_;
    std::vector<OHOS::HDI::Serials::V1_0::SerialDeviceInfo> mockDevices_;
    std::map<std::string, sptr<MockSerialDevice>> openedDevices_;
    bool openDeviceSuccess_ = true;
};

} // namespace Serial
} // namespace OHOS

#endif // MOCK_SERIAL_HDI_H