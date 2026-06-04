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

#include <semaphore.h>

#include "ability_connect_callback_stub.h"
#include "ability_manager_client.h"
#include "hilog_wrapper.h"
#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "serial_error_code.h"
#include "serial_service.h"
#include "string_ex.h"
#include "system_ability_definition.h"
#include "want.h"

namespace OHOS {
namespace Serial {

namespace {
constexpr int32_t MESSAGE_PARCEL_KEY_SIZE = 3;
constexpr uint32_t CMD_CODE_SHOW_DIALOG = 1;
constexpr int32_t ERR_CODE_OK = 0;

constexpr int32_t DEV0_PRODUCT_ID = 1;
constexpr int32_t DEV0_VENDOR_ID = 1;
constexpr int32_t DEV1_PRODUCT_ID = 2;
constexpr int32_t DEV1_VENDOR_ID = 2;

constexpr const char* HARDCODED_APP_NAME = "SerialAuthTest";
constexpr const char* HARDCODED_TOKEN_ID = "12345678";
constexpr const char* HARDCODED_DEVICE_ID = "test-device-serial-auth";
constexpr int32_t HARDCODED_USER_ID = 100;
} // namespace

static sem_t g_dialogSem;
static std::mutex g_dialogParamsMutex;
static std::map<std::string, std::string> g_dialogParams;

class SerialDialogConn : public AAFwk::AbilityConnectionStub {
public:
    void OnAbilityConnectDone(
        const AppExecFwk::ElementName& element, const sptr<IRemoteObject>& remoteObject, int32_t resultCode) override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "OnAbilityConnectDone resultCode=%{public}d", resultCode);
        if (remoteObject == nullptr) {
            SERIAL_HILOGE(MODULE_SERVICE, "remoteObject is null");
            return;
        }
        SERIAL_HILOGI(MODULE_SERVICE, "paramsJson=%{public}s", BuildParamsJson().c_str());
        MessageParcel data;
        MessageParcel reply;
        MessageOption option;
        data.WriteInt32(MESSAGE_PARCEL_KEY_SIZE);
        data.WriteString16(u"bundleName");
        data.WriteString16(u"com.usb.right");
        data.WriteString16(u"abilityName");
        data.WriteString16(u"UsbServiceExtAbility");
        data.WriteString16(u"parameters");
        data.WriteString16(Str8ToStr16(BuildParamsJson()));
        int32_t ret = remoteObject->SendRequest(CMD_CODE_SHOW_DIALOG, data, reply, option);
        SERIAL_HILOGI(MODULE_SERVICE, "SendRequest ret=%{public}d", ret);
    }

    void OnAbilityDisconnectDone(const AppExecFwk::ElementName& element, int32_t resultCode) override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "OnAbilityDisconnectDone");
        sem_post(&g_dialogSem);
    }

private:
    std::string BuildParamsJson()
    {
        std::string json = "{";
        bool first = true;
        std::lock_guard<std::mutex> lock(g_dialogParamsMutex);
        for (const auto& [key, value] : g_dialogParams) {
            if (!first) {
                json += ",";
            }
            json += "\"" + key + "\":\"" + value + "\"";
            first = false;
        }
        json += "}";
        return json;
    }
};

static bool ShowAuthDialog(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "ShowAuthDialog: appName=%{public}s, portName=%{public}s, tokenId=%{public}s, deviceId=%{public}s",
        HARDCODED_APP_NAME, portName.c_str(), HARDCODED_TOKEN_ID, HARDCODED_DEVICE_ID);

    {
        std::lock_guard<std::mutex> lock(g_dialogParamsMutex);
        g_dialogParams.clear();
        g_dialogParams["appName"] = HARDCODED_APP_NAME;
        g_dialogParams["portName"] = portName;
        g_dialogParams["tokenId"] = HARDCODED_TOKEN_ID;
        g_dialogParams["deviceId"] = HARDCODED_DEVICE_ID;
        g_dialogParams["ability.want.params.uiExtensionType"] = "sysDialog/common";
    }

    sem_init(&g_dialogSem, 1, 0);
    auto abmc = AAFwk::AbilityManagerClient::GetInstance();
    if (abmc == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "AbilityManagerClient null");
        return false;
    }

    sptr<SerialDialogConn> conn = new SerialDialogConn();
    AAFwk::Want want;
    want.SetElementName("com.ohos.sceneboard", "com.ohos.sceneboard.systemdialog");
    int32_t ret = abmc->ConnectAbility(want, conn, HARDCODED_USER_ID);
    SERIAL_HILOGI(MODULE_SERVICE, "ConnectAbility sceneboard ret=%{public}d", ret);
    if (ret != ERR_CODE_OK) {
        want.SetElementName("com.ohos.systemui", "com.ohos.systemui.dialog");
        ret = abmc->ConnectAbility(want, conn, HARDCODED_USER_ID);
        SERIAL_HILOGI(MODULE_SERVICE, "ConnectAbility systemui ret=%{public}d", ret);
        if (ret != ERR_CODE_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "ConnectAbility failed: %{public}d", ret);
            return false;
        }
    }

    sem_wait(&g_dialogSem);
    SERIAL_HILOGI(MODULE_SERVICE, "Dialog closed");
    return true;
}

static std::mutex g_serviceMutex;
static std::set<std::string> g_openedPorts;

REGISTER_SYSTEM_ABILITY_BY_ID(SerialService, SERIAL_SYSTEM_ABILITY_ID, true);

SerialService::SerialService(int32_t systemAbilityId, bool runOnCreate)
    : SystemAbility(systemAbilityId, runOnCreate), unloadTimer_("SerialServiceUnload")
{
}

SerialService::~SerialService() {}

void SerialService::OnStart(const OHOS::SystemAbilityOnDemandReason& startReason)
{
    if (registerToService_) {
        return;
    }
    if (!Publish(this)) {
        SERIAL_HILOGE(MODULE_SERVICE, "publish failed");
        return;
    }
    registerToService_ = true;
    SERIAL_HILOGI(MODULE_SERVICE, "OnStart success (auth dialog test)");
}

void SerialService::OnStop()
{
    registerToService_ = false;
}

int32_t SerialService::OnIdle(const SystemAbilityOnDemandReason& idleReason)
{
    return 0;
}

int SerialService::Dump(int fd, const std::vector<std::u16string>& args)
{
    return 0;
}

int32_t SerialService::OnRemoteRequest(uint32_t code, MessageParcel& data, MessageParcel& reply, MessageOption& option)
{
    return SerialServerStub::OnRemoteRequest(code, data, reply, option);
}

int32_t SerialService::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices called");
    SerialPortInfo dev0;
    dev0.portName = "/dev/ttyS0";
    dev0.productId = DEV0_PRODUCT_ID;
    dev0.vendorId = DEV0_VENDOR_ID;
    dev0.manufacturer = "MfgA";

    SerialPortInfo dev1;
    dev1.portName = "/dev/ttyS1";
    dev1.productId = DEV1_PRODUCT_ID;
    dev1.vendorId = DEV1_VENDOR_ID;
    dev1.manufacturer = "MfgB";

    devices.push_back(dev0);
    devices.push_back(dev1);

    SERIAL_HILOGI(MODULE_SERVICE,
        "QuerySerialDevices return 2 devices: /dev/ttyS0(pid=%{public}d,vid=%{public}d), "
        "/dev/ttyS1(pid=%{public}d,vid=%{public}d)",
        DEV0_PRODUCT_ID, DEV0_VENDOR_ID, DEV1_PRODUCT_ID, DEV1_VENDOR_ID);
    return SERIAL_OK;
}

int32_t SerialService::OpenSerial(
    const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token)
{
    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial portName=%{public}s", portInfo.portName.c_str());

    if (token == nullptr) {
        return SERIAL_ERR_SERVICE;
    }

    {
        std::lock_guard<std::mutex> lock(g_serviceMutex);
        if (g_openedPorts.count(portInfo.portName)) {
            return SERIAL_ERR_DEVICE_BUSY;
        }
    }

    if (!ShowAuthDialog(portInfo.portName)) {
        return SERIAL_ERR_PERMISSION_DENIED;
    }

    std::lock_guard<std::mutex> lock(g_serviceMutex);
    g_openedPorts.insert(portInfo.portName);
    return SERIAL_OK;
}

int32_t SerialService::CloseSerial(const std::string& portName)
{
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    g_openedPorts.erase(portName);
    return SERIAL_OK;
}

int32_t SerialService::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    if (!g_openedPorts.count(portName)) {
        bytesWritten = 0;
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }
    bytesWritten = static_cast<int32_t>(data.size());
    return SERIAL_OK;
}

int32_t SerialService::StartRead(const std::string& portName)
{
    return SERIAL_OK;
}

int32_t SerialService::StopRead(const std::string& portName)
{
    return SERIAL_OK;
}

int32_t SerialService::Flush(const std::string& portName)
{
    return SERIAL_OK;
}

int32_t SerialService::Drain(const std::string& portName)
{
    return SERIAL_OK;
}

int32_t SerialService::SendBrkSignal(const std::string& portName)
{
    return SERIAL_OK;
}

int32_t SerialService::SetRtsSignal(const std::string& portName, bool rts)
{
    return SERIAL_OK;
}

int32_t SerialService::GetCtsSignal(const std::string& portName, bool& cts)
{
    cts = false;
    return SERIAL_OK;
}

int32_t SerialService::SetDtrSignal(const std::string& portName, bool dtr)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SetDtrSignal called, portName=%{public}s, dtr=%{public}d", portName.c_str(), dtr);
    return SERIAL_OK;
}

int32_t SerialService::GetDsrSignal(const std::string& portName, bool& dsr)
{
    SERIAL_HILOGI(MODULE_SERVICE, "GetDsrSignal called, portName=%{public}s", portName.c_str());
    dsr = false;
    return SERIAL_OK;
}

int32_t SerialService::AddPortAuthorization(const std::string& tokenId, const std::string& deviceId)
{
    SERIAL_HILOGI(MODULE_SERVICE, "AddPortAuthorization tokenId=%{public}s deviceId=%{public}s", tokenId.c_str(),
        deviceId.c_str());
    return SERIAL_OK;
}

} // namespace Serial
} // namespace OHOS
