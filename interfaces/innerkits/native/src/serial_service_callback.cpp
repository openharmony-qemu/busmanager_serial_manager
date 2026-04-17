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

#include "serial_service_callback.h"
#include "hilog_wrapper.h"

namespace OHOS {
namespace Serial {

ErrCode SerialServiceCallback::onDataRead(const std::string& portName, const std::vector<uint8_t>& buffer)
{
    SERIAL_HILOGD(
        MODULE_INNERKIT, "[onDataRead] portName=%{public}s, size=%{public}zu", portName.c_str(), buffer.size());

    std::lock_guard<std::mutex> lock(listenerMutex_);
    auto it = listeners_.find(portName);
    if (it == listeners_.end() || it->second.empty()) {
        SERIAL_HILOGW(MODULE_INNERKIT, "[onDataRead] No listener for portName=%{public}s", portName.c_str());
        return ERR_OK;
    }

    for (const auto& pair : it->second) {
        if (pair.second) {
            pair.second(buffer);
        }
    }

    return ERR_OK;
}

ListenerHandle SerialServiceCallback::AddListener(const std::string& portName, const DataReceivedCallback& callback)
{
    if (!callback) {
        SERIAL_HILOGE(MODULE_INNERKIT, "[AddListener] Invalid callback");
        return 0;
    }

    std::lock_guard<std::mutex> lock(listenerMutex_);
    ListenerHandle listenerHandle = nextListenerHandle_++;
    listeners_[portName][listenerHandle] = callback;
    SERIAL_HILOGI(MODULE_INNERKIT, "[AddListener] portName=%{public}s, listenerHandle=%{public}llu, count=%{public}zu",
        portName.c_str(), (unsigned long long)listenerHandle, listeners_[portName].size());
    return listenerHandle;
}

bool SerialServiceCallback::RemoveListener(const std::string& portName, ListenerHandle listenerHandle)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    auto portIt = listeners_.find(portName);
    if (portIt == listeners_.end()) {
        SERIAL_HILOGW(MODULE_INNERKIT, "[RemoveListener] No listeners for portName=%{public}s", portName.c_str());
        return false;
    }

    auto& listenerMap = portIt->second;
    auto it = listenerMap.find(listenerHandle);
    if (it == listenerMap.end()) {
        SERIAL_HILOGW(MODULE_INNERKIT,
            "[RemoveListener] Listener not found for portName=%{public}s, listenerHandle=%{public}llu",
            portName.c_str(), (unsigned long long)listenerHandle);
        return false;
    }

    listenerMap.erase(it);
    SERIAL_HILOGI(MODULE_INNERKIT,
        "[RemoveListener] portName=%{public}s, listenerHandle=%{public}llu, remaining=%{public}zu", portName.c_str(),
        (unsigned long long)listenerHandle, listenerMap.size());

    if (listenerMap.empty()) {
        listeners_.erase(portIt);
    }

    return true;
}

void SerialServiceCallback::RemoveAllListeners(const std::string& portName)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    auto it = listeners_.find(portName);
    if (it != listeners_.end()) {
        SERIAL_HILOGI(MODULE_INNERKIT, "[RemoveAllListeners] portName=%{public}s, count=%{public}zu", portName.c_str(),
            it->second.size());
        listeners_.erase(it);
    }
}

bool SerialServiceCallback::HasListeners(const std::string& portName)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    auto it = listeners_.find(portName);
    return it != listeners_.end() && !it->second.empty();
}

size_t SerialServiceCallback::GetListenerCount(const std::string& portName)
{
    std::lock_guard<std::mutex> lock(listenerMutex_);
    auto it = listeners_.find(portName);
    if (it == listeners_.end()) {
        return 0;
    }
    return it->second.size();
}

} // namespace Serial
} // namespace OHOS
