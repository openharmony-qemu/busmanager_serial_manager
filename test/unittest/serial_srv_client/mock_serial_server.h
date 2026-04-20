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

#ifndef MOCK_SERIAL_SERVER_H
#define MOCK_SERIAL_SERVER_H

#include "iremote_stub.h"
#include "iserial_server.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace OHOS {
namespace Serial {

class MockSerialServer : public IRemoteStub<ISerialServer> {
public:
    DECLARE_INTERFACE_DESCRIPTOR(u"OHOS.Serial.ISerialServer");

    MockSerialServer() = default;
    ~MockSerialServer() override = default;

    int32_t OnRemoteRequest(uint32_t code, MessageParcel& data, MessageParcel& reply, MessageOption& option) override
    {
        return 0;
    }

    MOCK_METHOD1(QuerySerialDevices, ErrCode(std::vector<SerialPortInfo>& devices));
    MOCK_METHOD3(OpenSerial,
        ErrCode(const SerialPortInfo& portInfo, const SerialConfig& config, const sptr<ISerialServiceCallback>& cb));
    MOCK_METHOD1(CloseSerial, ErrCode(const std::string& portName));
    MOCK_METHOD4(WriteSerial,
        ErrCode(
            const std::string& portName, const std::vector<uint8_t>& buffer, int32_t timeout, int32_t& bytesWritten));
    MOCK_METHOD1(StartRead, ErrCode(const std::string& portName));
    MOCK_METHOD1(StopRead, ErrCode(const std::string& portName));
    MOCK_METHOD1(Flush, ErrCode(const std::string& portName));
    MOCK_METHOD1(Drain, ErrCode(const std::string& portName));
    MOCK_METHOD1(SendBrkSignal, ErrCode(const std::string& portName));
    MOCK_METHOD2(SetRtsSignal, ErrCode(const std::string& portName, bool rts));
    MOCK_METHOD2(GetCtsSignal, ErrCode(const std::string& portName, bool& cts));
    MOCK_METHOD2(AddPortAuthorization, ErrCode(const std::string& authToken, const std::string& deviceId));
};

} // namespace Serial
} // namespace OHOS
#endif // MOCK_SERIAL_SERVER_H
