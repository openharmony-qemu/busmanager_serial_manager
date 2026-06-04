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

#ifndef SERIAL_HDI_STUB_H
#define SERIAL_HDI_STUB_H

#include "hdf_base.h"
#include "v1_0/iserial_device.h"
#include "v1_0/iserial_device_callback.h"
#include "v1_0/iserials.h"
#include "v1_0/serial_types.h"
#include <string>
#include <vector>

namespace OHOS {
namespace Serial {

// HDI error codes from hdf_base.h
constexpr int32_t HDF_SUCCESS = 0;
constexpr int32_t HDF_FAILURE = -1;
constexpr int32_t HDF_ERR_NOT_SUPPORT = -2;
constexpr int32_t HDF_ERR_NOT_READY = -3;

/**
 * SerialDeviceStub - Stub implementation of ISerialDevice
 * Provides placeholder implementation when no real HDI device is available.
 * All operations return HDF_ERR_NOT_READY.
 */
class SerialDeviceStub : public OHOS::HDI::Serial::V1_0::ISerialDevice {
public:
    SerialDeviceStub() = default;
    ~SerialDeviceStub() override = default;

    int32_t StartRead() override
    {
        return HDF_ERR_NOT_READY;
    }

    int32_t StopRead() override
    {
        return HDF_ERR_NOT_READY;
    }

    int32_t Close() override
    {
        return HDF_SUCCESS; // Always succeed for stub close
    }

    int32_t Write(const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten) override
    {
        bytesWritten = 0;
        return HDF_ERR_NOT_READY;
    }

    int32_t Flush() override
    {
        return HDF_ERR_NOT_READY;
    }

    int32_t Drain() override
    {
        return HDF_ERR_NOT_READY;
    }

    int32_t SendBrkSignal() override
    {
        return HDF_ERR_NOT_READY;
    }

    int32_t SetRtsSignal(bool rts) override
    {
        return HDF_ERR_NOT_READY;
    }

    int32_t GetCtsSignal(bool& cts) override
    {
        cts = false;
        return HDF_ERR_NOT_READY;
    }

    int32_t SetDtrSignal(bool dtr) override
    {
        return HDF_ERR_NOT_READY;
    }

    int32_t GetDsrSignal(bool& dsr) override
    {
        dsr = false;
        return HDF_ERR_NOT_READY;
    }

    bool IsProxy() override
    {
        return false;
    }
};

/**
 * SerialHdiStub - Stub implementation of ISerials
 * Provides placeholder implementation when no real HDI service is available.
 * QueryDevices returns empty list, OpenDevice returns error.
 */
class SerialHdiStub : public OHOS::HDI::Serial::V1_0::ISerials {
public:
    SerialHdiStub() = default;
    ~SerialHdiStub() override = default;

    int32_t QueryDevices(std::vector<OHOS::HDI::Serial::V1_0::SerialDeviceInfo>& devices) override
    {
        devices.clear();
        return HDF_SUCCESS; // Return empty list successfully
    }

    int32_t OpenDevice(const std::string& portName, const OHOS::HDI::Serial::V1_0::SerialConfig& config,
        const sptr<OHOS::HDI::Serial::V1_0::ISerialDeviceCallback>& cb,
        sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& device) override
    {
        device = nullptr;
        return HDF_ERR_NOT_READY;
    }

    bool IsProxy() override
    {
        return false;
    }
};

} // namespace Serial
} // namespace OHOS

#endif // SERIAL_HDI_STUB_H