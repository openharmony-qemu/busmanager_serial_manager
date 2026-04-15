/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
#include "iremote_broker.h"
#include "iremote_object.h"
#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "system_ability_definition.h"
#include "serial_srv_client.h"
#include "serial_service_callback.h"
#include "serial_error_code.h"

namespace OHOS {
namespace Serial {
namespace {
constexpr int32_t SERIAL_SERVICE_ID = 4202;
constexpr uint32_t MAX_WAIT_LOAD_SA_SECONDS = 4;
}

SerialSrvClient::SerialSrvClient()
{
    deathRecipient_ = new (std::nothrow) SerialSrvDeathRecipient();
    if (deathRecipient_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Failed to create SerialSrvDeathRecipient");
    }
    callback_ = new (std::nothrow) SerialServiceCallback();
    if (callback_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Failed to create SerialServiceCallback");
    }
}

SerialSrvClient::~SerialSrvClient() {}

SerialSrvClient& SerialSrvClient::GetInstance()
{
    static SerialSrvClient instance;
    return instance;
}

void SerialSrvClient::SerialSrvDeathRecipient::OnRemoteDied(const wptr<IRemoteObject> &remote)
{
    SERIAL_HILOGE(MODULE_INNERKIT, "Remote died");
    SerialSrvClient::GetInstance().ResetProxy(remote);
}

void SerialSrvClient::ResetProxy(const wptr<IRemoteObject> &remote)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter");
    std::lock_guard<std::mutex> lock(mutex_);
    if (proxy_ != nullptr) {
        sptr<IRemoteObject> object = proxy_->AsObject();
        if ((object != nullptr) && (object == remote.promote())) {
            object->RemoveDeathRecipient(deathRecipient_);
            proxy_ = nullptr;
            ConnectUnLocked();
        }
    }
}

int32_t SerialSrvClient::Connect(bool force)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (proxy_ != nullptr) {
        return SERIAL_OK;
    }
    return ConnectUnLocked(force);
}

int32_t SerialSrvClient::ConnectUnLocked(bool force)
{
    sptr<ISystemAbilityManager> sm = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sm == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Get samgr failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    sptr<IRemoteObject> remoteObject = nullptr;
    if (force) {
        SERIAL_HILOGE(MODULE_INNERKIT, "force load start");
        remoteObject = sm->LoadSystemAbility(SERIAL_SERVICE_ID, MAX_WAIT_LOAD_SA_SECONDS);
    } else {
        remoteObject = sm->CheckSystemAbility(SERIAL_SERVICE_ID);
    }

    if (remoteObject == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Get serial service failed. force:%{public}d", force);
        return SERIAL_JS_ERR_SERVICE;
    }

    if (deathRecipient_ != nullptr) {
        remoteObject->AddDeathRecipient(deathRecipient_);
    }

    proxy_ = iface_cast<ISerialServer>(remoteObject);
    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Get serial proxy failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    SERIAL_HILOGI(MODULE_INNERKIT, "Connect success");
    return SERIAL_OK;
}

int32_t SerialSrvClient::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter");
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->QuerySerialDevices(devices);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "QuerySerialDevices ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::OpenSerial(const SerialPortInfo& portInfo, const SerialConfig& config)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s", portInfo.portName.c_str());
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->OpenSerial(portInfo, config, callback_);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "OpenSerial ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::CloseSerial(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s", portName.c_str());
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (callback_ != nullptr) {
        callback_->RemoveAllListeners(portName);
    }

    int32_t ret = proxy_->CloseSerial(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "CloseSerial ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::WriteSerial(const std::string& portName, const std::vector<uint8_t>& data,
    int32_t timeout, int32_t& bytesWritten)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s, timeout=%{public}d", portName.c_str(), timeout);
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->WriteSerial(portName, data, timeout, bytesWritten);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "WriteSerial ret = %{public}d -> jsRet = %{public}d, bytesWritten=%{public}d",
                  ret, jsRet, bytesWritten);
    return jsRet;
}

int32_t SerialSrvClient::Flush(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s", portName.c_str());
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->Flush(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "Flush ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::Drain(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s", portName.c_str());
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->Drain(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "Drain ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::SendBrkSignal(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s", portName.c_str());
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->SendBrkSignal(portName);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "SendBrkSignal ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::SetRtsSignal(const std::string& portName, bool rts)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s, rts=%{public}d", portName.c_str(), rts);
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->SetRtsSignal(portName, rts);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "SetRtsSignal ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

int32_t SerialSrvClient::GetCtsSignal(const std::string& portName, bool& cts)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s", portName.c_str());
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->GetCtsSignal(portName, cts);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "GetCtsSignal ret = %{public}d -> jsRet = %{public}d, cts=%{public}d", ret, jsRet, cts);
    return jsRet;
}

int32_t SerialSrvClient::AddPortAuthorization(const std::string& tokenId, const std::string& deviceId)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, tokenId=%{public}s, deviceId=%{public}s",
                  tokenId.c_str(), deviceId.c_str());
    if (Connect(true) != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
        return SERIAL_JS_ERR_SERVICE;
    }

    if (proxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
        return SERIAL_JS_ERR_SERVICE;
    }

    int32_t ret = proxy_->AddPortAuthorization(tokenId, deviceId);
    int32_t jsRet = SerialInternalToJsCode(ret);
    SERIAL_HILOGI(MODULE_INNERKIT, "AddPortAuthorization ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
    return jsRet;
}

ListenerHandle SerialSrvClient::RegisterDataListener(const std::string& portName, const DataReceivedCallback& callback)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s", portName.c_str());
    if (!callback) {
        SERIAL_HILOGE(MODULE_INNERKIT, "Invalid callback");
        return 0;
    }

    if (callback_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "callback_ is nullptr");
        return 0;
    }

    bool wasFirstListener = !callback_->HasListeners(portName);

    ListenerHandle listenerHandle = callback_->AddListener(portName, callback);
    if (listenerHandle == 0) {
        SERIAL_HILOGE(MODULE_INNERKIT, "AddListener failed");
        return 0;
    }
    SERIAL_HILOGI(MODULE_INNERKIT, "RegisterDataListener success, listenerHandle=%{public}llu, wasFirst=%{public}d",
                  (unsigned long long)listenerHandle, wasFirstListener);

    if (wasFirstListener) {
        if (Connect(true) != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
            callback_->RemoveListener(portName, listenerHandle);
            return 0;
        }

        if (proxy_ == nullptr) {
            SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
            callback_->RemoveListener(portName, listenerHandle);
            return 0;
        }

        int32_t ret = proxy_->StartRead(portName);
        SERIAL_HILOGI(MODULE_INNERKIT, "StartRead ret = %{public}d", ret);
        if (ret != SERIAL_OK) {
            callback_->RemoveListener(portName, listenerHandle);
            return 0;
        }
    }

    return listenerHandle;
}

int32_t SerialSrvClient::UnregisterDataListener(const std::string& portName, ListenerHandle listenerHandle)
{
    SERIAL_HILOGI(MODULE_INNERKIT, "Enter, portName=%{public}s, listenerHandle=%{public}llu",
                  portName.c_str(), (unsigned long long)listenerHandle);
    if (callback_ == nullptr) {
        SERIAL_HILOGE(MODULE_INNERKIT, "callback_ is nullptr");
        return -1;
    }

    size_t countBefore = callback_->GetListenerCount(portName);
    if (countBefore == 0) {
        SERIAL_HILOGW(MODULE_INNERKIT, "No listener for portName=%{public}s", portName.c_str());
        return 0;
    }

    if (!callback_->RemoveListener(portName, listenerHandle)) {
        SERIAL_HILOGW(MODULE_INNERKIT, "RemoveListener failed for portName=%{public}s", portName.c_str());
        return -1;
    }
    size_t countAfter = callback_->GetListenerCount(portName);
    SERIAL_HILOGI(MODULE_INNERKIT, "UnregisterDataListener success, count before=%{public}zu, after=%{public}zu",
                  countBefore, countAfter);

    if (countBefore > 0 && countAfter == 0) {
        if (Connect(true) != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_INNERKIT, "Connect failed");
            return SERIAL_JS_ERR_SERVICE;
        }

        if (proxy_ == nullptr) {
            SERIAL_HILOGE(MODULE_INNERKIT, "proxy is nullptr");
            return SERIAL_JS_ERR_SERVICE;
        }

        int32_t ret = proxy_->StopRead(portName);
        int32_t jsRet = SerialInternalToJsCode(ret);
        SERIAL_HILOGI(MODULE_INNERKIT, "StopRead ret = %{public}d -> jsRet = %{public}d", ret, jsRet);
        return jsRet;
    }

    return SERIAL_OK;
}

}
}
