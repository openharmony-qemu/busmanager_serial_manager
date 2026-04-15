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

#ifndef SERIAL_SERVICE_H
#define SERIAL_SERVICE_H

#include <vector>
#include <mutex>
#include <map>
#include <thread>
#include <condition_variable>
#include <atomic>

#include "system_ability.h"
#include "serial_server_stub.h"
#include "iserial_service_callback.h"

namespace OHOS {
namespace Serial {

struct ReadContext {
    sptr<ISerialServiceCallback> callback;
    std::thread readThread;
    std::atomic<bool> running{false};
    std::condition_variable cv;
    std::mutex mutex;
    std::vector<uint8_t> lastWrittenData;
};

class SerialService : public SystemAbility, public SerialServerStub {
    DECLARE_SYSTEM_ABILITY(SerialService);

public:
    SerialService(int32_t systemAbilityId, bool runOnCreate);
    ~SerialService() override;

    void OnStart() override;
    void OnStop() override;

    int32_t QuerySerialDevices(std::vector<SerialPortInfo>& devices) override;
    int32_t OpenSerial(const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& token) override;
    int32_t CloseSerial(const std::string& portName) override;
    int32_t WriteSerial(const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten) override;
    int32_t StartRead(const std::string& portName) override;
    int32_t StopRead(const std::string& portName) override;
    int32_t Flush(const std::string& portName) override;
    int32_t Drain(const std::string& portName) override;
    int32_t SendBrkSignal(const std::string& portName) override;
    int32_t SetRtsSignal(const std::string& portName, bool rts) override;
    int32_t GetCtsSignal(const std::string& portName, bool& cts) override;
    int32_t AddPortAuthorization(const std::string& tokenId, const std::string& deviceId) override;

private:
    void ReadThreadFunc(const std::string& portName);

    bool registerToService_ = false;
    std::mutex serviceMutex_;
    std::map<std::string, std::shared_ptr<ReadContext>> readContexts_;
};

}
} // namespace OHOS
#endif