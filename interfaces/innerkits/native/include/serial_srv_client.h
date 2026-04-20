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

#ifndef SERIALMGR_SERIAL_SRV_CLIENT_H
#define SERIALMGR_SERIAL_SRV_CLIENT_H

#include <mutex>
#include <singleton.h>

#include "iremote_object.h"
#include "iserial_server.h"
#include "iserial_service_callback.h"
#include "serial_server_types.h"
#include "serial_service_callback.h"

namespace OHOS {
namespace Serial {

class SerialSrvClient final {
public:
    DISALLOW_COPY_AND_MOVE(SerialSrvClient);

    static SerialSrvClient& GetInstance();

    int32_t QuerySerialDevices(std::vector<SerialPortInfo>& devices);
    int32_t OpenSerial(const SerialPortInfo& portInfo, const SerialConfig& config);
    int32_t CloseSerial(const std::string& portName);
    int32_t WriteSerial(
        const std::string& portName, const std::vector<uint8_t>& data, int32_t timeout, int32_t& bytesWritten);
    int32_t Flush(const std::string& portName);
    int32_t Drain(const std::string& portName);
    int32_t SendBrkSignal(const std::string& portName);
    int32_t SetRtsSignal(const std::string& portName, bool rts);
    int32_t GetCtsSignal(const std::string& portName, bool& cts);
    int32_t AddPortAuthorization(const std::string& tokenId, const std::string& deviceId);

    ListenerHandle RegisterDataListener(const std::string& portName, const DataReceivedCallback& callback);
    int32_t UnregisterDataListener(const std::string& portName, ListenerHandle listenerHandle);

private:
    SerialSrvClient();
    ~SerialSrvClient();

    sptr<ISerialServer> GetProxy();
    int32_t ConnectUnLocked(bool force = false);
    void ResetProxy(const wptr<IRemoteObject>& remote);

    class SerialSrvDeathRecipient : public IRemoteObject::DeathRecipient {
    public:
        SerialSrvDeathRecipient() = default;
        ~SerialSrvDeathRecipient() override = default;
        void OnRemoteDied(const wptr<IRemoteObject>& remote) override;

    private:
        DISALLOW_COPY_AND_MOVE(SerialSrvDeathRecipient);
    };

    sptr<ISerialServer> proxy_ = nullptr;
    sptr<IRemoteObject::DeathRecipient> deathRecipient_ = nullptr;
    sptr<SerialServiceCallback> callback_ = nullptr;
    std::mutex mutex_;
};

} // namespace Serial

} // namespace OHOS

#endif
