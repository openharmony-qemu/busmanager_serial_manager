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

#include "serial_srv_client.h"

#include "hilog_wrapper.h"
#include "serial_error_code.h"

namespace OHOS {
namespace Serial {
namespace {
constexpr int32_t SERIAL_JS_ERR_NOT_SUPPORTED = 801;
constexpr ListenerHandle STUB_FIRST_LISTENER_HANDLE = 1;
} // namespace

SerialSrvClient::SerialSrvClient() {}

SerialSrvClient::~SerialSrvClient() {}

SerialSrvClient& SerialSrvClient::GetInstance()
{
    static SerialSrvClient instance;
    return instance;
}

int32_t SerialSrvClient::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[QuerySerialDevices] Stub: service not deployed, return empty list");
    devices.clear();
    return SERIAL_OK;
}

int32_t SerialSrvClient::OpenSerial(const SerialPortInfo& portInfo, const SerialConfig& config)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[OpenSerial] Stub: service not deployed");
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::CloseSerial(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[CloseSerial] Stub: service not deployed");
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[WriteSerial] Stub: service not deployed");
    bytesWritten = 0;
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::Flush(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[Flush] Stub: service not deployed");
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::Drain(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[Drain] Stub: service not deployed");
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::SendBrkSignal(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[SendBrkSignal] Stub: service not deployed");
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::SetRtsSignal(const std::string& portName, bool rts)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[SetRtsSignal] Stub: service not deployed");
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::GetCtsSignal(const std::string& portName, bool& cts)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[GetCtsSignal] Stub: service not deployed");
    cts = false;
    return SERIAL_JS_ERR_NOT_SUPPORTED;
}

int32_t SerialSrvClient::AddPortAuthorization(const std::string& tokenId, const std::string& deviceId)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[AddPortAuthorization] Stub: service not deployed, return success");
    return SERIAL_OK;
}

int32_t SerialSrvClient::RegisterDataListener(
    const std::string& portName, const DataReceivedCallback& callback, ListenerHandle& listenerHandle)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[RegisterDataListener] Stub: service not deployed, return success");
    if (!callback) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[RegisterDataListener] Invalid callback");
        listenerHandle = 0;
        return SERIAL_JS_ERR_INVALID_PARAM;
    }
    listenerHandle = STUB_FIRST_LISTENER_HANDLE;
    return SERIAL_OK;
}

int32_t SerialSrvClient::UnregisterDataListener(const std::string& portName, ListenerHandle listenerHandle)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[UnregisterDataListener] Stub: service not deployed, return success");
    return SERIAL_OK;
}

} // namespace Serial
} // namespace OHOS
