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

void SerialService::OnStart(const OHOS::SystemAbilityOnDemandReason& startReason)
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
        SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc loop start, portName=%{public}s, running=%{public}d",
            portName.c_str(), context->running.load());

        std::unique_lock<std::mutex> lock(context->mutex);
        bool stopped = context->cv.wait_for(lock, std::chrono::seconds(READ_POLL_INTERVAL_SEC), [&context] {
            return !context->running.load();
        });

        SERIAL_HILOGI(MODULE_SERVICE,
            "ReadThreadFunc after wait_for, portName=%{public}s, stopped=%{public}d, running=%{public}d",
            portName.c_str(), stopped, context->running.load());

        if (stopped) {
            SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc stopped by condition, portName=%{public}s", portName.c_str());
            break;
        }

        if (!context->running.load()) {
            SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc stopped, portName=%{public}s", portName.c_str());
            break;
        }

        if (callback != nullptr && !context->lastWrittenData.empty()) {
            SERIAL_HILOGI(MODULE_SERVICE,
                "ReadThreadFunc calling onDataRead, portName=%{public}s, dataSize=%{public}zu", portName.c_str(),
                context->lastWrittenData.size());
            callback->onDataRead(portName, context->lastWrittenData);
        } else if (context->lastWrittenData.empty()) {
            SERIAL_HILOGD(MODULE_SERVICE, "ReadThreadFunc: no data to send, portName=%{public}s", portName.c_str());
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc exited, portName=%{public}s", portName.c_str());
}

namespace {

constexpr int32_t TEST_UNKNOWN_ERROR = -99;

struct TestScenario {
    int32_t queryRet;
    int32_t openRet;
    int32_t closeRet;
    int32_t writeRet;
    int32_t flushRet;
    int32_t drainRet;
    int32_t sendBrkRet;
    int32_t setRtsRet;
    int32_t getCtsRet;
    int32_t addAuthRet;
};

static std::atomic<int32_t> g_testRoundIndex {0};

static const TestScenario TEST_SCENARIOS[] = {
    {SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK},
    {SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE,
        SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE, SERIAL_ERR_SERVICE},
    {SERIAL_OK, SERIAL_ERR_INVALID_PARAM, SERIAL_OK, SERIAL_ERR_INVALID_PARAM, SERIAL_OK, SERIAL_OK, SERIAL_OK,
        SERIAL_OK, SERIAL_OK, SERIAL_ERR_INVALID_PARAM},
    {SERIAL_OK, SERIAL_ERR_DEVICE_DISCONNECTED, SERIAL_OK, SERIAL_ERR_DEVICE_DISCONNECTED,
        SERIAL_ERR_DEVICE_DISCONNECTED, SERIAL_ERR_DEVICE_DISCONNECTED, SERIAL_ERR_DEVICE_DISCONNECTED,
        SERIAL_ERR_DEVICE_DISCONNECTED, SERIAL_ERR_DEVICE_DISCONNECTED, SERIAL_OK},
    {SERIAL_OK, SERIAL_ERR_DEVICE_BUSY, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK,
        SERIAL_OK},
    {SERIAL_OK, SERIAL_OK, SERIAL_ERR_DEVICE_NOT_OPENED, SERIAL_ERR_DEVICE_NOT_OPENED, SERIAL_ERR_DEVICE_NOT_OPENED,
        SERIAL_ERR_DEVICE_NOT_OPENED, SERIAL_ERR_DEVICE_NOT_OPENED, SERIAL_ERR_DEVICE_NOT_OPENED,
        SERIAL_ERR_DEVICE_NOT_OPENED, SERIAL_OK},
    {SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_ERR_TIMEOUT, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK,
        SERIAL_OK},
    {SERIAL_OK, SERIAL_ERR_PERMISSION_DENIED, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK,
        SERIAL_OK, SERIAL_OK},
    {SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK,
        SERIAL_ERR_NOT_ALLOWED},
    {TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR,
        TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR, TEST_UNKNOWN_ERROR},
    {SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK,
        SERIAL_ERR_NOT_SYSTEM_APP},
    {SERIAL_ERR_ENTERPRISE_POLICY, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK, SERIAL_OK,
        SERIAL_OK, SERIAL_OK},
};

static constexpr int32_t TEST_SCENARIO_COUNT = sizeof(TEST_SCENARIOS) / sizeof(TEST_SCENARIOS[0]);

static int32_t AdvanceTestRound()
{
    return g_testRoundIndex.fetch_add(1) % TEST_SCENARIO_COUNT;
}

static int32_t GetCurrentTestRound()
{
    return (g_testRoundIndex.load() - 1 + TEST_SCENARIO_COUNT) % TEST_SCENARIO_COUNT;
}

static const TestScenario& GetCurrentTestScenario()
{
    return TEST_SCENARIOS[GetCurrentTestRound()];
}

} // namespace

int32_t SerialService::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    int32_t round = AdvanceTestRound();
    const TestScenario& scenario = TEST_SCENARIOS[round];
    SERIAL_HILOGI(
        MODULE_SERVICE, "[TEST] QuerySerialDevices round=%{public}d returning %{public}d", round, scenario.queryRet);
    devices.push_back({"/dev/ttyS0", 1, 1, "MfgA"});
    devices.push_back({"/dev/ttyS1", 2, 2, "MfgB"});
    return scenario.queryRet;
}

int32_t SerialService::OpenSerial(
    const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] OpenSerial returning %{public}d", scenario.openRet);
    if (scenario.openRet == SERIAL_OK && token != nullptr) {
        std::lock_guard<std::mutex> lock(g_serviceMutex);
        if (g_readContexts.find(portInfo.portName) == g_readContexts.end()) {
            auto context = std::make_shared<ReadContext>();
            context->callback = token;
            g_readContexts[portInfo.portName] = context;
        }
    }
    return scenario.openRet;
}

int32_t SerialService::CloseSerial(const std::string& portName)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] CloseSerial returning %{public}d", scenario.closeRet);
    if (scenario.closeRet != SERIAL_OK) {
        return scenario.closeRet;
    }
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    auto it = g_readContexts.find(portName);
    if (it == g_readContexts.end()) {
        return scenario.closeRet;
    }
    DetachReadThread(it->second);
    g_readContexts.erase(it);
    return scenario.closeRet;
}

int32_t SerialService::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] WriteSerial returning %{public}d", scenario.writeRet);
    if (scenario.writeRet == SERIAL_OK) {
        bytesWritten = static_cast<int32_t>(data.size());
        std::lock_guard<std::mutex> lock(g_serviceMutex);
        auto it = g_readContexts.find(portName);
        if (it != g_readContexts.end() && it->second) {
            it->second->lastWrittenData = data;
        }
    } else {
        bytesWritten = 0;
    }
    return scenario.writeRet;
}

int32_t SerialService::StartRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] StartRead, portName=%{public}s", portName.c_str());
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
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] StopRead, portName=%{public}s", portName.c_str());
    std::lock_guard<std::mutex> lock(g_serviceMutex);
    auto it = g_readContexts.find(portName);
    if (it == g_readContexts.end() || !it->second) {
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }
    if (!it->second->running) {
        return SERIAL_OK;
    }
    DetachReadThread(it->second);
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] StopRead signaled stop, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::Flush(const std::string& portName)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] Flush returning %{public}d", scenario.flushRet);
    return scenario.flushRet;
}

int32_t SerialService::Drain(const std::string& portName)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] Drain returning %{public}d", scenario.drainRet);
    return scenario.drainRet;
}

int32_t SerialService::SendBrkSignal(const std::string& portName)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] SendBrkSignal returning %{public}d", scenario.sendBrkRet);
    return scenario.sendBrkRet;
}

int32_t SerialService::SetRtsSignal(const std::string& portName, bool rts)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] SetRtsSignal returning %{public}d", scenario.setRtsRet);
    return scenario.setRtsRet;
}

int32_t SerialService::GetCtsSignal(const std::string& portName, bool& cts)
{
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] GetCtsSignal returning %{public}d", scenario.getCtsRet);
    cts = false;
    return scenario.getCtsRet;
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
    const TestScenario& scenario = GetCurrentTestScenario();
    SERIAL_HILOGI(MODULE_SERVICE, "[TEST] AddPortAuthorization returning %{public}d", scenario.addAuthRet);
    return scenario.addAuthRet;
}

} // namespace Serial
} // namespace OHOS
