/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License"),
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

#include "ohos.busManager.serial.impl.hpp"
#include "ohos.busManager.serial.proj.hpp"
#include "taihe/runtime.hpp"

#include "hilog_wrapper.h"
#include "serial_error_code.h"
#include "serial_service_callback.h"
#include "serial_srv_client.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

using namespace OHOS;
using namespace OHOS::Serial;
using namespace taihe;

namespace {
using TaiheSerialPortInfo = ohos::busManager::serial::SerialPortInfo;
using TaiheSerialPort = ohos::busManager::serial::SerialPort;
using TaiheSerialConfigs = ohos::busManager::serial::SerialConfigs;

static SerialSrvClient& g_serialClient = SerialSrvClient::GetInstance();

constexpr int32_t DEFAULT_BAUD_RATE = 115200;
constexpr int32_t DEFAULT_DATA_BITS = 8;
constexpr int32_t DEFAULT_STOP_BITS = 1;
constexpr int32_t DEFAULT_PARITY = 0;
constexpr size_t WRITE_DATA_MAX_SIZE = 4096;
constexpr int32_t WRITE_TIMEOUT_MIN = 0;
constexpr int32_t WRITE_TIMEOUT_MAX = 300000;
constexpr int32_t VALID_DATA_BITS[] = {5, 6, 7, 8};
constexpr int32_t VALID_STOP_BITS[] = {1, 2};

struct CallbackEntry {
    taihe::callback<void(array_view<uint8_t>)> cb;
    ListenerHandle innerkitHandle;
};

static std::mutex g_callbackMutex;
static std::map<std::string, std::vector<CallbackEntry>> g_callbacks;
static std::map<std::string, ListenerHandle> g_innerkitHandles;
static std::mutex g_portCacheMutex;
static std::map<std::string, TaiheSerialPort> g_cachedPorts;

inline bool CheckAndThrowOnError(bool assertion, int errCode, const std::string& errMsg)
{
    if (!assertion) {
        SERIAL_HILOGI(MODULE_TAIHE, "Throw out ErrCode: [%{public}d], Msg: [%{public}s]", errCode, errMsg.c_str());
        taihe::set_business_error(errCode, errMsg.c_str());
        return false;
    }
    return true;
}

bool ConvertParityToInner(taihe::string_view parityStr, int32_t& outValue)
{
    static const std::map<std::string, int32_t> parityMap = {
        {"none", 0},
        {"even", 1},
        {"odd", 2},
        {"mark", 3},
        {"space", 4},
    };
    std::string str(parityStr);
    auto it = parityMap.find(str);
    if (it != parityMap.end()) {
        outValue = it->second;
        return true;
    }
    return false;
}

template <size_t N>
bool IsOneOf(int32_t value, const int32_t (&arr)[N])
{
    for (size_t i = 0; i < N; i++) {
        if (arr[i] == value) {
            return true;
        }
    }
    return false;
}

OHOS::Serial::SerialConfig MakeDefaultConfig()
{
    OHOS::Serial::SerialConfig cfg;
    cfg.baudRate = DEFAULT_BAUD_RATE;
    cfg.dataBits = DEFAULT_DATA_BITS;
    cfg.stopBits = DEFAULT_STOP_BITS;
    cfg.parity = DEFAULT_PARITY;
    cfg.rtscts = false;
    cfg.xon = false;
    cfg.xoff = false;
    cfg.xany = false;
    return cfg;
}

bool ApplyBaudRate(const TaiheSerialConfigs& c, int32_t& baudRate)
{
    if (!c.baudRate.has_value()) {
        return true;
    }
    baudRate = c.baudRate.value();
    return CheckAndThrowOnError(baudRate > 0, SERIAL_JS_ERR_INVALID_PARAM, "baudRate must be positive");
}

bool ApplyDataBits(const TaiheSerialConfigs& c, int32_t& dataBits)
{
    if (!c.dataBits.has_value()) {
        return true;
    }
    dataBits = c.dataBits.value().get_value();
    return CheckAndThrowOnError(
        IsOneOf(dataBits, VALID_DATA_BITS), SERIAL_JS_ERR_INVALID_PARAM, "dataBits must be one of {5, 6, 7, 8}");
}

bool ApplyStopBits(const TaiheSerialConfigs& c, int32_t& stopBits)
{
    if (!c.stopBits.has_value()) {
        return true;
    }
    stopBits = c.stopBits.value().get_value();
    return CheckAndThrowOnError(
        IsOneOf(stopBits, VALID_STOP_BITS), SERIAL_JS_ERR_INVALID_PARAM, "stopBits must be one of {1, 2}");
}

bool ApplyParity(const TaiheSerialConfigs& c, int32_t& parity)
{
    if (!c.parity.has_value()) {
        return true;
    }
    return CheckAndThrowOnError(ConvertParityToInner(c.parity.value().get_value(), parity), SERIAL_JS_ERR_INVALID_PARAM,
        "parity must be one of {none, even, odd, mark, space}");
}

bool BuildConfig(optional_view<TaiheSerialConfigs> config, OHOS::Serial::SerialConfig& outCfg)
{
    OHOS::Serial::SerialConfig cfg = MakeDefaultConfig();
    if (!config.has_value()) {
        outCfg = cfg;
        return true;
    }
    const TaiheSerialConfigs& c = config.value();
    if (!ApplyBaudRate(c, cfg.baudRate) || !ApplyDataBits(c, cfg.dataBits) || !ApplyStopBits(c, cfg.stopBits) ||
        !ApplyParity(c, cfg.parity)) {
        return false;
    }
    if (c.rtscts.has_value()) {
        cfg.rtscts = c.rtscts.value();
    }
    if (c.xon.has_value()) {
        cfg.xon = c.xon.value();
    }
    if (c.xoff.has_value()) {
        cfg.xoff = c.xoff.value();
    }
    if (c.xany.has_value()) {
        cfg.xany = c.xany.value();
    }
    outCfg = cfg;
    return true;
}

void RemoveAllCallbacksForPort(const std::string& portName)
{
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_callbacks.erase(portName);
    g_innerkitHandles.erase(portName);
}

void InvokeCallbacks(const std::string& portName, const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    auto it = g_callbacks.find(portName);
    if (it == g_callbacks.end()) {
        return;
    }
    taihe::array<uint8_t> taiheData(taihe::array_view<uint8_t>(const_cast<uint8_t*>(data.data()), data.size()));
    for (auto& entry : it->second) {
        entry.cb(taiheData);
    }
}

class SerialPortInfoHolder {
public:
    SerialPortInfoHolder(std::string portName, int32_t productId, int32_t vendorId, std::string manufacturer)
        : portName_(std::move(portName)),
          productId_(productId),
          vendorId_(vendorId),
          manufacturer_(std::move(manufacturer))
    {
    }

    taihe::string getPortName() const
    {
        return taihe::string(portName_);
    }

    taihe::optional<int32_t> getProductId() const
    {
        if (productId_ != 0) {
            return optional<int32_t>::make(productId_);
        }
        return optional<int32_t>();
    }

    taihe::optional<int32_t> getVendorId() const
    {
        if (vendorId_ != 0) {
            return optional<int32_t>::make(vendorId_);
        }
        return optional<int32_t>();
    }

    taihe::optional<taihe::string> getManufacturer() const
    {
        if (!manufacturer_.empty()) {
            return optional<taihe::string>::make(taihe::string(manufacturer_));
        }
        return optional<taihe::string>();
    }

private:
    std::string portName_;
    int32_t productId_;
    int32_t vendorId_;
    std::string manufacturer_;
};

class SerialPortHolder {
public:
    SerialPortHolder(std::string portName, int32_t productId, int32_t vendorId, std::string manufacturer)
        : portName_(std::move(portName)),
          productId_(productId),
          vendorId_(vendorId),
          manufacturer_(std::move(manufacturer))
    {
    }

    TaiheSerialPortInfo getPortInfo() const
    {
        SERIAL_HILOGI(MODULE_TAIHE, "getPortInfo for %{public}s", portName_.c_str());
        return taihe::make_holder<SerialPortInfoHolder, TaiheSerialPortInfo>(
            portName_, productId_, vendorId_, manufacturer_);
    }

    void open(optional_view<TaiheSerialConfigs> config)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "open start, portName=%{public}s", portName_.c_str());
        OHOS::Serial::SerialConfig cfg;
        if (!BuildConfig(config, cfg)) {
            return;
        }
        OHOS::Serial::SerialPortInfo innerInfo;
        innerInfo.portName = portName_;
        innerInfo.productId = productId_;
        innerInfo.vendorId = vendorId_;
        innerInfo.manufacturer = manufacturer_;
        int32_t ret = g_serialClient.OpenSerial(innerInfo, cfg);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "open failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "open finish.");
    }

    void close()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "close start, portName=%{public}s", portName_.c_str());
        RemoveAllCallbacksForPort(portName_);
        int32_t ret = g_serialClient.CloseSerial(portName_);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "close failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "close finish.");
    }

    int32_t write(array_view<uint8_t> data, optional_view<int32_t> timeout)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "write start, portName=%{public}s", portName_.c_str());
        if (!CheckAndThrowOnError(data.size() > 0 && data.size() <= WRITE_DATA_MAX_SIZE, SERIAL_JS_ERR_INVALID_PARAM,
                "data size must be in (0, 4096]")) {
            return 0;
        }
        int32_t timeoutVal = 0;
        if (timeout.has_value()) {
            timeoutVal = timeout.value();
        }
        if (!CheckAndThrowOnError(timeoutVal >= WRITE_TIMEOUT_MIN && timeoutVal <= WRITE_TIMEOUT_MAX,
                SERIAL_JS_ERR_INVALID_PARAM, "timeout must be in [0, 300000]")) {
            return 0;
        }
        std::vector<uint8_t> vecData(data.data(), data.data() + data.size());
        int32_t bytesWritten = 0;
        int32_t ret = g_serialClient.WriteSerial(portName_, vecData, timeoutVal, bytesWritten);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "write failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return 0;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "write finish, bytesWritten=%{public}d", bytesWritten);
        return bytesWritten;
    }

    void onDataRead(taihe::callback<void(array_view<uint8_t>)> callback)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "onDataRead start, portName=%{public}s", portName_.c_str());
        bool needRegister = false;
        {
            std::lock_guard<std::mutex> lock(g_callbackMutex);
            auto it = g_callbacks.find(portName_);
            if (it != g_callbacks.end()) {
                for (const auto& entry : it->second) {
                    if (entry.cb == callback) {
                        SERIAL_HILOGI(MODULE_TAIHE, "onDataRead callback already registered, skip");
                        return;
                    }
                }
            }
            needRegister = g_innerkitHandles.find(portName_) == g_innerkitHandles.end();
        }
        ListenerHandle innerkitHandle = 0;
        if (needRegister) {
            std::string name = portName_;
            innerkitHandle = g_serialClient.RegisterDataListener(name, [name](const std::vector<uint8_t>& data) {
                InvokeCallbacks(name, data);
            });
            if (innerkitHandle == 0) {
                SERIAL_HILOGE(MODULE_TAIHE, "RegisterDataListener failed for %{public}s", portName_.c_str());
                taihe::set_business_error(SERIAL_JS_ERR_SERVICE, SerialJsErrorMessage(SERIAL_JS_ERR_SERVICE));
                return;
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_callbackMutex);
            if (innerkitHandle != 0) {
                g_innerkitHandles[portName_] = innerkitHandle;
            } else {
                innerkitHandle = g_innerkitHandles[portName_];
            }
            CallbackEntry entry {callback, innerkitHandle};
            g_callbacks[portName_].push_back(entry);
        }
        SERIAL_HILOGI(
            MODULE_TAIHE, "onDataRead finish, handle=%{public}llu", static_cast<unsigned long long>(innerkitHandle));
    }

    void offDataRead(optional_view<taihe::callback<void(array_view<uint8_t>)>> callback)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "offDataRead start, portName=%{public}s", portName_.c_str());
        if (!callback.has_value()) {
            RemoveAllListeners();
            return;
        }
        ListenerHandle removedHandle = 0;
        bool portEmpty = false;
        if (!RemoveOneListener(callback.value(), removedHandle, portEmpty)) {
            return;
        }
        if (portEmpty && removedHandle != 0) {
            g_serialClient.UnregisterDataListener(portName_, removedHandle);
        }
        SERIAL_HILOGI(MODULE_TAIHE, "offDataRead finish.");
    }

    void flush()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "flush start, portName=%{public}s", portName_.c_str());
        int32_t ret = g_serialClient.Flush(portName_);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "flush failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "flush finish.");
    }

    void drain()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "drain start, portName=%{public}s", portName_.c_str());
        int32_t ret = g_serialClient.Drain(portName_);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "drain failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "drain finish.");
    }

    void setRts(bool enable)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "setRts start, portName=%{public}s", portName_.c_str());
        int32_t ret = g_serialClient.SetRtsSignal(portName_, enable);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "setRts failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "setRts finish.");
    }

    bool getCts()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "getCts start, portName=%{public}s", portName_.c_str());
        bool cts = false;
        int32_t ret = g_serialClient.GetCtsSignal(portName_, cts);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "getCts failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return false;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "getCts finish, cts=%{public}d", static_cast<int>(cts));
        return cts;
    }

    void sendBrk()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "sendBrk start, portName=%{public}s", portName_.c_str());
        int32_t ret = g_serialClient.SendBrkSignal(portName_);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "sendBrk failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "sendBrk finish.");
    }

private:
    void RemoveAllListeners()
    {
        ListenerHandle innerkitHandle = 0;
        {
            std::lock_guard<std::mutex> lock(g_callbackMutex);
            auto ihIt = g_innerkitHandles.find(portName_);
            if (ihIt != g_innerkitHandles.end()) {
                innerkitHandle = ihIt->second;
            }
            g_callbacks.erase(portName_);
            g_innerkitHandles.erase(portName_);
        }
        if (innerkitHandle != 0) {
            g_serialClient.UnregisterDataListener(portName_, innerkitHandle);
        }
        SERIAL_HILOGI(MODULE_TAIHE, "offDataRead removed all callbacks for %{public}s", portName_.c_str());
    }

    bool RemoveOneListener(
        taihe::callback<void(array_view<uint8_t>)> cb, ListenerHandle& removedHandle, bool& portEmpty)
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        portEmpty = false;
        auto it = g_callbacks.find(portName_);
        if (it == g_callbacks.end()) {
            SERIAL_HILOGW(MODULE_TAIHE, "offDataRead no callbacks for %{public}s", portName_.c_str());
            return false;
        }
        auto& entries = it->second;
        bool found = false;
        for (auto eit = entries.begin(); eit != entries.end(); ++eit) {
            if (eit->cb == cb) {
                removedHandle = eit->innerkitHandle;
                entries.erase(eit);
                found = true;
                break;
            }
        }
        if (!found) {
            SERIAL_HILOGW(MODULE_TAIHE, "offDataRead callback not found for %{public}s", portName_.c_str());
            return false;
        }
        if (entries.empty()) {
            portEmpty = true;
            g_callbacks.erase(it);
            g_innerkitHandles.erase(portName_);
        }
        return true;
    }

    std::string portName_;
    int32_t productId_;
    int32_t vendorId_;
    std::string manufacturer_;
};

array<TaiheSerialPort> GetSerialPortListSync()
{
    SERIAL_HILOGI(MODULE_TAIHE, "GetSerialPortListSync start.");
    std::vector<OHOS::Serial::SerialPortInfo> devices;
    int32_t ret = g_serialClient.QuerySerialDevices(devices);
    if (ret != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_TAIHE, "QuerySerialDevices failed, ret=%{public}d", ret);
        taihe::set_business_error(ret, SerialJsErrorMessage(ret));
        return array<TaiheSerialPort>(static_cast<TaiheSerialPort*>(nullptr), 0);
    }
    std::set<std::string> currentPorts;
    for (const auto& dev : devices) {
        currentPorts.insert(dev.portName);
    }
    {
        std::lock_guard<std::mutex> lock(g_portCacheMutex);
        for (auto it = g_cachedPorts.begin(); it != g_cachedPorts.end();) {
            if (currentPorts.find(it->first) == currentPorts.end()) {
                it = g_cachedPorts.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::vector<TaiheSerialPort> ports;
    for (const auto& dev : devices) {
        std::lock_guard<std::mutex> lock(g_portCacheMutex);
        auto cacheIt = g_cachedPorts.find(dev.portName);
        if (cacheIt != g_cachedPorts.end()) {
            ports.push_back(cacheIt->second);
        } else {
            auto port = taihe::make_holder<SerialPortHolder, TaiheSerialPort>(
                dev.portName, dev.productId, dev.vendorId, dev.manufacturer);
            g_cachedPorts.emplace(dev.portName, port);
            ports.push_back(port);
        }
    }
    SERIAL_HILOGI(MODULE_TAIHE, "GetSerialPortListSync finish, count=%{public}zu", ports.size());
    return array<TaiheSerialPort>(ports);
}

void AddPortAuthorizationSync(taihe::string_view tokenId, taihe::string_view deviceId)
{
    SERIAL_HILOGI(MODULE_TAIHE, "AddPortAuthorizationSync start.");
    std::string tok(tokenId);
    std::string dev(deviceId);
    int32_t ret = g_serialClient.AddPortAuthorization(tok, dev);
    if (ret != SERIAL_OK) {
        SERIAL_HILOGE(MODULE_TAIHE, "AddPortAuthorization failed, ret=%{public}d", ret);
        taihe::set_business_error(ret, SerialJsErrorMessage(ret));
        return;
    }
    SERIAL_HILOGI(MODULE_TAIHE, "AddPortAuthorizationSync finish.");
}

} // namespace

// NOLINTBEGIN
TH_EXPORT_CPP_API_getSerialPortListSync(GetSerialPortListSync);
TH_EXPORT_CPP_API_addPortAuthorizationSync(AddPortAuthorizationSync);
// NOLINTEND
