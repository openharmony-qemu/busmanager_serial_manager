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

#include <chrono>
#include <condition_variable>
#include <thread>

#include "hilog_wrapper.h"
#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "serial_error_code.h"
#include "serial_service.h"
#include "system_ability_definition.h"

namespace OHOS {
namespace Serial {

struct ReadContext {
    sptr<ISerialServiceCallback> callback;
    std::thread readThread;
    std::atomic<bool> running {false};
    std::condition_variable cv;
    std::mutex mutex;
    std::vector<uint8_t> lastWrittenData;
};

static std::mutex g_serviceMutex;
static std::map<std::string, std::shared_ptr<ReadContext>> g_readContexts;

namespace {
constexpr int32_t READ_POLL_INTERVAL_SEC = 5;

static void DetachReadThread(std::shared_ptr<ReadContext>& ctx)
{
    if (!ctx) {
        return;
    }
    ctx->running = false;
    ctx->cv.notify_all();
    if (ctx->readThread.joinable()) {
        ctx->readThread.detach();
    }
}
} // namespace

REGISTER_SYSTEM_ABILITY_BY_ID(SerialService, SERIAL_SYSTEM_ABILITY_ID, true);

SerialService::SerialService(int32_t systemAbilityId, bool runOnCreate)
    : SystemAbility(systemAbilityId, runOnCreate), unloadTimer_("SerialServiceUnload")
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService constructed");
}

SerialService::~SerialService()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService destroyed");
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    for (auto& pair : g_readContexts) {
        DetachReadThread(pair.second);
    }
    g_readContexts.clear();
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
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    for (auto& pair : g_readContexts) {
        DetachReadThread(pair.second);
    }
    g_readContexts.clear();
    registerToService_ = false;
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnStop success");
}

int32_t SerialService::OnIdle(const SystemAbilityOnDemandReason& idleReason)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnIdle");
    return 0;
}

int SerialService::Dump(int fd, const std::vector<std::u16string>& args)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService Dump");
    return 0;
}

int32_t SerialService::OnRemoteRequest(uint32_t code, MessageParcel& data, MessageParcel& reply, MessageOption& option)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnRemoteRequest");
    return SerialServerStub::OnRemoteRequest(code, data, reply, option);
}

static void ReadThreadFunc(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc started, portName=%{public}s", portName.c_str());

    sptr<ISerialServiceCallback> callback;
    std::shared_ptr<ReadContext> context;
    {
        std::lock_guard<std::mutex> lock(g_serviceMutex);
        auto it = g_readContexts.find(portName);
        if (it == g_readContexts.end() || !it->second) {
            SERIAL_HILOGE(MODULE_SERVICE, "ReadThreadFunc: portName %{public}s not found", portName.c_str());
            return;
        }
        context = it->second;
        callback = context->callback;
    }

    while (context->running.load()) {
        std::unique_lock<std::mutex> lock(context->mutex);
        bool stopped = context->cv.wait_for(lock, std::chrono::seconds(READ_POLL_INTERVAL_SEC), [&context] {
            return !context->running.load();
        });
        if (stopped || !context->running.load()) {
            break;
        }

        if (callback != nullptr && !context->lastWrittenData.empty()) {
            callback->onDataRead(portName, context->lastWrittenData);
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc exited, portName=%{public}s", portName.c_str());
}

int32_t SerialService::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] QuerySerialDevices returning 3 devices");
    devices.push_back({"/dev/ttyS0", 0, 0, ""});
    devices.push_back({"/dev/ttyUSB0", 0x1234, 0x5678, "FTDI"});
    devices.push_back({"/dev/ttyUSB1", 0x9999, 0xABCD, "Prolific"});
    return SERIAL_OK;
}

int32_t SerialService::OpenSerial(
    const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] OpenSerial portName=%{public}s", portInfo.portName.c_str());
    if (token == nullptr) {
        return SERIAL_ERR_SERVICE;
    }

    std::lock_guard<std::mutex> lock(g_serviceMutex);
    if (g_readContexts.find(portInfo.portName) != g_readContexts.end()) {
        return SERIAL_ERR_DEVICE_BUSY;
    }

    auto context = std::make_shared<ReadContext>();
    context->callback = token;
    g_readContexts[portInfo.portName] = context;
    return SERIAL_OK;
}

int32_t SerialService::CloseSerial(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] CloseSerial portName=%{public}s", portName.c_str());
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    auto it = g_readContexts.find(portName);
    if (it == g_readContexts.end()) {
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }
    DetachReadThread(it->second);
    g_readContexts.erase(it);
    return SERIAL_OK;
}

int32_t SerialService::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] WriteSerial portName=%{public}s, dataSize=%{public}zu", portName.c_str(),
        data.size());
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    auto it = g_readContexts.find(portName);
    if (it == g_readContexts.end() || !it->second) {
        bytesWritten = 0;
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }
    it->second->lastWrittenData = data;
    bytesWritten = static_cast<int32_t>(data.size());
    return SERIAL_OK;
}

int32_t SerialService::StartRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] StartRead portName=%{public}s", portName.c_str());
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    auto it = g_readContexts.find(portName);
    if (it == g_readContexts.end() || !it->second) {
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }
    if (it->second->running) {
        return SERIAL_OK;
    }
    it->second->running = true;
    it->second->readThread = std::thread(ReadThreadFunc, portName);
    return SERIAL_OK;
}

int32_t SerialService::StopRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] StopRead portName=%{public}s", portName.c_str());
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    auto it = g_readContexts.find(portName);
    if (it == g_readContexts.end() || !it->second) {
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }
    if (!it->second->running) {
        return SERIAL_OK;
    }
    DetachReadThread(it->second);
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] StopRead signaled stop, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::Flush(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] Flush portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::Drain(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] Drain portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::SendBrkSignal(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] SendBrkSignal portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::SetRtsSignal(const std::string& portName, bool rts)
{
    SERIAL_HILOGI(
        MODULE_SERVICE, "[PARAMTEST] SetRtsSignal portName=%{public}s, rts=%{public}d", portName.c_str(), rts);
    return SERIAL_OK;
}

int32_t SerialService::GetCtsSignal(const std::string& portName, bool& cts)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] GetCtsSignal portName=%{public}s", portName.c_str());
    cts = false;
    return SERIAL_OK;
}

int32_t SerialService::AddPortAuthorization(const std::string& tokenId, const std::string& deviceId)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[PARAMTEST] AddPortAuthorization tokenId=%{public}s, deviceId=%{public}s",
        tokenId.c_str(), deviceId.c_str());
    return SERIAL_OK;
}

} // namespace Serial
} // namespace OHOS
