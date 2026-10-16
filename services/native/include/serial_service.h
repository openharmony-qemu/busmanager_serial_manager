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

#ifndef SERIAL_SERVICE_H
#define SERIAL_SERVICE_H

#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <ipc_skeleton.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "iremote_object.h"
#include "iserial_service_callback.h"
#include "nlohmann/json.hpp"
#include "serial_right_manager.h"
#include "serial_security_report.h"
#include "serial_server_stub.h"
#include "system_ability.h"
#include "system_ability_ondemand_reason.h"
#include "timer.h"

#include "v1_0/iserial_device.h"
#include "v1_0/iserial_device_callback.h"
#include "v1_0/iserials.h"
#include "v1_0/serial_types.h"

namespace OHOS {
namespace Serial {
constexpr int64_t SERIAL_OP_EVENT_ID = 0x030000100;     // serial operation audit event id
constexpr int64_t SERIAL_EDM_EVENT_ID = 0x030000101;    // serial management audit event id
constexpr const char* SERIAL_OP_EVENT_VERSION = "1.0";  // serial operation audit event version
constexpr const char* SERIAL_EDM_EVENT_VERSION = "1.0"; // serial management audit event version
constexpr const char* SERIAL_OPEN = "SerialOpen";
constexpr const char* SERIAL_CLOSE = "SerialClose";
constexpr const char* SERIAL_READ = "SerialRead";
constexpr const char* SERIAL_WRITE = "SerialWrite";
constexpr int32_t ACM_PORT_ID_OFFSET = 1000;
constexpr int32_t TTYS_PORT_ID_OFFSET = 2000;

class SerialService;

class SerialHdiCallback : public OHOS::HDI::Serial::V1_0::ISerialDeviceCallback {
public:
    explicit SerialHdiCallback(const sptr<ISerialServiceCallback>& serviceCallback, const std::string& portName);
    ~SerialHdiCallback() override = default;

    int32_t OnReadData(const std::vector<int8_t>& data, uint32_t dataLen) override;
    int32_t OnDeviceOffline() override;

private:
    sptr<ISerialServiceCallback> serviceCallback_;
    std::string portName_;
};

class SerialClientDeathRecipient : public IRemoteObject::DeathRecipient {
public:
    SerialClientDeathRecipient(const std::string& portName, SerialService* service);
    ~SerialClientDeathRecipient() override = default;

    void OnRemoteDied(const wptr<IRemoteObject>& object) override;

private:
    std::string portName_;
    SerialService* service_;
};

struct DeviceContext {
    sptr<OHOS::HDI::Serial::V1_0::ISerialDevice> hdiDevice;
    sptr<SerialHdiCallback> hdiCallback;
    sptr<ISerialServiceCallback> serviceCallback;
    sptr<SerialClientDeathRecipient> deathRecipient;
    std::string portName;
    std::string bundleName;
    std::string tokenId;
    int32_t userId;
    SerialConfig config;
    std::chrono::steady_clock::time_point openTime;
};

class SerialService : public SystemAbility, public SerialServerStub {
    DECLARE_SYSTEM_ABILITY(SerialService);

public:
    SerialService(int32_t systemAbilityId, bool runOnCreate);
    ~SerialService() override;

    void OnStart(const OHOS::SystemAbilityOnDemandReason& startReason) override;
    void OnStop() override;
    int32_t OnIdle(const SystemAbilityOnDemandReason& idleReason) override;
    int Dump(int fd, const std::vector<std::u16string>& args) override;

    int32_t QuerySerialDevices(std::vector<SerialPortInfo>& devices) override;
    int32_t OpenSerial(
        const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token) override;
    int32_t CloseSerial(const std::string& portName) override;
    int32_t WriteSerial(
        const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten) override;
    int32_t StartRead(const std::string& portName) override;
    int32_t StopRead(const std::string& portName) override;
    int32_t Flush(const std::string& portName) override;
    int32_t Drain(const std::string& portName) override;
    int32_t SendBrkSignal(const std::string& portName) override;
    int32_t SetRtsSignal(const std::string& portName, bool rts) override;
    int32_t GetCtsSignal(const std::string& portName, bool& cts) override;
    int32_t SetDtrSignal(const std::string& portName, bool dtr) override;
    int32_t GetDsrSignal(const std::string& portName, bool& dsr) override;
    int32_t AddPortAuthorization(const std::string& tokenId, const std::string& deviceId) override;

    void OnClientDied(const std::string& portName);
    void HandleDeviceOffline(const std::string& portName);
    int32_t OnRemoteRequest(uint32_t code, MessageParcel& data, MessageParcel& reply, MessageOption& option) override;

    static sptr<SerialService> GetGlobalInstance();

private:
    void InitHdiProxy();
    void InitCommonEventSubscriber();
    void LoadPcieSerialConfig();
    OHOS::HDI::Serial::V1_0::SerialConfig ConvertConfig(const SerialConfig& serviceConfig);
    SerialPortInfo ConvertDeviceInfo(const OHOS::HDI::Serial::V1_0::SerialDeviceInfo& hdiInfo);
    DeviceContext* FindDeviceContext(const std::string& portName);
    std::string FindPortNameForDevice(const SerialPortInfo& portInfo);
    int32_t CheckPortAccessPermission(const DeviceContext* context, const std::string& portName);

    bool IsUsbSerial(const std::string& portName);
    int32_t CalculatePortId(const std::string& portName);
    void SetSerialManagePolicy(bool disable);
    void ReportSerialOperationAudit(const std::string& portName, const std::string& operationType);
    void ReportSerialManageAuditEvent(const std::string& portType, bool disable);

    bool QueryAndRecordFirstRead(const std::string& portName);
    bool QueryAndRecordFirstWrite(const std::string& portName);
    void ResetFirstRead(const std::string& portName);
    void ResetFirstWrite(const std::string& portName);

    std::string GetCallingBundleName();
    std::string GetCallingTokenId();
    int32_t GetCallingUserId();
    int32_t GetCallingUserIdFromToken();
    bool CheckSerialPortRight(const std::string& portName, const std::string& productName);
    int32_t RequestSerialPortRight(const SerialPortInfo& portInfo);
    int32_t RequestSerialPortRight(const SerialPortInfo& portInfo, const std::string& portName);

    void ScheduleUnloadSa();
    void CancelUnloadSa();
    void DoUnloadSa();
    uint32_t GetDeviceCount();
    std::string BuildDeviceId(const SerialPortInfo& portInfo);

    void DumpHelp(int fd);
    int DoDump(int fd, const std::vector<std::string>& argList);

    // Internal helper functions for function decomposition
    int32_t ValidateOpenSerialParams(const SerialPortInfo& portInfo,
        const SerialConfig& config, const sptr<ISerialServiceCallback>& token);
    int32_t CheckWritePreconditions(DeviceContext* context, const std::string& portName);
    int32_t OpenHdiDevice(const std::string& portName, const SerialConfig& config,
        const sptr<ISerialServiceCallback>& token, sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& hdiDevice,
        sptr<SerialHdiCallback>& hdiCallback);
    std::shared_ptr<DeviceContext> BuildDeviceContext(const std::string& portName, const SerialConfig& config,
        const sptr<ISerialServiceCallback>& token, const sptr<OHOS::HDI::Serial::V1_0::ISerialDevice>& hdiDevice,
        const sptr<SerialHdiCallback>& hdiCallback);
    void DumpPhysicalDevices(int fd);
    void DumpOpenedDevices(int fd);

    // Helper functions for AddPortAuthorization
    int32_t ValidateDialogHapCaller(
        Security::AccessToken::AccessTokenID callerTokenId, Security::AccessToken::HapTokenInfo& callerTokenInfo);
    int32_t ParseTargetTokenId(const std::string& tokenId, Security::AccessToken::AccessTokenID& targetTokenId);

    bool registerToService_ = false;
    std::mutex serviceMutex_;

    sptr<OHOS::HDI::Serial::V1_0::ISerials> serialsProxy_;
    bool hdiStubMode_ = false;

    std::map<std::string, std::shared_ptr<DeviceContext>> deviceContexts_;
    std::map<std::pair<int32_t, int32_t>, std::string> productIdVendorIdToPortName_;

    Utils::Timer unloadTimer_;
    uint32_t unloadTimerId_ = 0;
    static constexpr uint32_t UNLOAD_SA_DELAY_MS = 30000;

    std::atomic<bool> isRs232Disabled_ = false;
    bool reportManageFlag_ = false;
    std::map<std::string, int32_t> pcieSerialOffsets_;
    std::unordered_set<std::string> portsHasBeenRead_;
    std::unordered_set<std::string> portsHasBeenWritten_;
    std::mutex readStatusMutex_;
    std::mutex writeStatusMutex_;
};

} // namespace Serial
} // namespace OHOS

#endif // SERIAL_SERVICE_H