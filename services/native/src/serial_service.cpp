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

#include <iomanip>
#include <sstream>

#include "hilog_wrapper.h"
#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "serial_error_code.h"
#include "serial_service.h"
#include "system_ability_definition.h"

namespace OHOS {
namespace Serial {

constexpr uint8_t ASCII_PRINTABLE_MIN = 32;
constexpr uint8_t ASCII_PRINTABLE_MAX = 126;
constexpr int HEX_WIDTH = 2;

REGISTER_SYSTEM_ABILITY_BY_ID(SerialService, SERIAL_SYSTEM_ABILITY_ID, true);

SerialService::SerialService(int32_t systemAbilityId, bool runOnCreate) : SystemAbility(systemAbilityId, runOnCreate)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService constructed, systemAbilityId=%{public}d, runOnCreate=%{public}d",
        systemAbilityId, runOnCreate);
}

SerialService::~SerialService()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService destroyed");
}

void SerialService::OnStart()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnStart");
    if (registerToService_) {
        SERIAL_HILOGW(MODULE_SERVICE, "SerialService already started");
        return;
    }

    if (!Publish(this)) {
        SERIAL_HILOGE(MODULE_SERVICE, "SerialService publish failed");
        return;
    }

    registerToService_ = true;
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnStart success");
}

void SerialService::OnStop()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnStop");
    registerToService_ = false;
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnStop success");
}

int32_t SerialService::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices called");
    return SERIAL_OK;
}

int32_t SerialService::OpenSerial(
    const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "OpenSerial called, portName=%{public}s, productId=%{public}d, vendorId=%{public}d, "
        "manufacturer=%{public}s, baudRate=%{public}d, dataBits=%{public}d, stopBits=%{public}d, "
        "parity=%{public}d, rtscts=%{public}d, xon=%{public}d, xoff=%{public}d, xany=%{public}d, "
        "token=%{public}p",
        portInfo.portName.c_str(), portInfo.productId, portInfo.vendorId, portInfo.manufacturer.c_str(),
        config.baudRate, config.dataBits, config.stopBits, config.parity, config.rtscts, config.xon, config.xoff,
        config.xany, token.GetRefPtr());
    return SERIAL_OK;
}

int32_t SerialService::CloseSerial(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial called, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial called, portName=%{public}s, dataSize=%{public}zu, timeout=%{public}d",
        portName.c_str(), data.size(), timeout);

    std::ostringstream hexStream;
    std::ostringstream strStream;
    for (size_t i = 0; i < data.size(); ++i) {
        hexStream << std::hex << std::setfill('0') << std::setw(HEX_WIDTH) << static_cast<int>(data[i]);
        if (i < data.size() - 1) {
            hexStream << " ";
        }
        if (data[i] >= ASCII_PRINTABLE_MIN && data[i] <= ASCII_PRINTABLE_MAX) {
            strStream << static_cast<char>(data[i]);
        } else {
            strStream << ".";
        }
    }
    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial data hex: %{public}s", hexStream.str().c_str());
    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial data str: %{public}s", strStream.str().c_str());

    bytesWritten = static_cast<int32_t>(data.size());
    return SERIAL_OK;
}

int32_t SerialService::StartRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "StartRead called, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::StopRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "StopRead called, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::Flush(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "Flush called, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::Drain(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "Drain called, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::SendBrkSignal(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SendBrkSignal called, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::SetRtsSignal(const std::string& portName, bool rts)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SetRtsSignal called, portName=%{public}s, rts=%{public}d", portName.c_str(), rts);
    return SERIAL_OK;
}

int32_t SerialService::GetCtsSignal(const std::string& portName, bool& cts)
{
    SERIAL_HILOGI(MODULE_SERVICE, "GetCtsSignal called, portName=%{public}s", portName.c_str());
    cts = false;
    return SERIAL_OK;
}

int32_t SerialService::AddPortAuthorization(const std::string& tokenId, const std::string& deviceId)
{
    SERIAL_HILOGI(MODULE_SERVICE, "AddPortAuthorization called, tokenId=%{public}s, deviceId=%{public}s",
        tokenId.c_str(), deviceId.c_str());
    return SERIAL_OK;
}

} // namespace Serial
} // namespace OHOS
