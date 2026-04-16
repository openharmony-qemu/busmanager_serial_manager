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
#include <iomanip>
#include <iostream>
#include <sstream>

#include "hilog_wrapper.h"
#include "if_system_ability_manager.h"
#include "iservice_registry.h"
#include "serial_error_code.h"
#include "serial_service.h"
#include "system_ability_definition.h"

namespace OHOS {
namespace Serial {

namespace {
constexpr int32_t READ_POLL_INTERVAL_SEC = 5;
constexpr uint8_t PRINTABLE_ASCII_MIN = 32;
constexpr uint8_t PRINTABLE_ASCII_MAX = 126;
constexpr int HEX_BYTE_WIDTH = 2;
} // namespace

REGISTER_SYSTEM_ABILITY_BY_ID(SerialService, SERIAL_SYSTEM_ABILITY_ID, true);

SerialService::SerialService(int32_t systemAbilityId, bool runOnCreate) : SystemAbility(systemAbilityId, runOnCreate)
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService constructed");
}

SerialService::~SerialService()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService destroyed");
    std::lock_guard<std::mutex> lock(serviceMutex_);
    for (auto& pair : readContexts_) {
        if (pair.second && pair.second->running) {
            pair.second->running = false;
            pair.second->cv.notify_all();
            if (pair.second->readThread.joinable()) {
                pair.second->readThread.join();
            }
        }
    }
    readContexts_.clear();
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
    std::lock_guard<std::mutex> lock(serviceMutex_);
    for (auto& pair : readContexts_) {
        if (pair.second && pair.second->running) {
            pair.second->running = false;
            pair.second->cv.notify_all();
            if (pair.second->readThread.joinable()) {
                pair.second->readThread.join();
            }
        }
    }
    readContexts_.clear();
    registerToService_ = false;
    SERIAL_HILOGI(MODULE_SERVICE, "SerialService OnStop success");
}

void SerialService::ReadThreadFunc(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc started, portName=%{public}s", portName.c_str());

    sptr<ISerialServiceCallback> callback;
    std::shared_ptr<ReadContext> context;
    {
        std::lock_guard<std::mutex> lock(serviceMutex_);
        auto it = readContexts_.find(portName);
        if (it == readContexts_.end() || !it->second) {
            SERIAL_HILOGE(MODULE_SERVICE, "ReadThreadFunc: portName %{public}s not found", portName.c_str());
            return;
        }
        context = it->second;
        callback = context->callback;
    }

    while (context->running.load()) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "ReadThreadFunc loop start, portName=%{public}s, "
            "running=%{public}d",
            portName.c_str(), context->running.load());

        std::unique_lock<std::mutex> lock(context->mutex);
        bool stopped = context->cv.wait_for(lock, std::chrono::seconds(READ_POLL_INTERVAL_SEC), [&context] {
            return !context->running.load();
        });

        SERIAL_HILOGI(MODULE_SERVICE,
            "ReadThreadFunc after wait_for, portName=%{public}s, "
            "stopped=%{public}d, running=%{public}d",
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
                "ReadThreadFunc calling onDataRead, portName=%{public}s, "
                "dataSize=%{public}zu",
                portName.c_str(), context->lastWrittenData.size());
            callback->onDataRead(portName, context->lastWrittenData);
        } else if (context->lastWrittenData.empty()) {
            SERIAL_HILOGD(MODULE_SERVICE, "ReadThreadFunc: no data to send, portName=%{public}s", portName.c_str());
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "ReadThreadFunc exited, portName=%{public}s", portName.c_str());
}

static std::atomic<int32_t> g_queryCallCount {0};

static const std::vector<std::vector<SerialPortInfo>> g_fakeDeviceScenarios = {
    {
        {"/dev/ttyS0", 1, 1, "MfgA"},
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {
        {"/dev/ttyS0", 1, 1, "MfgA"},
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {
        {"/dev/ttyS0", 99, 1, "MfgA"},
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {
        {"/dev/ttyS0", 1, 1, "MfgA"},
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {
        {"/dev/ttyS0", 1, 1, "MfgA"},
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {
        {"/dev/ttyS0", 1, 1, "MfgA"},
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
    {},
    {
        {"/dev/ttyS0", 1, 1, "MfgA"},
        {"/dev/ttyS1", 2, 2, "MfgB"},
    },
};

int32_t SerialService::QuerySerialDevices(std::vector<SerialPortInfo>& devices)
{
    int32_t idx = g_queryCallCount.fetch_add(1);
    int32_t scenarioIdx = idx % static_cast<int32_t>(g_fakeDeviceScenarios.size());

    SERIAL_HILOGI(
        MODULE_SERVICE, "QuerySerialDevices called, callCount=%{public}d, scenario=%{public}d", idx, scenarioIdx);

    const auto& scenario = g_fakeDeviceScenarios[scenarioIdx];
    for (const auto& dev : scenario) {
        devices.push_back(dev);
        SERIAL_HILOGI(MODULE_SERVICE,
            "  device: portName=%{public}s, productId=%{public}d, "
            "vendorId=%{public}d, "
            "manufacturer=%{public}s",
            dev.portName.c_str(), dev.productId, dev.vendorId, dev.manufacturer.c_str());
    }

    SERIAL_HILOGI(MODULE_SERVICE, "QuerySerialDevices return %{public}zu devices", devices.size());
    return SERIAL_OK;
}

int32_t SerialService::OpenSerial(
    const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "OpenSerial called, portName=%{public}s, baudRate=%{public}d, "
        "dataBits=%{public}d, stopBits=%{public}d, parity=%{public}d, "
        "rtscts=%{public}d, xon=%{public}d, xoff=%{public}d, xany=%{public}d",
        portInfo.portName.c_str(), config.baudRate, config.dataBits, config.stopBits, config.parity, config.rtscts,
        config.xon, config.xoff, config.xany);

    if (token == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "OpenSerial failed: token is null");
        return SERIAL_ERR_SERVICE;
    }

    std::lock_guard<std::mutex> lock(serviceMutex_);

    if (readContexts_.find(portInfo.portName) != readContexts_.end()) {
        SERIAL_HILOGW(MODULE_SERVICE, "PortName %{public}s already opened", portInfo.portName.c_str());
        return SERIAL_ERR_DEVICE_BUSY;
    }

    auto context = std::make_shared<ReadContext>();
    context->callback = token;
    readContexts_[portInfo.portName] = context;

    SERIAL_HILOGI(MODULE_SERVICE, "OpenSerial success, portName=%{public}s, token saved", portInfo.portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::CloseSerial(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial called, portName=%{public}s", portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    auto it = readContexts_.find(portName);
    if (it == readContexts_.end()) {
        SERIAL_HILOGW(MODULE_SERVICE, "PortName %{public}s not found", portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (it->second) {
        if (it->second->running) {
            it->second->running = false;
            it->second->cv.notify_all();
            if (it->second->readThread.joinable()) {
                it->second->readThread.join();
            }
        }
        readContexts_.erase(it);
    }

    SERIAL_HILOGI(MODULE_SERVICE, "CloseSerial success, portName=%{public}s", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::WriteSerial(
    const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "WriteSerial called, portName=%{public}s, dataSize=%{public}zu, "
        "timeout=%{public}d",
        portName.c_str(), data.size(), timeout);

    std::ostringstream hexStream;
    std::ostringstream strStream;
    for (size_t i = 0; i < data.size(); ++i) {
        hexStream << std::hex << std::setfill('0') << std::setw(HEX_BYTE_WIDTH) << static_cast<int>(data[i]);
        if (i < data.size() - 1) {
            hexStream << " ";
        }

        if (data[i] >= PRINTABLE_ASCII_MIN && data[i] <= PRINTABLE_ASCII_MAX) {
            strStream << static_cast<char>(data[i]);
        } else {
            strStream << ".";
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial data hex: %{public}s", hexStream.str().c_str());
    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial data str: %{public}s", strStream.str().c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    auto it = readContexts_.find(portName);
    if (it == readContexts_.end() || !it->second) {
        SERIAL_HILOGE(MODULE_SERVICE, "WriteSerial: portName %{public}s not opened", portName.c_str());
        bytesWritten = 0;
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }
    it->second->lastWrittenData = data;
    SERIAL_HILOGI(MODULE_SERVICE, "WriteSerial saved lastWrittenData, size=%{public}zu", data.size());

    bytesWritten = static_cast<int32_t>(data.size());
    return SERIAL_OK;
}

int32_t SerialService::StartRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "StartRead called, portName=%{public}s", portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    auto it = readContexts_.find(portName);
    if (it == readContexts_.end() || !it->second) {
        SERIAL_HILOGE(MODULE_SERVICE, "StartRead failed: portName %{public}s not opened", portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    if (it->second->running) {
        SERIAL_HILOGW(MODULE_SERVICE, "StartRead: portName %{public}s already reading", portName.c_str());
        return SERIAL_OK;
    }

    it->second->running = true;
    it->second->readThread = std::thread(&SerialService::ReadThreadFunc, this, portName);

    SERIAL_HILOGI(MODULE_SERVICE, "StartRead success, portName=%{public}s, read thread started", portName.c_str());
    return SERIAL_OK;
}

int32_t SerialService::StopRead(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "StopRead called, portName=%{public}s", portName.c_str());

    std::lock_guard<std::mutex> lock(serviceMutex_);
    auto it = readContexts_.find(portName);
    if (it == readContexts_.end() || !it->second) {
        SERIAL_HILOGE(MODULE_SERVICE, "StopRead failed: portName %{public}s not opened", portName.c_str());
        return SERIAL_ERR_DEVICE_NOT_OPENED;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "StopRead: portName=%{public}s, running before=%{public}d", portName.c_str(),
        it->second->running.load());

    if (!it->second->running) {
        SERIAL_HILOGW(MODULE_SERVICE, "StopRead: portName %{public}s not reading", portName.c_str());
        return SERIAL_OK;
    }

    it->second->running = false;
    SERIAL_HILOGI(MODULE_SERVICE,
        "StopRead: portName=%{public}s, running after=%{public}d, calling "
        "notify_all",
        portName.c_str(), it->second->running.load());
    it->second->cv.notify_all();

    if (it->second->readThread.joinable()) {
        SERIAL_HILOGI(MODULE_SERVICE, "StopRead: portName=%{public}s, joining thread", portName.c_str());
        it->second->readThread.join();
    }

    SERIAL_HILOGI(MODULE_SERVICE, "StopRead success, portName=%{public}s, read thread stopped", portName.c_str());
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
