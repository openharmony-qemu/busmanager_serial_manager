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

#include "ohos.busManager.serial.impl.hpp"
#include "ohos.busManager.serial.proj.hpp"
#include "taihe/runtime.hpp"

#include <cstdint>
#include <string>

#include "hilog_wrapper.h"

using namespace OHOS::Serial;
using namespace taihe;

namespace {
using TaiheSerialPortInfo = ohos::busManager::serial::SerialPortInfo;
using TaiheSerialPort = ohos::busManager::serial::SerialPort;
using TaiheSerialConfigs = ohos::busManager::serial::SerialConfigs;

class SerialPortInfoHolder {
public:
    SerialPortInfoHolder(std::string portName, int32_t productId, int32_t vendorId, std::string manufacturer)
        : portName_(std::move(portName)),
          productId_(productId),
          vendorId_(vendorId),
          manufacturer_(std::move(manufacturer))
    {
    }

    taihe::string getPortName() const
    {
        return taihe::string(portName_);
    }

    taihe::optional<int32_t> getProductId() const
    {
        if (productId_ != 0) {
            return optional<int32_t>::make(productId_);
        }
        return optional<int32_t>();
    }

    taihe::optional<int32_t> getVendorId() const
    {
        if (vendorId_ != 0) {
            return optional<int32_t>::make(vendorId_);
        }
        return optional<int32_t>();
    }

    taihe::optional<taihe::string> getManufacturer() const
    {
        if (!manufacturer_.empty()) {
            return optional<taihe::string>::make(taihe::string(manufacturer_));
        }
        return optional<taihe::string>();
    }

private:
    std::string portName_;
    int32_t productId_;
    int32_t vendorId_;
    std::string manufacturer_;
};

class SerialPortHolder {
public:
    SerialPortHolder(std::string portName, int32_t productId, int32_t vendorId, std::string manufacturer)
        : portName_(std::move(portName)),
          productId_(productId),
          vendorId_(vendorId),
          manufacturer_(std::move(manufacturer))
    {
    }

    TaiheSerialPortInfo getPortInfo() const
    {
        SERIAL_HILOGI(MODULE_TAIHE, "getPortInfo stub, portName=%{public}s", portName_.c_str());
        return taihe::make_holder<SerialPortInfoHolder, TaiheSerialPortInfo>(
            portName_, productId_, vendorId_, manufacturer_);
    }

    void open(optional_view<TaiheSerialConfigs> config)
    {
        SERIAL_HILOGW(MODULE_TAIHE, "open stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    void close()
    {
        SERIAL_HILOGW(MODULE_TAIHE, "close stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    int32_t write(array_view<uint8_t> data, optional_view<int32_t> timeout)
    {
        SERIAL_HILOGW(MODULE_TAIHE, "write stub (service not deployed), portName=%{public}s", portName_.c_str());
        return 0;
    }

    void onDataRead(taihe::callback<void(array_view<uint8_t>)> callback)
    {
        SERIAL_HILOGW(MODULE_TAIHE, "onDataRead stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    void offDataRead(optional_view<taihe::callback<void(array_view<uint8_t>)>> callback)
    {
        SERIAL_HILOGW(MODULE_TAIHE, "offDataRead stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    void flush()
    {
        SERIAL_HILOGW(MODULE_TAIHE, "flush stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    void drain()
    {
        SERIAL_HILOGW(MODULE_TAIHE, "drain stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    void setRts(bool enable)
    {
        SERIAL_HILOGW(MODULE_TAIHE, "setRts stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    bool getCts()
    {
        SERIAL_HILOGW(MODULE_TAIHE, "getCts stub (service not deployed), portName=%{public}s", portName_.c_str());
        return false;
    }

    void sendBrk()
    {
        SERIAL_HILOGW(MODULE_TAIHE, "sendBrk stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    void setDtr(bool enable)
    {
        SERIAL_HILOGW(MODULE_TAIHE, "setDtr stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    bool getDsr()
    {
        SERIAL_HILOGW(MODULE_TAIHE, "getDsr stub (service not deployed), portName=%{public}s", portName_.c_str());
        return false;
    }

    void onDisconnect(taihe::callback<void(uintptr_t)> callback)
    {
        SERIAL_HILOGW(MODULE_TAIHE, "onDisconnect stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

    void offDisconnect(optional_view<taihe::callback<void(uintptr_t)>> callback)
    {
        SERIAL_HILOGW(
            MODULE_TAIHE, "offDisconnect stub (service not deployed), portName=%{public}s", portName_.c_str());
    }

private:
    std::string portName_;
    int32_t productId_;
    int32_t vendorId_;
    std::string manufacturer_;
};

array<TaiheSerialPort> GetSerialPortListSync()
{
    SERIAL_HILOGI(MODULE_TAIHE, "GetSerialPortListSync stub (service not deployed)");
    return array<TaiheSerialPort>(static_cast<TaiheSerialPort*>(nullptr), 0);
}

void AddPortAuthorizationSync(taihe::string_view tokenId, taihe::string_view deviceId)
{
    SERIAL_HILOGI(MODULE_TAIHE, "AddPortAuthorizationSync stub (service not deployed)");
}
} // namespace

// NOLINTBEGIN
TH_EXPORT_CPP_API_getSerialPortListSync(GetSerialPortListSync);
TH_EXPORT_CPP_API_addPortAuthorizationSync(AddPortAuthorizationSync);
// NOLINTEND
