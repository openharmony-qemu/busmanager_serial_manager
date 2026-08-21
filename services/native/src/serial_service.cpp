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

#include <cinttypes>
#include <iomanip>
#include <sstream>

#include "accesstoken_kit.h"
#include "bundle_mgr_interface.h"
#include "config_policy_utils.h"
#include "common_timer_errors.h"
#include "hdf_types.h"
#include "hilog_wrapper.h"
#include "if_system_ability_manager.h"
#include "ipc_skeleton.h"
#include "iservice_registry.h"
#include "os_account_manager.h"
#include "parameter.h"
#include "parameters.h"
#include "serial_error_code.h"
#include "serial_right_manager.h"
#include "serial_service.h"
#include "system_ability_definition.h"
#include "system_ability_ondemand_reason.h"
#include "tokenid_kit.h"
#include "xcollie/ipc_full.h"
#include "xcollie/xcollie_define.h"

using namespace OHOS::Security::AccessToken;
using namespace OHOS::HDI::Serial::V1_0;

namespace OHOS {
namespace Serial {
constexpr const char* DEV_PREFIX = "/dev/";
constexpr size_t DEV_PREFIX_LEN = 5;
constexpr size_t WRITE_DATA_MAX_SIZE = 4096;
constexpr int32_t WRITE_TIMEOUT_MAX_MS = 300000;
constexpr int32_t PARITY_MIN = 0;
constexpr int32_t PARITY_MAX = 4;
constexpr int32_t DATA_BITS_MIN = 5;
constexpr int32_t DATA_BITS_MAX = 8;
constexpr int32_t STOP_BITS_MAX = 2;

static int32_t SerialHdiToInternalCode(int32_t hdiCode)
{
    switch (hdiCode) {
        case HDF_SUCCESS:
            return SERIAL_OK;
        case HDF_FAILURE:
        case HDF_ERR_NOT_SUPPORT:
        case HDF_ERR_INVALID_OBJECT:
        case HDF_ERR_MALLOC_FAIL:
        case HDF_ERR_THREAD_CREATE_FAIL:
        case HDF_ERR_QUEUE_FULL:
        case HDF_ERR_OUT_OF_RANGE:
            return SERIAL_ERR_SERVICE;
        case HDF_ERR_IO:
        case HDF_DEV_ERR_NO_DEVICE:
            return SERIAL_ERR_DEVICE_DISCONNECTED;
        case HDF_ERR_INVALID_PARAM:
            return SERIAL_ERR_INVALID_PARAM;
        case HDF_ERR_TIMEOUT:
            return SERIAL_ERR_TIMEOUT;
        case HDF_ERR_DEVICE_BUSY:
            return SERIAL_ERR_DEVICE_BUSY;
        case HDF_ERR_BAD_FD:
            return SERIAL_ERR_DEVICE_NOT_OPENED;
        case HDF_ERR_NOPERM:
            return SERIAL_ERR_PERMISSION_DENIED;
        default:
            return SERIAL_ERR_SERVICE;
    }
}

constexpr uint8_t ASCII_PRINTABLE_MIN = 32;
constexpr uint8_t ASCII_PRINTABLE_MAX = 126;
constexpr int HEX_WIDTH = 2;
constexpr const char* RS232_MDM_PARAM = "persist.edm.rs232_serial_disable";
constexpr const char* PORT_ID_CHARS = "0123456789";
constexpr const char* PCIE_SERIAL_CONFIG_PATH = "etc/serial/pcie_serial_config.json";
sptr<SerialService> g_serviceInstance = nullptr;

REGISTER_SYSTEM_ABILITY_BY_ID(SerialService, SERIAL_SYSTEM_ABILITY_ID, true);

static int32_t GetIdFromPortName(const std::string& portName)
{
    int32_t portId = 0;
    auto pos = portName.find_last_not_of(PORT_ID_CHARS);
    if (pos != std::string::npos) {
        auto ret = std::from_chars(portName.data() + pos + 1, portName.data() + portName.size(), portId);
        if (ret.ec == std::errc()) {
            return portId;
        }
    }
    SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:convert portName %{public}s failed.", __func__, portName.c_str());
    return SERIAL_ERR_INVALID_PARAM;
}

sptr<SerialService> SerialService::GetGlobalInstance()
{
    return g_serviceInstance;
}

SerialClientDeathRecipient::SerialClientDeathRecipient(const std::string& portName, SerialService* service)
    : portName_(portName), service_(service)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialClientDeathRecipient created for port %{public}s",
        __func__, __LINE__, portName.c_str());
}

void SerialClientDeathRecipient::OnRemoteDied(const wptr<IRemoteObject>& object)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Client died for port %{public}s, cleaning up resources",
        __func__, __LINE__, portName_.c_str());
    if (service_ != nullptr) {
        service_->OnClientDied(portName_);
    }
}

SerialHdiCallback::SerialHdiCallback(const sptr<ISerialServiceCallback>& serviceCallback, const std::string& deviceId)
    : serviceCallback_(serviceCallback), portName_(deviceId)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialHdiCallback created for port %{public}s", __func__,
        __LINE__, deviceId.c_str());
}

int32_t SerialHdiCallback::OnReadData(const std::vector<int8_t>& data, uint32_t dataLen)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d SerialHdiCallback::OnReadData, port=%{public}s, dataLen=%{public}u", __func__, __LINE__,
        portName_.c_str(), dataLen);
    if (serviceCallback_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d serviceCallback is null", __func__, __LINE__);
        return SERIAL_ERR_INVALID_PARAM;
    }

    // Convert int8_t to uint8_t and forward data to service callback
    std::vector<uint8_t> dataCopy;
    size_t actualLen = std::min(static_cast<size_t>(dataLen), data.size());
    for (size_t i = 0; i < actualLen; ++i) {
        dataCopy.push_back(static_cast<uint8_t>(data[i]));
    }
    serviceCallback_->onDataRead(portName_, dataCopy);
    return SERIAL_OK;
}

int32_t SerialHdiCallback::OnDeviceOffline()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialHdiCallback::OnDeviceOffline, port=%{public}s", __func__,
        __LINE__, portName_.c_str());

    auto service = SerialService::GetGlobalInstance();
    if (service != nullptr) {
        service->HandleDeviceOffline(portName_);
    }

    // 设备离线时不清理授权记录（缓存模式：授权在本次启动周期有效，重启清空）
    // 设备重新插入时，如果 VID/PID 相同但 portName 变化，需要重新授权
    return SERIAL_OK;
}

SerialService::SerialService(int32_t systemAbilityId, bool runOnCreate)
    : SystemAbility(systemAbilityId, runOnCreate), unloadTimer_("SerialServiceUnload")
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d SerialService constructed, systemAbilityId=%{public}d, runOnCreate=%{public}d", __func__,
        __LINE__, systemAbilityId, runOnCreate);
}

SerialService::~SerialService()
{
    CancelUnloadSa();
    unloadTimer_.Shutdown();
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialService destroyed", __func__, __LINE__);
}

void SerialService::OnStart(const OHOS::SystemAbilityOnDemandReason& startReason)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialService OnStart, reason=%{public}d", __func__, __LINE__,
        startReason.GetId());
    if (registerToService_) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d SerialService already started", __func__, __LINE__);
        return;
    }
    g_serviceInstance = this;

    if (OHOS::system::GetParameter(RS232_MDM_PARAM, "") == "" || startReason.GetId() == OnDemandReasonId::PARAM) {
        // 1. not set before; 2. SA launched due to the param;
        this->reportManageFlag_ = true;
    }
    // Load & watch enterprise policy
    auto watchRet = WatchParameter(
        RS232_MDM_PARAM,
        [](const char* key, const char* value, void* context) {
            if (key == nullptr || value == nullptr) {
                SERIAL_HILOGE(MODULE_SERVICE, "param null pointer");
                return;
            }
            SERIAL_HILOGI(MODULE_SERVICE, "watch serial param: get %{public}s=%{public}s", key, value);
            bool disable = (std::string(value) == "1");
            if (g_serviceInstance) {
                g_serviceInstance->SetSerialManagePolicy(disable);
                g_serviceInstance->ReportSerialManageAuditEvent("RS232", disable);
            } else {
                SERIAL_HILOGE(MODULE_SERVICE, "%{public}s parameter watch callback failed.", RS232_MDM_PARAM);
            }
        },
        this);
    if (watchRet != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "failed to watch %{public}s", RS232_MDM_PARAM);
    }

    // Initialize HDI proxy
    InitHdiProxy();

    // Load PCIE serial port configuration from CCM
    LoadPcieSerialConfig();

    // Initialize permission manager
    SerialRightManager::GetInstance().Init();

    // Add IPC full monitoring to detect binder buffer overflow
    using namespace OHOS::HiviewDFX;
    constexpr uint64_t IPC_FULL_CHECK_INTERVAL = 30; // 30 seconds interval
    if (!IpcFull::GetInstance().AddIpcFull(IPC_FULL_CHECK_INTERVAL, XCOLLIE_FLAG_DEFAULT)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d AddIpcFull failed", __func__, __LINE__);
    } else {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d AddIpcFull success, interval=%{public}" PRIu64 "s",
            __func__, __LINE__, IPC_FULL_CHECK_INTERVAL);
    }

    // Set up unload timer (scheduled when all devices are closed)
    if (unloadTimer_.Setup() != Utils::TIMER_ERR_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Unload timer setup failed", __func__, __LINE__);
    }

    if (!Publish(g_serviceInstance)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d SerialService publish failed", __func__, __LINE__);
        return;
    }

    registerToService_ = true;
    ScheduleUnloadSa();
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialService OnStart success", __func__, __LINE__);
}

void SerialService::OnStop()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialService OnStop", __func__, __LINE__);

    // Step 0: Save authorization cache to database before SA stops
    SerialRightManager::GetInstance().OnStop();

    // Step 1: Cancel unload timer (safe operation, no dependencies)
    CancelUnloadSa();
    unloadTimer_.Shutdown();

    // Step 2: Lock mutex before closing all opened devices
    std::lock_guard<std::mutex> lock(serviceMutex_);

    // Iterate through all device contexts and clean up each one
    for (auto& pair : deviceContexts_) {
        // Defensive check: skip empty DeviceContext entries
        if (pair.second == nullptr) {
            SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d DeviceContext is nullptr, skip", __func__, __LINE__);
            continue;
        }

        // Step 3a: HDI device cleanup
        // Intentionally skip: hdiDevice->Close()
        // Reason: During system shutdown, HDI service may stop before this service.
        //         Calling Close() on a stopped HDI proxy causes segfault.
        //         HDI service handles cleanup in its OnStop lifecycle.
        if (pair.second->hdiDevice != nullptr) {
            SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Closing device %{public}s", __func__, __LINE__,
                pair.second->portName.c_str());
            // Skip HDI Close() to prevent potential segfault
            // HDI resources will be cleaned up by HDI service itself
        }

        // Step 3b: Remove death recipient (with null safety check)
        // Must check: deathRecipient != nullptr && serviceCallback != nullptr
        if (pair.second->deathRecipient != nullptr && pair.second->serviceCallback != nullptr) {
            // AsObject() may return nullptr (if client process already died)
            auto remoteObj = pair.second->serviceCallback->AsObject();
            if (remoteObj != nullptr) {
                remoteObj->RemoveDeathRecipient(pair.second->deathRecipient);
                SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Death recipient removed for port %{public}s",
                    __func__, __LINE__, pair.second->portName.c_str());
            } else {
                // Client remote object already gone, log warning
                SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d AsObject() returned nullptr for port %{public}s",
                    __func__, __LINE__, pair.second->portName.c_str());
            }
        }
    }

    // Step 4: Clear all maps
    deviceContexts_.clear();
    productIdVendorIdToPortName_.clear();

    // Step 5: Reset proxy and flags
    serialsProxy_ = nullptr;
    registerToService_ = false;
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialService OnStop success", __func__, __LINE__);
}

void SerialService::InitHdiProxy()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d InitHdiProxy: getting ISerials proxy", __func__, __LINE__);
    serialsProxy_ = ISerials::Get(true);
    if (serialsProxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d Failed to get ISerials HDI proxy, using stub mode as placeholder service", __func__,
            __LINE__);
    } else {
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d ISerials HDI proxy obtained successfully", __func__, __LINE__);
    }
}

void SerialService::LoadPcieSerialConfig()
{
    // ttyS is Linux standard serial port - always present as PCIE serial with offset 2000
    pcieSerialOffsets_["ttyS"] = TTYS_PORT_ID_OFFSET;

    char buf[MAX_PATH_LEN] = {0};
    char* path = GetOneCfgFile(PCIE_SERIAL_CONFIG_PATH, buf, MAX_PATH_LEN);
    if (path == nullptr || *path == '\0') {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d GetOneCfgFile failed for %{public}s",
                __func__, __LINE__, PCIE_SERIAL_CONFIG_PATH);
            return;
    }

    nlohmann::json root = nlohmann::json::parse(ifsConf, nullptr, false);
    if (root.is_discarded() || !root.contains("pcieSerialPorts")) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid pcie serial config format", __func__, __LINE__);
        return;
    }

    for (const auto& item : root["pcieSerialPorts"]) {
        if (!item.contains("prefix") || !item.contains("portIdOffset")) {
            SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d Invalid pcie serial entry, skipping",
                __func__, __LINE__);
            continue;
        }
        if (!item["prefix"].is_string() || !item["portIdOffset"].is_number_integer()) {
            SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d Invalid pcie serial entry type, skipping",
                __func__, __LINE__);
            continue;
        }
        std::string prefix = item["prefix"].get<std::string>();
        int32_t offset = item["portIdOffset"].get<int32_t>();
        pcieSerialOffsets_[prefix] = offset;
    }
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Loaded %{public}zu pcie serial port configs from %{public}s",
        __func__, __LINE__, pcieSerialOffsets_.size(), path);
}

OHOS::HDI::Serial::V1_0::SerialConfig SerialService::ConvertConfig(const SerialConfig& serviceConfig)
{
    OHOS::HDI::Serial::V1_0::SerialConfig hdiConfig;
    hdiConfig.baudRate = serviceConfig.baudRate;
    hdiConfig.dataBits = serviceConfig.dataBits;
    hdiConfig.stopBits = serviceConfig.stopBits;
    hdiConfig.parity = serviceConfig.parity;
    hdiConfig.rtscts = serviceConfig.rtscts;
    hdiConfig.xon = serviceConfig.xon;
    hdiConfig.xoff = serviceConfig.xoff;
    hdiConfig.xany = serviceConfig.xany;
    return hdiConfig;
}

SerialPortInfo SerialService::ConvertDeviceInfo(const SerialDeviceInfo& hdiInfo)
{
    SerialPortInfo serviceInfo;
    std::string portName = hdiInfo.portName;
    if (portName.rfind(DEV_PREFIX, 0) == 0) {
        portName = portName.substr(DEV_PREFIX_LEN);
    }
    serviceInfo.portName = portName;
    serviceInfo.productId = hdiInfo.productId;
    serviceInfo.vendorId = hdiInfo.vendorId;
    serviceInfo.manufacturer = hdiInfo.manufacturer;
    return serviceInfo;
}

DeviceContext* SerialService::FindDeviceContext(const std::string& portName)
{
    auto it = deviceContexts_.find(portName);
    if (it != deviceContexts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

int32_t SerialService::CheckPortAccessPermission(const DeviceContext* context, const std::string& portName)
{
    if (context == nullptr) {
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    std::string callerTokenId = GetCallingTokenId();
    if (context->tokenId != callerTokenId) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d Not allowed to operate port %{public}s, "
            "callerTokenId=%{public}s, ownerTokenId=%{public}s",
            __func__, __LINE__, portName.c_str(), callerTokenId.c_str(), context->tokenId.c_str());
        return SERIAL_ERR_NOT_ALLOWED;
    }
    return SERIAL_OK;
}

int32_t SerialService::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d QuerySerialDevices called", __func__, __LINE__);

    if (serialsProxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d HDI proxy not initialized", __func__, __LINE__);
        return SERIAL_ERR_SERVICE;
    }

    std::vector<SerialDeviceInfo> hdiDevices;
    int32_t ret = serialsProxy_->QueryDevices(hdiDevices);
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI QueryDevices failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    // Convert HDI device info to service layer device info
    devices.clear();
    for (const auto& hdiDevice : hdiDevices) {
        if (isRs232Disabled_ && !IsUsbSerial(hdiDevice.portName)) {
            continue;
        }
        devices.push_back(ConvertDeviceInfo(hdiDevice));
        // Store productId/vendorId mapping for device identification (without /dev/)
        std::string portName = hdiDevice.portName;
        if (portName.rfind(DEV_PREFIX, 0) == 0) {
            portName = portName.substr(DEV_PREFIX_LEN);
        }
        productIdVendorIdToPortName_[std::make_pair(hdiDevice.productId, hdiDevice.vendorId)] = portName;
    }
    if (isRs232Disabled_ && hdiDevices.size() > 0 && devices.size() == 0) {
        return SERIAL_ERR_ENTERPRISE_POLICY;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d QuerySerialDevices found %{public}zu devices", __func__,
        __LINE__, devices.size());

    for (const auto& device : devices) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "SerialPort: portName=%{public}s, vendorId=0x%{public}04x, productId=0x%{public}04x, "
            "manufacturer=%{public}s",
            device.portName.c_str(), device.vendorId, device.productId, device.manufacturer.c_str());
    }

    return SERIAL_OK;
}

std::string SerialService::FindPortNameForDevice(const SerialPortInfo& portInfo)
{
    // Strategy 1: Find by portName (most precise) - return directly
    if (!portInfo.portName.empty()) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Found portName directly: %{public}s", __func__, __LINE__,
            portInfo.portName.c_str());
        return portInfo.portName;
    }

    // Strategy 2: Find by productId + vendorId (for device identification)
    auto itPv = productIdVendorIdToPortName_.find(std::make_pair(portInfo.productId, portInfo.vendorId));
    if (itPv != productIdVendorIdToPortName_.end()) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d Found portName by productId/vendorId: %{public}d/%{public}d -> %{public}s", __func__,
            __LINE__, portInfo.productId, portInfo.vendorId, itPv->second.c_str());
        return itPv->second;
    }

    SERIAL_HILOGW(MODULE_SERVICE,
        "%{public}s:%{public}d Cannot find portName for device (productId=%{public}d, vendorId=%{public}d)", __func__,
        __LINE__, portInfo.productId, portInfo.vendorId);
    return "";
}

int32_t SerialService::ValidateOpenSerialParams(
    const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token)
{
    if (serialsProxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d HDI proxy not initialized", __func__, __LINE__);
        return SERIAL_ERR_SERVICE;
    }

    if (config.baudRate <= 0) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid baudRate=%{public}d",
            __func__, __LINE__, config.baudRate);
        return SERIAL_ERR_INVALID_PARAM;
    }
    if (config.dataBits < DATA_BITS_MIN || config.dataBits > DATA_BITS_MAX) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid dataBits=%{public}d",
            __func__, __LINE__, config.dataBits);
        return SERIAL_ERR_INVALID_PARAM;
    }
    if (config.stopBits < 1 || config.stopBits > STOP_BITS_MAX) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid stopBits=%{public}d",
            __func__, __LINE__, config.stopBits);
        return SERIAL_ERR_INVALID_PARAM;
    }
    if (config.parity < PARITY_MIN || config.parity > PARITY_MAX) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid parity=%{public}d",
            __func__, __LINE__, config.parity);
        return SERIAL_ERR_INVALID_PARAM;
    }

    if (token == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid callback token", __func__, __LINE__);
        return SERIAL_ERR_INVALID_PARAM;
    }
    return SERIAL_OK;
}

int32_t SerialService::OpenHdiDevice(const std::string& portName, const SerialConfig& config,
    const sptr<ISerialServiceCallback>& token, sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& hdiDevice,
    sptr<SerialHdiCallback>& hdiCallback)
{
    OHOS::HDI::Serial::V1_0::SerialConfig hdiConfig = ConvertConfig(config);
    if (serialsProxy_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d HDI proxy not initialized", __func__, __LINE__);
        return SERIAL_ERR_SERVICE;
    }
    int32_t ret = serialsProxy_->OpenDevice(portName, hdiConfig, hdiCallback, hdiDevice);
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI OpenDevice failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }
    if (hdiDevice == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d HDI returned null device", __func__, __LINE__);
        return SERIAL_ERR_SERVICE;
    }
    return SERIAL_OK;
}

std::shared_ptr<DeviceContext> SerialService::BuildDeviceContext(const std::string& portName,
    const SerialConfig& config, const sptr<ISerialServiceCallback>& token,
    const sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& hdiDevice, const sptr<SerialHdiCallback>& hdiCallback)
{
    auto context = std::make_shared<DeviceContext>();
    context->hdiDevice = hdiDevice;
    context->hdiCallback = hdiCallback;
    context->serviceCallback = token;
    context->portName = portName;
    context->bundleName = GetCallingBundleName();
    context->tokenId = GetCallingTokenId();
    context->userId = GetCallingUserId();
    context->openTime = std::chrono::steady_clock::now();
    context->config = config;

    sptr<SerialClientDeathRecipient> deathRecipient = new SerialClientDeathRecipient(portName, this);
    auto tokenObject = token->AsObject();
    if (tokenObject == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE,
            "%{public}s:%{public}d token->AsObject() is null, skip death recipient registration for port %{public}s",
            __func__, __LINE__, portName.c_str());
    } else if (tokenObject->AddDeathRecipient(deathRecipient)) {
        context->deathRecipient = deathRecipient;
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Death recipient registered for port %{public}s", __func__,
            __LINE__, portName.c_str());
    } else {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d Failed to register death recipient for port %{public}s",
            __func__, __LINE__, portName.c_str());
    }
    return context;
}

int32_t SerialService::OpenSerial(
    const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d OpenSerial called, portName=%{public}s, productId=%{public}d, vendorId=%{public}d, "
        "manufacturer=%{public}s, baudRate=%{public}d, dataBits=%{public}d, stopBits=%{public}d, "
        "parity=%{public}d, rtscts=%{public}d, xon=%{public}d, xoff=%{public}d, xany=%{public}d, ",
        __func__, __LINE__, portInfo.portName.c_str(), portInfo.productId, portInfo.vendorId,
        portInfo.manufacturer.c_str(), config.baudRate, config.dataBits, config.stopBits, config.parity, config.rtscts,
        config.xon, config.xoff, config.xany);
    ReportSerialOperationAudit(portInfo.portName, SERIAL_OPEN);
    if (isRs232Disabled_ && !IsUsbSerial(portInfo.portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    int32_t paramRet = ValidateOpenSerialParams(portInfo, config, token);
    if (paramRet != SERIAL_OK) {
        return paramRet;
    }

    std::lock_guard<std::mutex> lock(serviceMutex_);

    if (unloadTimerId_ != 0) {
        unloadTimer_.Unregister(unloadTimerId_);
        unloadTimerId_ = 0;
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d Cancelled unload timer on device open", __func__, __LINE__);
    }

    std::string portName = FindPortNameForDevice(portInfo);
    if (portName.empty()) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Cannot find device %{public}s", __func__, __LINE__,
            portInfo.portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    std::vector<SerialPortInfo> currentDevices;
    int32_t queryRet = QuerySerialDevices(currentDevices);
    if (queryRet != SERIAL_OK) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d QuerySerialDevices failed before open, ret=%{public}d",
            __func__, __LINE__, queryRet);
    } else {
        bool portFound = false;
        for (const auto& dev : currentDevices) {
            if (dev.portName == portName && dev.vendorId == portInfo.vendorId && dev.productId == portInfo.productId &&
                dev.manufacturer == portInfo.manufacturer) {
                portFound = true;
                break;
            }
        }
        if (!portFound) {
            SERIAL_HILOGE(MODULE_SERVICE,
                "%{public}s:%{public}d Port %{public}s not found or mismatch "
                "(expected: vid=0x%{public}04x, pid=0x%{public}04x, manufacturer=%{public}s)",
                __func__, __LINE__, portName.c_str(), portInfo.vendorId, portInfo.productId,
                portInfo.manufacturer.c_str());
            return SERIAL_ERR_DEVICE_DISCONNECTED;
        }
    }

    int32_t rightRet = RequestSerialPortRight(portInfo);
    if (rightRet != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Permission denied for port %{public}s, ret=%{public}d",
            __func__, __LINE__, portInfo.portName.c_str(), rightRet);
        return rightRet;
    }

    if (deviceContexts_.find(portName) != deviceContexts_.end()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s already opened", __func__, __LINE__,
            portName.c_str());
        return SERIAL_ERR_DEVICE_BUSY;
    }

    std::string hdiPortName = DEV_PREFIX + portName;
    sptr<OHOS::HDI::Serial::V1_0::ISerialDevice> hdiDevice;
    sptr<SerialHdiCallback> hdiCallback = new SerialHdiCallback(token, portName);
    int32_t hdiRet = OpenHdiDevice(hdiPortName, config, token, hdiDevice, hdiCallback);
    if (hdiRet != SERIAL_OK) {
        return hdiRet;
    }

    auto context = BuildDeviceContext(portName, config, token, hdiDevice, hdiCallback);
    deviceContexts_[portName] = context;

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d OpenSerial success, portName=%{public}s, bundleName=%{public}s", __func__, __LINE__,
        portName.c_str(), context->bundleName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::CloseSerial(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d CloseSerial called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    ReportSerialOperationAudit(portName, SERIAL_CLOSE);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }
    ResetFirstRead(portName);
    ResetFirstWrite(portName);

    std::lock_guard<std::mutex> lock(serviceMutex_);

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Remove death recipient before closing
    if (context->deathRecipient != nullptr && context->serviceCallback != nullptr) {
        auto callbackObject = context->serviceCallback->AsObject();
        if (callbackObject != nullptr) {
            callbackObject->RemoveDeathRecipient(context->deathRecipient);
            SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Death recipient removed for port %{public}s", __func__,
                __LINE__, portName.c_str());
        } else {
            SERIAL_HILOGW(MODULE_SERVICE,
                "%{public}s:%{public}d serviceCallback->AsObject() is null for port %{public}s", __func__, __LINE__,
                portName.c_str());
        }
    }

    if (context->hdiDevice != nullptr) {
        std::string hdiPortName = DEV_PREFIX + portName;
        int32_t ret = context->hdiDevice->Close();
        if (ret != 0) {
            SERIAL_HILOGW(
                MODULE_SERVICE, "%{public}s:%{public}d HDI Close failed, ret=%{public}d", __func__, __LINE__, ret);
        }
    }

    deviceContexts_.erase(portName);

    // Schedule unload timer if no devices are opened
    if (deviceContexts_.empty()) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d No devices open, scheduling UnloadSystemAbility", __func__,
            __LINE__);
        // First cancel existing timer
        if (unloadTimerId_ != 0) {
            unloadTimer_.Unregister(unloadTimerId_);
            unloadTimerId_ = 0;
        }
        auto task = [this]() {
            DoUnloadSa();
        };
        unloadTimerId_ = unloadTimer_.Register(task, UNLOAD_SA_DELAY_MS, true);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d CloseSerial success, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    return SERIAL_OK;
}

void SerialService::OnClientDied(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnClientDied: client process died for port %{public}s",
        __func__, __LINE__, portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s already cleaned up", __func__, __LINE__,
            portName.c_str());
        return;
    }

    // Log client info
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d Cleaning up device %{public}s for bundle %{public}s, tokenId=%{public}s, "
        "userId=%{public}d",
        __func__, __LINE__, portName.c_str(), context->bundleName.c_str(), context->tokenId.c_str(), context->userId);

    // Close HDI device
    if (context->hdiDevice != nullptr) {
        int32_t ret = context->hdiDevice->Close();
        if (ret != 0) {
            SERIAL_HILOGW(MODULE_SERVICE,
                "%{public}s:%{public}d HDI Close failed during client death cleanup, ret=%{public}d", __func__,
                __LINE__, ret);
        }
    }

    // Remove device context from map
    deviceContexts_.erase(portName);

    // Schedule unload timer if no devices are opened
    if (deviceContexts_.empty()) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d No devices after client death, scheduling UnloadSystemAbility", __func__, __LINE__);
        if (unloadTimerId_ != 0) {
            unloadTimer_.Unregister(unloadTimerId_);
            unloadTimerId_ = 0;
        }
        auto task = [this]() {
            DoUnloadSa();
        };
        unloadTimerId_ = unloadTimer_.Register(task, UNLOAD_SA_DELAY_MS, true);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnClientDied cleanup complete for port %{public}s", __func__,
        __LINE__, portName.c_str());
}

void SerialService::HandleDeviceOffline(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d HandleDeviceOffline called, portName=%{public}s", __func__,
        __LINE__, portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);

    auto it = deviceContexts_.find(portName);
    if (it == deviceContexts_.end()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not found in contexts", __func__,
            __LINE__, portName.c_str());
        return;
    }

    if (it->second->serviceCallback != nullptr) {
        it->second->serviceCallback->onDisconnect(portName);
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Notified client of disconnection for port %{public}s",
            __func__, __LINE__, portName.c_str());
    }

    // Skip hdiDevice->Close(): the HDI layer already detected the offline event
    // and will handle its own cleanup. Calling Close() on a physically
    // disconnected device may block indefinitely, keeping serviceMutex_ held
    // and preventing subsequent OpenSerial calls from acquiring the lock.

    if (it->second->deathRecipient != nullptr && it->second->serviceCallback != nullptr) {
        auto callbackObject = it->second->serviceCallback->AsObject();
        if (callbackObject != nullptr) {
            callbackObject->RemoveDeathRecipient(it->second->deathRecipient);
        }
    }

    deviceContexts_.erase(it);

    if (deviceContexts_.empty()) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d No devices after offline, scheduling UnloadSystemAbility",
            __func__, __LINE__);
        if (unloadTimerId_ != 0) {
            unloadTimer_.Unregister(unloadTimerId_);
            unloadTimerId_ = 0;
        }
        auto task = [this]() {
            DoUnloadSa();
        };
        unloadTimerId_ = unloadTimer_.Register(task, UNLOAD_SA_DELAY_MS, true);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s offline cleanup complete", __func__,
        __LINE__, portName.c_str());
}

int32_t SerialService::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d WriteSerial called, portName=%{public}s, dataSize=%{public}zu, timeout=%{public}d",
        __func__, __LINE__, portName.c_str(), data.size(), timeout);
    if (QueryAndRecordFirstWrite(portName)) {
        ReportSerialOperationAudit(portName, SERIAL_WRITE);
    }
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }
    if (data.empty() || data.size() > WRITE_DATA_MAX_SIZE) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid data size=%{public}zu",
            __func__, __LINE__, data.size());
        return SERIAL_ERR_INVALID_PARAM;
    }
    if (timeout < 0 || timeout > WRITE_TIMEOUT_MAX_MS) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid timeout=%{public}d",
            __func__, __LINE__, timeout);
        return SERIAL_ERR_INVALID_PARAM;
    }

    std::lock_guard<std::mutex> lock(serviceMutex_);

    DeviceContext* context = FindDeviceContext(portName);
    int32_t checkRet = CheckWritePreconditions(context, portName);
    if (checkRet != SERIAL_OK) {
        return checkRet;
    }

    LogWriteData(data);

    int32_t ret = context->hdiDevice->Write(data, timeout, bytesWritten);
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI Write failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d WriteSerial success, bytesWritten=%{public}d", __func__,
        __LINE__, bytesWritten);
    return SERIAL_OK;
}

int32_t SerialService::StartRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d StartRead called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    if (QueryAndRecordFirstRead(portName)) {
        ReportSerialOperationAudit(portName, SERIAL_READ);
    }
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    std::lock_guard<std::mutex> lock(serviceMutex_);

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Call HDI StartRead to start async read
    int32_t ret = context->hdiDevice->StartRead();
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI StartRead failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d StartRead success, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::StopRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d StopRead called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Call HDI StopRead to stop async read
    int32_t ret = context->hdiDevice->StopRead();
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI StopRead failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d StopRead success, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::Flush(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Flush called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Call HDI Flush to flush buffer
    int32_t ret = context->hdiDevice->Flush();
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI Flush failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Flush success, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::Drain(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Drain called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Call HDI Drain to wait for data transmission complete
    int32_t ret = context->hdiDevice->Drain();
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI Drain failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Drain success, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::SendBrkSignal(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SendBrkSignal called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Call HDI SendBrkSignal to send break signal
    int32_t ret = context->hdiDevice->SendBrkSignal();
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI SendBrkSignal failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SendBrkSignal success, portName=%{public}s", __func__,
        __LINE__, portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::SetRtsSignal(const std::string& portName, bool rts)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SetRtsSignal called, portName=%{public}s, rts=%{public}d",
        __func__, __LINE__, portName.c_str(), rts);

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Call HDI SetRtsSignal to set RTS signal
    int32_t ret = context->hdiDevice->SetRtsSignal(rts);
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI SetRtsSignal failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SetRtsSignal success, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::GetCtsSignal(const std::string& portName, bool& cts)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetCtsSignal called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    // Call HDI GetCtsSignal to get CTS signal status
    int32_t ret = context->hdiDevice->GetCtsSignal(cts);
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI GetCtsSignal failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetCtsSignal success, portName=%{public}s, cts=%{public}d",
        __func__, __LINE__, portName.c_str(), cts);
    return SERIAL_OK;
}

int32_t SerialService::SetDtrSignal(const std::string& portName, bool dtr)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SetDtrSignal called, portName=%{public}s, dtr=%{public}d",
        __func__, __LINE__, portName.c_str(), dtr);

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    int32_t ret = context->hdiDevice->SetDtrSignal(dtr);
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI SetDtrSignal failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SetDtrSignal success, portName=%{public}s", __func__, __LINE__,
        portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::GetDsrSignal(const std::string& portName, bool& dsr)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetDsrSignal called, portName=%{public}s", __func__, __LINE__,
        portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (isRs232Disabled_ && !IsUsbSerial(portName)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:operation denied by enterprise.", __func__);
        return SERIAL_ERR_SERVICE;
    }

    DeviceContext* context = FindDeviceContext(portName);
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }

    int32_t ret = context->hdiDevice->GetDsrSignal(dsr);
    if (ret != 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d HDI GetDsrSignal failed, ret=%{public}d", __func__, __LINE__, ret);
        return SerialHdiToInternalCode(ret);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetDsrSignal success, portName=%{public}s, dsr=%{public}d",
        __func__, __LINE__, portName.c_str(), dsr);
    return SERIAL_OK;
}
int32_t SerialService::ValidateDialogHapCaller(AccessTokenID callerTokenId, HapTokenInfo& callerTokenInfo)
{
    ATokenTypeEnum tokenType = AccessTokenKit::GetTokenType(callerTokenId);
    if (tokenType != TOKEN_HAP) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d ValidateDialogHapCaller denied: caller is not HAP token, tokenType=%{public}d",
            __func__, __LINE__, static_cast<int32_t>(tokenType));
        return SERIAL_ERR_PERMISSION_DENIED;
    }

    uint64_t fullTokenId = IPCSkeleton::GetCallingFullTokenID();
    bool isSystemApp = TokenIdKit::IsSystemAppByFullTokenID(fullTokenId);
    if (!isSystemApp) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d ValidateDialogHapCaller denied: caller is not system app",
            __func__, __LINE__);
        return SERIAL_ERR_NOT_SYSTEM_APP;
    }

    int ret = AccessTokenKit::GetHapTokenInfo(callerTokenId, callerTokenInfo);
    if (ret != 0) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d ValidateDialogHapCaller denied: GetHapTokenInfo failed, ret=%{public}d", __func__,
            __LINE__, ret);
        return SERIAL_ERR_PERMISSION_DENIED;
    }

    const std::string DIALOG_HAP_BUNDLE_NAME = "com.usb.right";
    if (callerTokenInfo.bundleName != DIALOG_HAP_BUNDLE_NAME) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d ValidateDialogHapCaller denied: wrong bundleName=%{public}s, expected=%{public}s",
            __func__, __LINE__, callerTokenInfo.bundleName.c_str(), DIALOG_HAP_BUNDLE_NAME.c_str());
        return SERIAL_ERR_PERMISSION_DENIED;
    }

    return SERIAL_OK;
}

int32_t SerialService::ParseTargetTokenId(const std::string& tokenId, AccessTokenID& targetTokenId)
{
    char* endPtr = nullptr;
    errno = 0;
    unsigned long long parsedValue = strtoull(tokenId.c_str(), &endPtr, 10);
    if (errno != 0 || endPtr == tokenId.c_str() || *endPtr != '\0') {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d ParseTargetTokenId failed: invalid tokenId=%{public}s",
            __func__, __LINE__, tokenId.c_str());
        return SERIAL_ERR_INVALID_PARAM;
    }
    targetTokenId = static_cast<AccessTokenID>(parsedValue);
    return SERIAL_OK;
}

int32_t SerialService::AddPortAuthorization(const std::string& tokenId, const std::string& deviceId)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d AddPortAuthorization called, tokenId=%{public}s, deviceId=%{public}s", __func__,
        __LINE__, tokenId.c_str(), deviceId.c_str());

    AccessTokenID callerTokenId = IPCSkeleton::GetCallingTokenID();
    HapTokenInfo callerTokenInfo;
    int32_t ret = ValidateDialogHapCaller(callerTokenId, callerTokenInfo);
    if (ret != SERIAL_OK) {
        return ret;
    }

    AccessTokenID targetTokenId = 0;
    ret = ParseTargetTokenId(tokenId, targetTokenId);
    if (ret != SERIAL_OK) {
        return ret;
    }

    HapTokenInfo targetTokenInfo;
    ret = AccessTokenKit::GetHapTokenInfo(targetTokenId, targetTokenInfo);
    if (ret != 0) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d AddPortAuthorization denied: GetHapTokenInfo failed for target tokenId=%{public}s, "
            "ret=%{public}d",
            __func__, __LINE__, tokenId.c_str(), ret);
        return SERIAL_ERR_INVALID_PARAM;
    }

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d AddPortAuthorization validated: caller=%{public}s, target=%{public}s, "
        "userId=%{public}d, tokenId=%{public}s",
        __func__, __LINE__, callerTokenInfo.bundleName.c_str(), targetTokenInfo.bundleName.c_str(),
        targetTokenInfo.userID, tokenId.c_str());

    SerialRightManager& rightManager = SerialRightManager::GetInstance();
    bool result = rightManager.AddDeviceRight(deviceId, targetTokenInfo.bundleName, tokenId, targetTokenInfo.userID);
    if (!result) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d AddDeviceRight failed for deviceId=%{public}s, userId=%{public}d", __func__,
            __LINE__, deviceId.c_str(), targetTokenInfo.userID);
        return SERIAL_ERR_SERVICE;
    }

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d AddPortAuthorization success, port=%{public}s, userId=%{public}d, tokenId=%{public}s, "
        "bundleName=%{public}s",
        __func__, __LINE__, deviceId.c_str(), targetTokenInfo.userID, tokenId.c_str(),
        targetTokenInfo.bundleName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::GetCallingUserId()
{
    int32_t userId = SERIAL_RIGHT_USERID_DEFAULT;
    int32_t ret = AccountSA::OsAccountManager::GetOsAccountLocalIdFromUid(IPCSkeleton::GetCallingUid(), userId);
    if (ret != 0) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d GetOsAccountLocalIdFromUid failed: %{public}d, set to default", __func__, __LINE__,
            ret);
        userId = SERIAL_RIGHT_USERID_DEFAULT;
    }
    return userId;
}

int32_t SerialService::GetCallingUserIdFromToken()
{
    AccessTokenID tokenId = IPCSkeleton::GetCallingTokenID();
    ATokenTypeEnum tokenType = AccessTokenKit::GetTokenTypeFlag(tokenId);

    if (tokenType == TOKEN_HAP) {
        HapTokenInfo hapTokenInfo;
        int32_t ret = AccessTokenKit::GetHapTokenInfo(tokenId, hapTokenInfo);
        if (ret == RET_SUCCESS) {
            SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d HAP token userId=%{public}d", __func__, __LINE__,
                hapTokenInfo.userID);
            return hapTokenInfo.userID;
        }
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d GetHapTokenInfo failed: %{public}d", __func__, __LINE__, ret);
    }

    // For native tokens or failed HAP lookup, fallback to uid-based userId
    return GetCallingUserId();
}

std::string SerialService::GetCallingBundleName()
{
    AccessTokenID tokenId = IPCSkeleton::GetCallingTokenID();
    ATokenTypeEnum tokenType = AccessTokenKit::GetTokenTypeFlag(tokenId);
    if (tokenType == TOKEN_HAP) {
        HapTokenInfo hapTokenInfo;
        int32_t ret = AccessTokenKit::GetHapTokenInfo(tokenId, hapTokenInfo);
        if (ret == RET_SUCCESS) {
            return hapTokenInfo.bundleName;
        }
    } else if (tokenType == TOKEN_NATIVE) {
        NativeTokenInfo nativeTokenInfo;
        int32_t ret = AccessTokenKit::GetNativeTokenInfo(tokenId, nativeTokenInfo);
        if (ret == RET_SUCCESS) {
            return nativeTokenInfo.processName;
        }
    }

    return "";
}

std::string SerialService::GetCallingTokenId()
{
    AccessTokenID tokenId = IPCSkeleton::GetCallingTokenID();
    return std::to_string(tokenId);
}

bool SerialService::CheckSerialPortRight(const std::string& portName, const std::string& productName)
{
    SerialRightManager& rightManager = SerialRightManager::GetInstance();

    // System app or SA bypass permission check
    if (rightManager.IsSystemAppOrSa()) {
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d System app or SA bypass permission check", __func__, __LINE__);
        return true;
    }

    int32_t userId = GetCallingUserIdFromToken();
    std::string bundleName = GetCallingBundleName();
    std::string tokenId = GetCallingTokenId();
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d CheckSerialPortRight: portName=%{public}s, bundleName=%{public}s, userId=%{public}d",
        __func__, __LINE__, portName.c_str(), bundleName.c_str(), userId);

    return rightManager.HasRight(portName, bundleName, tokenId, userId);
}

int32_t SerialService::RequestSerialPortRight(const SerialPortInfo& portInfo)
{
    SerialRightManager& rightManager = SerialRightManager::GetInstance();

    // System app or SA bypass permission request
    if (rightManager.IsSystemAppOrSa()) {
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d System app or SA bypass permission request", __func__, __LINE__);
        return SERIAL_OK;
    }

    int32_t userId = GetCallingUserIdFromToken();
    std::string bundleName = GetCallingBundleName();
    std::string tokenId = GetCallingTokenId();
    std::string productName = portInfo.manufacturer.empty() ? portInfo.portName : portInfo.manufacturer;

    // Find the portName for the device
    std::string portName = FindPortNameForDevice(portInfo);

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d RequestSerialPortRight: portName=%{public}s, "
        "bundleName=%{public}s, userId=%{public}d, tokenId=%{public}s",
        __func__, __LINE__, portName.c_str(), bundleName.c_str(), userId, tokenId.c_str());

    // Build device info for authorization dialog
    SerialDeviceInfoForDialog deviceInfo;
    deviceInfo.portName = portName;
    deviceInfo.bundleName = bundleName;
    deviceInfo.abilityName = "SerialServiceExtAbility";
    deviceInfo.tokenId = tokenId;
    deviceInfo.deviceName = productName;
    deviceInfo.productId = std::to_string(portInfo.productId);
    deviceInfo.vendorId = std::to_string(portInfo.vendorId);
    deviceInfo.serialNumber = "";
    deviceInfo.deviceId = BuildDeviceId(portInfo);
    deviceInfo.portId = 0; // No longer used in new API

    // Request permission - will show dialog if not authorized
    // RequestRight internally calls AddDeviceRight via GetUserAgreementByDiag when authorization is granted
    int32_t ret = rightManager.RequestRight(deviceInfo, userId);
    if (ret != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d RequestRight failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    return SERIAL_OK;
}

int32_t SerialService::OnIdle(const SystemAbilityOnDemandReason& idleReason)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialService OnIdle, reasonId=%{public}d", __func__, __LINE__,
        static_cast<int>(idleReason.GetId()));
    return 0;
}

int32_t SerialService::OnRemoteRequest(uint32_t code, MessageParcel& data, MessageParcel& reply, MessageOption& option)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialService OnRemoteRequest: code=%{public}u", __func__,
        __LINE__, code);
    return SerialServerStub::OnRemoteRequest(code, data, reply, option);
}

uint32_t SerialService::GetDeviceCount()
{
    std::lock_guard<std::mutex> lock(serviceMutex_);
    return static_cast<uint32_t>(deviceContexts_.size());
}

void SerialService::ScheduleUnloadSa()
{
    std::lock_guard<std::mutex> lock(serviceMutex_);

    // Only schedule unload when no devices are opened
    if (deviceContexts_.size() != 0) {
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d Devices still open, skip unload scheduling", __func__, __LINE__);
        return;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Scheduling UnloadSystemAbility after %{public}u ms", __func__,
        __LINE__, UNLOAD_SA_DELAY_MS);

    // First cancel existing timer
    if (unloadTimerId_ != 0) {
        unloadTimer_.Unregister(unloadTimerId_);
        unloadTimerId_ = 0;
    }

    // Register unload task to timer
    auto task = [this]() {
        DoUnloadSa();
    };
    unloadTimerId_ = unloadTimer_.Register(task, UNLOAD_SA_DELAY_MS, true);
}

void SerialService::CancelUnloadSa()
{
    std::lock_guard<std::mutex> lock(serviceMutex_);
    if (unloadTimerId_ != 0) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Cancelling unload timer", __func__, __LINE__);
        unloadTimer_.Unregister(unloadTimerId_);
        unloadTimerId_ = 0;
    }
}

void SerialService::DoUnloadSa()
{
    SERIAL_HILOGI(
        MODULE_SERVICE, "%{public}s:%{public}d Attempting UnloadSystemAbility for serial service", __func__, __LINE__);

    // Check again if any devices are opened (may have been opened during delay)
    if (GetDeviceCount() != 0) {
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d Devices opened during delay, abort unload", __func__, __LINE__);
        return;
    }

    auto samgrProxy = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (samgrProxy == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetSystemAbilityManager failed", __func__, __LINE__);
        return;
    }

    // Check if SA is still in loaded state
    auto saObj = samgrProxy->CheckSystemAbility(SERIAL_SYSTEM_ABILITY_ID);
    if (saObj == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d SA already unloaded", __func__, __LINE__);
        return;
    }

    int32_t ret = samgrProxy->UnloadSystemAbility(SERIAL_SYSTEM_ABILITY_ID);
    if (ret != ERR_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d UnloadSystemAbility failed: %{public}d", __func__, __LINE__, ret);
    } else {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d UnloadSystemAbility success", __func__, __LINE__);
    }
}

std::string SerialService::BuildDeviceId(const SerialPortInfo& portInfo)
{
    std::string deviceId;

    if (portInfo.vendorId != 0) {
        char vendorIdBuf[32];
        (void)snprintf_s(vendorIdBuf, sizeof(vendorIdBuf), sizeof(vendorIdBuf) - 1, "%04x", portInfo.vendorId);
        deviceId += vendorIdBuf;
    }

    if (portInfo.productId != 0) {
        char productIdBuf[32];
        (void)snprintf_s(productIdBuf, sizeof(productIdBuf), sizeof(productIdBuf) - 1, "%04x", portInfo.productId);
        deviceId += "_" + std::string(productIdBuf);
    }

    if (!portInfo.manufacturer.empty()) {
        deviceId += "_" + portInfo.manufacturer;
    }

    deviceId += "_" + portInfo.portName;
    return deviceId;
}

void SerialService::DumpHelp(int fd)
{
    dprintf(fd, "Serial Service Dump Help:\n");
    dprintf(fd, "Usage: hidumper -s SERIAL_SYSTEM_ABILITY_ID [options]\n");
    dprintf(fd, "Options:\n");
    dprintf(fd, "  -h: show this help message\n");
    dprintf(fd, "  -a: dump all opened serial devices info\n");
    dprintf(fd, "  -l: dump serial device list info\n");
}

void SerialService::DumpPhysicalDevices(int fd)
{
    std::vector<SerialPortInfo> physicalDevices;
    int32_t queryRet = QuerySerialDevices(physicalDevices);
    if (queryRet == SERIAL_OK) {
        dprintf(fd, "Physical Serial Devices (from HDI):\n");
        dprintf(fd, "----------------------------------------\n");
        if (physicalDevices.empty()) {
            dprintf(fd, "  No serial devices detected.\n");
        } else {
            dprintf(fd, "  Total detected: %zu\n", physicalDevices.size());
            for (const auto& dev : physicalDevices) {
                dprintf(fd, "  Port Name: %s\n", dev.portName.c_str());
                dprintf(fd, "    Product ID: %d (0x%04X)\n", dev.productId, dev.productId);
                dprintf(fd, "    Vendor ID: %d (0x%04X)\n", dev.vendorId, dev.vendorId);
                dprintf(fd, "    Manufacturer: %s\n", dev.manufacturer.c_str());
            }
        }
        dprintf(fd, "----------------------------------------\n");
    } else {
        dprintf(fd, "QuerySerialDevices failed: %d\n", queryRet);
        dprintf(fd, "----------------------------------------\n");
    }
}

void SerialService::DumpOpenedDevices(int fd)
{
    std::lock_guard<std::mutex> lock(serviceMutex_);
    dprintf(fd, "Opened Serial Devices:\n");
    dprintf(fd, "----------------------------------------\n");
    if (deviceContexts_.empty()) {
        dprintf(fd, "  No serial devices currently opened.\n");
    } else {
        dprintf(fd, "  Total opened: %zu\n", deviceContexts_.size());
        for (const auto& pair : deviceContexts_) {
            const auto& ctx = pair.second;
            dprintf(fd, "  Port Name: %s\n", ctx->portName.c_str());
            dprintf(fd, "    Bundle Name: %s\n", ctx->bundleName.c_str());
            dprintf(fd, "    Token ID: %s\n", ctx->tokenId.c_str());
            dprintf(fd, "    User ID: %d\n", ctx->userId);
            dprintf(fd, "    Config:\n");
            dprintf(fd, "      Baud Rate: %d\n", ctx->config.baudRate);
            dprintf(fd, "      Data Bits: %d\n", ctx->config.dataBits);
            dprintf(fd, "      Stop Bits: %d\n", ctx->config.stopBits);
            dprintf(fd, "      Parity: %d\n", ctx->config.parity);
            dprintf(fd, "      RTSCTS: %d\n", ctx->config.rtscts);
        }
    }
    dprintf(fd, "----------------------------------------\n");
}

int SerialService::DoDump(int fd, const std::vector<std::string>& argList)
{
    if (argList.empty()) {
        DumpHelp(fd);
        return SERIAL_ERR_INVALID_PARAM;
    }

    const std::string& arg = argList[0];

    if (arg == "-h") {
        DumpHelp(fd);
        return SERIAL_OK;
    }

    if (arg == "-a" || arg == "-l") {
        dprintf(fd, "Serial Service Dump:\n");
        dprintf(fd, "========================================\n");
        DumpPhysicalDevices(fd);
        DumpOpenedDevices(fd);
        dprintf(fd, "========================================\n");
        return SERIAL_OK;
    }

    dprintf(fd, "Unknown option: %s\n", arg.c_str());
    DumpHelp(fd);
    return SERIAL_ERR_INVALID_PARAM;
}

int SerialService::Dump(int fd, const std::vector<std::u16string>& args)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d SerialService Dump called, fd=%{public}d, args.size=%{public}zu", __func__, __LINE__, fd,
        args.size());

    // Validate file descriptor validity
    if (fd < 0) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Invalid fd: %{public}d", __func__, __LINE__, fd);
        return SERIAL_ERR_INVALID_PARAM;
    }

    // Convert u16string args to std::string
    // hidumper passes u16string format args for Unicode support
    std::vector<std::string> argList;
    for (const auto& arg : args) {
        std::string strArg;
        // Convert each u16char to char (ASCII only, ignore non-ASCII)
        for (const auto& ch : arg) {
            if (ch <= 0xFF) { // Only convert ASCII range characters
                strArg.push_back(static_cast<char>(ch));
            }
        }
        argList.push_back(strArg);
    }

    // Delegate to DoDump for actual dump processing
    return DoDump(fd, argList);
}

bool SerialService::IsUsbSerial(const std::string& portName)
{
    return (portName.find("USB") != std::string::npos || portName.find("ACM") != std::string::npos);
}

void SerialService::SetSerialManagePolicy(bool disable)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:serial policy set: %{public}d.", __func__, int(disable));
    isRs232Disabled_ = disable;
}

void SerialService::ReportSerialManageAuditEvent(const std::string& portType, bool disable)
{
    if (!reportManageFlag_) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:skip report manage policy: %{public}d.", __func__, int(disable));
        reportManageFlag_ = true;
        return;
    }
    // report audit event
    nlohmann::json auditJson {
        {"portType", portType},
        {"portStatus", disable ? "Disabled" : "Enabled"},
        {"userId", GetCallingUserId()},
        {"happenTime", Serial::SerialSecurityReport::GetCurrentTime()},
    };
    Serial::SerialSecurityReport::ReportSecurityInfo(
        SERIAL_EDM_EVENT_ID, std::string(SERIAL_EDM_EVENT_VERSION), auditJson);
}

void SerialService::ReportSerialOperationAudit(const std::string& portName, const std::string& operationType)
{
    int32_t portId = CalculatePortId(portName);

    // Report Security Audit Event for serial operation
    nlohmann::json operationJson {
        {"happenTime", Serial::SerialSecurityReport::GetCurrentTime()},
        {"portId", portId},
        {"operationType", operationType},
        {"uid", IPCSkeleton::GetCallingUid()},
    };
    Serial::SerialSecurityReport::ReportSecurityInfo(
        SERIAL_OP_EVENT_ID, std::string(SERIAL_OP_EVENT_VERSION), operationJson);
}

int32_t SerialService::CalculatePortId(const std::string& portName)
{
    int32_t baseId = GetIdFromPortName(portName);
    if (baseId == SERIAL_ERR_INVALID_PARAM) {
        return SERIAL_ERR_INVALID_PARAM;
    }

    if (IsUsbSerial(portName)) {
        if (portName.find("ACM") != std::string::npos) {
            return baseId + ACM_PORT_ID_OFFSET;
        }
        return baseId;
    }

    for (const auto& [prefix, offset] : pcieSerialOffsets_) {
        if (portName.find(prefix) != std::string::npos) {
            return baseId + offset;
        }
    }

    // Onboard serial: negative number
    return -baseId;
}

bool SerialService::QueryAndRecordFirstRead(const std::string& portName)
{
    std::lock_guard lock(readStatusMutex_);
    return portsHasBeenRead_.insert(portName).second;
}

bool SerialService::QueryAndRecordFirstWrite(const std::string& portName)
{
    std::lock_guard lock(writeStatusMutex_);
    return portsHasBeenWritten_.insert(portName).second;
}

void SerialService::ResetFirstRead(const std::string& portName)
{
    std::lock_guard lock(readStatusMutex_);
    portsHasBeenRead_.erase(portName);
}

void SerialService::ResetFirstWrite(const std::string& portName)
{
    std::lock_guard lock(writeStatusMutex_);
    portsHasBeenWritten_.erase(portName);
}

void SerialService::LogWriteData(const std::vector<uint8_t>& data)
{
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
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d WriteSerial data hex: %{public}s", __func__, __LINE__,
        hexStream.str().c_str());
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d WriteSerial data str: %{public}s", __func__, __LINE__,
        strStream.str().c_str());
}

int32_t SerialService::CheckWritePreconditions(DeviceContext* context, const std::string& portName)
{
    if (context == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s not opened", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (context->hdiDevice == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Device %{public}s is offline", __func__, __LINE__, portName.c_str());
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }

    int32_t permRet = CheckPortAccessPermission(context, portName);
    if (permRet != SERIAL_OK) {
        return permRet;
    }
    return SERIAL_OK;
}
} // namespace Serial
} // namespace OHOS