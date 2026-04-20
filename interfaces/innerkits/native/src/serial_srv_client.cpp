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

#include <sys/types.h>

#include "hilog_wrapper.h"
#include "if_system_ability_manager.h"
#include "iremote_broker.h"
#include "iremote_object.h"
#include "iservice_registry.h"
#include "serial_error_code.h"
#include "serial_service_callback.h"
#include "serial_srv_client.h"
#include "system_ability_definition.h"

namespace OHOS {
namespace Serial {
namespace {
constexpr int32_t SERIAL_SERVICE_ID = 4202;
constexpr uint32_t MAX_WAIT_LOAD_SA_SECONDS = 4;
constexpr size_t WRITE_DATA_MAX_SIZE = 4096;
constexpr int32_t WRITE_TIMEOUT_MAX_MS = 300000;
constexpr int32_t PARITY_MIN = 0;
constexpr int32_t PARITY_MAX = 4;
constexpr int32_t DATA_BITS_MIN = 5;
constexpr int32_t DATA_BITS_MAX = 8;
constexpr int32_t STOP_BITS_MAX = 2;
constexpr int32_t UNREGISTER_LISTENER_ERROR = -1;
} // namespace

SerialSrvClient::SerialSrvClient()
{
    deathRecipient_ = new (std::nothrow) SerialSrvDeathRecipient();
    if (deathRecipient_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[SerialSrvClient] Failed to create SerialSrvDeathRecipient");
    }
    callback_ = new (std::nothrow) SerialServiceCallback();
    if (callback_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[SerialSrvClient] Failed to create SerialServiceCallback");
    }
}

SerialSrvClient::~SerialSrvClient() {}

SerialSrvClient& SerialSrvClient::GetInstance()
{
    static SerialSrvClient instance;
    return instance;
}

void SerialSrvClient::SerialSrvDeathRecipient::OnRemoteDied(const wptr<IRemoteObject>& remote)
{
    SERIAL_HILOGE(MODULE_INNERKIT, "[OnRemoteDied] Remote died");
    SerialSrvClient::GetInstance().ResetProxy(remote);
}

void SerialSrvClient::ResetProxy(const wptr<IRemoteObject>& remote)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[ResetProxy] Enter");
    std::lock_guard<std::mutex> lock(mutex_);
    if (proxy_ != nullptr) {
        sptr<IRemoteObject> object = proxy_->AsObject();
        if ((object != nullptr) && (object == remote.promote())) {
            object->RemoveDeathRecipient(deathRecipient_);
            proxy_ = nullptr;
        }
    }
}

sptr<ISerialServer> SerialSrvClient::GetProxy()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (proxy_ == nullptr) {
        ConnectUnLocked(true);
    }
    return proxy_;
}

int32_t SerialSrvClient::ConnectUnLocked(bool force)
{
    sptr<ISystemAbilityManager> sm = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sm == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[ConnectUnLocked] Get samgr failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    sptr<IRemoteObject> remoteObject = nullptr;
    if (force) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[ConnectUnLocked] Force load start");
        remoteObject = sm->LoadSystemAbility(SERIAL_SERVICE_ID, MAX_WAIT_LOAD_SA_SECONDS);
    } else {
        remoteObject = sm->CheckSystemAbility(SERIAL_SERVICE_ID);
    }

    if (remoteObject == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[ConnectUnLocked] Get serial service failed, force:%{public}d", force);
        return SERIAL_JS_ERR_SERVICE;
    }

    if (deathRecipient_ != nullptr) {
        remoteObject->AddDeathRecipient(deathRecipient_);
    }

    proxy_ = iface_cast<ISerialServer>(remoteObject);
    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[ConnectUnLocked] Get serial proxy failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    SERIAL_HILOGI(MODULE_INNERKIT, "[ConnectUnLocked] Connect success");
    return SERIAL_OK;
}

int32_t SerialSrvClient::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[QuerySerialDevices] Enter");

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[QuerySerialDevices] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->QuerySerialDevices(devices);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[QuerySerialDevices] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::OpenSerial(const SerialPortInfo& portInfo, const SerialConfig& config)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[OpenSerial] Enter, portName=%{public}s", portInfo.portName.c_str());
    if (portInfo.portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[OpenSerial] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }
    if (config.baudRate <= 0) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[OpenSerial] Invalid baudRate=%{public}d", config.baudRate);
        return SERIAL_JS_ERR_INVALID_PARAM;
    }
    if (config.dataBits < DATA_BITS_MIN || config.dataBits > DATA_BITS_MAX) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[OpenSerial] Invalid dataBits=%{public}d", config.dataBits);
        return SERIAL_JS_ERR_INVALID_PARAM;
    }
    if (config.stopBits < 1 || config.stopBits > STOP_BITS_MAX) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[OpenSerial] Invalid stopBits=%{public}d", config.stopBits);
        return SERIAL_JS_ERR_INVALID_PARAM;
    }
    if (config.parity < PARITY_MIN || config.parity > PARITY_MAX) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[OpenSerial] Invalid parity=%{public}d", config.parity);
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[OpenSerial] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->OpenSerial(portInfo, config, callback_);
    int32_t jsRet = SerialInternalToJsCode(ret);
    if (jsRet == SERIAL_OK && callback_ != nullptr) {
        callback_->RemoveAllListeners(portInfo.portName);
    }
    SERIAL_HILOGI(MODULE_INNERKIT, "[OpenSerial] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::CloseSerial(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[CloseSerial] Enter, portName=%{public}s", portName.c_str());
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[CloseSerial] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[CloseSerial] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (callback_ != nullptr) {
        callback_->RemoveAllListeners(portName);
    }

    int32_t ret = proxy->CloseSerial(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[CloseSerial] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    SERIAL_HILOGI(
        MODULE_INNERKIT, "[WriteSerial] Enter, portName=%{public}s, timeout=%{public}d", portName.c_str(), timeout);
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[WriteSerial] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }
    if (data.empty() || data.size() > WRITE_DATA_MAX_SIZE) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[WriteSerial] Invalid data size=%{public}zu", data.size());
        return SERIAL_JS_ERR_INVALID_PARAM;
    }
    if (timeout < 0 || timeout > WRITE_TIMEOUT_MAX_MS) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[WriteSerial] Invalid timeout=%{public}d", timeout);
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[WriteSerial] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->WriteSerial(portName, data, timeout, bytesWritten);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[WriteSerial] ret = %{public}d -> jsRet = %{public}d, bytesWritten=%{public}d", ret,
        jsRet, bytesWritten);
    return jsRet;
}

int32_t SerialSrvClient::Flush(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[Flush] Enter, portName=%{public}s", portName.c_str());
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[Flush] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[Flush] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->Flush(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[Flush] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::Drain(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[Drain] Enter, portName=%{public}s", portName.c_str());
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[Drain] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[Drain] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->Drain(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[Drain] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::SendBrkSignal(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[SendBrkSignal] Enter, portName=%{public}s", portName.c_str());
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[SendBrkSignal] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[SendBrkSignal] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->SendBrkSignal(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[SendBrkSignal] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::SetRtsSignal(const std::string& portName, bool rts)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[SetRtsSignal] Enter, portName=%{public}s, rts=%{public}d", portName.c_str(), rts);
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[SetRtsSignal] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[SetRtsSignal] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->SetRtsSignal(portName, rts);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[SetRtsSignal] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::GetCtsSignal(const std::string& portName, bool& cts)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[GetCtsSignal] Enter, portName=%{public}s", portName.c_str());
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[GetCtsSignal] PortName is empty");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[GetCtsSignal] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->GetCtsSignal(portName, cts);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(
        MODULE_INNERKIT, "[GetCtsSignal] ret = %{public}d -> jsRet = %{public}d, cts=%{public}d", ret, jsRet, cts);
    return jsRet;
}

int32_t SerialSrvClient::AddPortAuthorization(const std::string& tokenId, const std::string& deviceId)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[AddPortAuthorization] Enter, tokenId=%{public}s, deviceId=%{public}s",
        tokenId.c_str(), deviceId.c_str());
    if (tokenId.empty() || deviceId.empty()) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[AddPortAuthorization] Invalid params");
        return SERIAL_JS_ERR_INVALID_PARAM;
    }

    sptr<ISerialServer> proxy = GetProxy();
    if (proxy == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[AddPortAuthorization] Proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy->AddPortAuthorization(tokenId, deviceId);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "[AddPortAuthorization] ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

ListenerHandle SerialSrvClient::RegisterDataListener(const std::string& portName, const DataReceivedCallback& callback)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[RegisterDataListener] Enter, portName=%{public}s", portName.c_str());
    if (!callback) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[RegisterDataListener] Invalid callback");
        return 0;
    }

    if (callback_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[RegisterDataListener] Callback_ is nullptr");
        return 0;
    }

    bool wasFirstListener = !callback_->HasListeners(portName);

    ListenerHandle listenerHandle = callback_->AddListener(portName, callback);
    if (listenerHandle == 0) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[RegisterDataListener] AddListener failed");
        return 0;
    }
    SERIAL_HILOGI(MODULE_INNERKIT, "[RegisterDataListener] Success, listenerHandle=%{public}llu, wasFirst=%{public}d",
        (unsigned long long)listenerHandle, wasFirstListener);

    if (wasFirstListener) {
        sptr<ISerialServer> proxy = GetProxy();
        if (proxy == nullptr) {
            SERIAL_HILOGE(MODULE_INNERKIT, "[RegisterDataListener] Proxy is nullptr");
            callback_->RemoveListener(portName, listenerHandle);
            return 0;
        }

        int32_t ret = proxy->StartRead(portName);
        SERIAL_HILOGI(MODULE_INNERKIT, "[RegisterDataListener] StartRead ret = %{public}d", ret);
        if (ret != SERIAL_OK) {
            callback_->RemoveListener(portName, listenerHandle);
            return 0;
        }
    }

    return listenerHandle;
}

int32_t SerialSrvClient::UnregisterDataListener(const std::string& portName, ListenerHandle listenerHandle)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "[UnregisterDataListener] Enter, portName=%{public}s, listenerHandle=%{public}llu",
        portName.c_str(), (unsigned long long)listenerHandle);
    if (callback_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[UnregisterDataListener] Callback_ is nullptr");
        return UNREGISTER_LISTENER_ERROR;
    }

    size_t countBefore = callback_->GetListenerCount(portName);
    if (countBefore == 0) {
        SERIAL_HILOGW(
            MODULE_INNERKIT, "[UnregisterDataListener] No listener for portName=%{public}s", portName.c_str());
        return SERIAL_OK;
    }

    if (!callback_->RemoveListener(portName, listenerHandle)) {
        SERIAL_HILOGW(MODULE_INNERKIT, "[UnregisterDataListener] RemoveListener failed for portName=%{public}s",
            portName.c_str());
        return UNREGISTER_LISTENER_ERROR;
    }
    size_t countAfter = callback_->GetListenerCount(portName);
    SERIAL_HILOGI(MODULE_INNERKIT, "[UnregisterDataListener] Success, count before=%{public}zu, after=%{public}zu",
        countBefore, countAfter);

    if (countBefore > 0 && countAfter == 0) {
        sptr<ISerialServer> proxy = GetProxy();
        if (proxy == nullptr) {
            SERIAL_HILOGE(MODULE_INNERKIT, "[UnregisterDataListener] Proxy is nullptr");
            return SERIAL_JS_ERR_SERVICE;
        }

        int32_t ret = proxy->StopRead(portName);
        int32_t jsRet = SerialInternalToJsCode(ret);
        SERIAL_HILOGI(
            MODULE_INNERKIT, "[UnregisterDataListener] StopRead ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
        return jsRet;
    }

    return SERIAL_OK;
}

} // namespace Serial
} // namespace OHOS
