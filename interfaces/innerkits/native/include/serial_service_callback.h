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

#ifndef SERIALMGR_SERIAL_SERVICE_CALLBACK_H
#define SERIALMGR_SERIAL_SERVICE_CALLBACK_H

#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "iremote_object.h"
#include "serial_service_callback_stub.h"

namespace OHOS {
namespace Serial {

using DataReceivedCallback = std::function<void(const std::vector<uint8_t>& data)>;
using ListenerHandle = uint64_t;

class SerialServiceCallback : public SerialServiceCallbackStub {
public:
    SerialServiceCallback() = default;
    ~SerialServiceCallback() override = default;

    ErrCode onDataRead(const std::string& portName, const std::vector<uint8_t>& buffer) override;

    ListenerHandle AddListener(const std::string& portName, const DataReceivedCallback& callback);
    bool RemoveListener(const std::string& portName, ListenerHandle listenerHandle);
    void RemoveAllListeners(const std::string& portName);
    bool HasListeners(const std::string& portName);
    size_t GetListenerCount(const std::string& portName);

private:
    std::mutex listenerMutex_;
    std::map<std::string, std::map<ListenerHandle, DataReceivedCallback>> listeners_;
    ListenerHandle nextListenerHandle_ = 1;
};

} // namespace Serial
} // namespace OHOS

#endif
