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

#include <cstdint>

#include "hilog_wrapper.h"
#include "interop_js/arkts_interop_js_api.h"
#include "napi/native_api.h"
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

struct CachedPortInfo {
    TaiheSerialPort port;
    int32_t productId;
    int32_t vendorId;
    std::string manufacturer;
};

static std::mutex g_callbackMutex;
static std::map<std::string, std::vector<CallbackEntry>> g_callbacks;
static std::map<std::string, ListenerHandle> g_innerkitHandles;
static std::mutex g_portCacheMutex;
static std::map<std::string, CachedPortInfo> g_cachedPorts;
static std::mutex g_napiEnvMutex;
static napi_env g_napiEnv = nullptr;
class SerialPortHolder;
static std::mutex g_holderRegistryMutex;
static std::multimap<std::string, SerialPortHolder*> g_holderRegistry;
static std::mutex g_disconnectedCallbackMutex;
static std::map<std::string, std::vector<taihe::callback<void()>>> g_disconnectedCallbacks;

void InvalidateAllHoldersForPort(const std::string& portName);

void EnsureNapiEnvStored()
{
    std::lock_guard<std::mutex> lock(g_napiEnvMutex);
    if (g_napiEnv != nullptr) {
        return;
    }
    ani_env* aniEnv = taihe::get_env();
    if (aniEnv == nullptr) {
        SERIAL_HILOGE(MODULE_TAIHE, "EnsureNapiEnvStored ani_env is null");
        return;
    }
    napi_env napiEnv = nullptr;
    if (!arkts_napi_scope_open(aniEnv, &napiEnv)) {
        SERIAL_HILOGE(MODULE_TAIHE, "EnsureNapiEnvStored arkts_napi_scope_open failed");
        return;
    }
    g_napiEnv = napiEnv;
    SERIAL_HILOGI(MODULE_TAIHE, "EnsureNapiEnvStored stored napiEnv=0x%{public}04x",
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(napiEnv) & 0xFFFF));
}

napi_env GetNapiEnv()
{
    std::lock_guard<std::mutex> lock(g_napiEnvMutex);
    return g_napiEnv;
}

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

bool IsPortInfoEqual(const CachedPortInfo& cached, const OHOS::Serial::SerialPortInfo& info)
{
    return cached.productId == info.productId && cached.vendorId == info.vendorId &&
        cached.manufacturer == info.manufacturer;
}

void RemoveAllCallbacksForPort(const std::string& portName)
{
    ListenerHandle handle = 0;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        auto ihIt = g_innerkitHandles.find(portName);
        if (ihIt != g_innerkitHandles.end()) {
            handle = ihIt->second;
        }
        g_callbacks.erase(portName);
        g_innerkitHandles.erase(portName);
        g_disconnectedCallbacks.erase(portName);
    }
    if (handle != 0) {
        g_serialClient.UnregisterDataListener(portName, handle);
    }
    SERIAL_HILOGI(MODULE_TAIHE, "RemoveAllCallbacksForPort %{public}s, handle=%{public}llu", portName.c_str(),
        static_cast<unsigned long long>(handle));
}

void QueueDisconnectedCallbacks(
    napi_env env, napi_value resourceName, std::vector<taihe::callback<void()>>&& callbacksCopy)
{
    napi_async_work asyncWork = nullptr;
    auto* workData = new (std::nothrow) std::vector<taihe::callback<void()>>(std::move(callbacksCopy));
    if (workData == nullptr) {
        SERIAL_HILOGE(MODULE_TAIHE, "InvokeDisconnectedCallbacks alloc failed");
        return;
    }
    napi_create_async_work(
        env, nullptr, resourceName, [](napi_env env, void* data) {},
        [](napi_env env, napi_status status, void* data) {
            auto* cbs = static_cast<std::vector<taihe::callback<void()>>*>(data);
            for (size_t i = 0; i < cbs->size(); i++) {
                (*cbs)[i]();
            }
            delete cbs;
        },
        workData, &asyncWork);
    if (asyncWork != nullptr) {
        napi_status qStatus = napi_queue_async_work(env, asyncWork);
        if (qStatus != napi_ok) {
            SERIAL_HILOGE(MODULE_TAIHE, "InvokeDisconnectedCallbacks queue failed");
            napi_delete_async_work(env, asyncWork);
            delete workData;
        }
    } else {
        SERIAL_HILOGE(MODULE_TAIHE, "InvokeDisconnectedCallbacks create work failed");
        delete workData;
    }
}

void InvokeDisconnectedCallbacks(const std::string& portName)
{
    std::vector<taihe::callback<void()>> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(g_disconnectedCallbackMutex);
        auto it = g_disconnectedCallbacks.find(portName);
        if (it == g_disconnectedCallbacks.end()) {
            SERIAL_HILOGW(MODULE_TAIHE, "InvokeDisconnectedCallbacks no callbacks for %{public}s", portName.c_str());
            return;
        }
        for (const auto& cb : it->second) {
            callbacksCopy.push_back(cb);
        }
    }
    SERIAL_HILOGI(MODULE_TAIHE, "InvokeDisconnectedCallbacks port=%{public}s, count=%{public}zu", portName.c_str(),
        callbacksCopy.size());
    napi_env env = GetNapiEnv();
    if (env == nullptr) {
        SERIAL_HILOGE(MODULE_TAIHE, "InvokeDisconnectedCallbacks napi_env is null, port=%{public}s", portName.c_str());
        return;
    }
    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "serialDisconnected", NAPI_AUTO_LENGTH, &resourceName);
    if (resourceName == nullptr) {
        SERIAL_HILOGE(MODULE_TAIHE, "InvokeDisconnectedCallbacks create string failed");
        return;
    }
    QueueDisconnectedCallbacks(env, resourceName, std::move(callbacksCopy));
}

struct CallbackWorkData {
    std::string portName;
    napi_async_work asyncWork = nullptr;
    std::vector<taihe::callback<void(array_view<uint8_t>)>> callbacks;
    std::vector<uint8_t> data;
};

void ScheduleCallbackAsyncWork(napi_env env, CallbackWorkData* workData)
{
    napi_value resourceName = nullptr;
    napi_status status = napi_create_string_utf8(env, "serialDataRead", NAPI_AUTO_LENGTH, &resourceName);
    if (status != napi_ok || resourceName == nullptr) {
        SERIAL_HILOGE(MODULE_TAIHE, "ScheduleCallbackAsyncWork create string failed");
        delete workData;
        return;
    }
    napi_async_work asyncWork = nullptr;
    napi_create_async_work(
        env, nullptr, resourceName, [](napi_env env, void* data) {},
        [](napi_env env, napi_status status, void* data) {
            auto* wd = static_cast<CallbackWorkData*>(data);
            SERIAL_HILOGI(MODULE_TAIHE,
                "CallbackWork complete, port=%{public}s, dataSize=%{public}zu, count=%{public}zu", wd->portName.c_str(),
                wd->data.size(), wd->callbacks.size());
            taihe::array<uint8_t> taiheData(array_view<uint8_t>(wd->data.data(), wd->data.size()));
            for (size_t i = 0; i < wd->callbacks.size(); i++) {
                wd->callbacks[i](taiheData);
            }
            napi_delete_async_work(env, wd->asyncWork);
            delete wd;
        },
        workData, &asyncWork);
    if (asyncWork != nullptr) {
        workData->asyncWork = asyncWork;
        napi_status qStatus = napi_queue_async_work(env, asyncWork);
        if (qStatus != napi_ok) {
            SERIAL_HILOGE(MODULE_TAIHE, "ScheduleCallbackAsyncWork queue failed");
            napi_delete_async_work(env, asyncWork);
            delete workData;
        }
    } else {
        SERIAL_HILOGE(MODULE_TAIHE, "ScheduleCallbackAsyncWork create work failed");
        delete workData;
    }
}

void InvokeCallbacks(const std::string& portName, const std::vector<uint8_t>& data)
{
    std::vector<taihe::callback<void(array_view<uint8_t>)>> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        auto it = g_callbacks.find(portName);
        if (it == g_callbacks.end()) {
            SERIAL_HILOGW(MODULE_TAIHE, "InvokeCallbacks no callbacks for %{public}s", portName.c_str());
            return;
        }
        for (const auto& entry : it->second) {
            callbacksCopy.push_back(entry.cb);
        }
    }
    SERIAL_HILOGI(MODULE_TAIHE, "InvokeCallbacks port=%{public}s, dataSize=%{public}zu, count=%{public}zu",
        portName.c_str(), data.size(), callbacksCopy.size());
    napi_env env = GetNapiEnv();
    if (env == nullptr) {
        SERIAL_HILOGE(MODULE_TAIHE, "InvokeCallbacks napi_env is null, port=%{public}s", portName.c_str());
        return;
    }
    auto* workData = new (std::nothrow) CallbackWorkData();
    if (workData == nullptr) {
        SERIAL_HILOGE(MODULE_TAIHE, "InvokeCallbacks alloc failed, port=%{public}s", portName.c_str());
        return;
    }
    workData->portName = portName;
    workData->callbacks = std::move(callbacksCopy);
    workData->data = data;
    ScheduleCallbackAsyncWork(env, workData);
}

bool CheckCallbackDuplicate(const std::string& portName, const taihe::callback<void(array_view<uint8_t>)>& callback,
    bool& needRegister, ListenerHandle& existingHandle)
{
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    auto it = g_callbacks.find(portName);
    if (it != g_callbacks.end()) {
        for (const auto& entry : it->second) {
            if (entry.cb == callback) {
                return true;
            }
        }
    }
    auto ihIt = g_innerkitHandles.find(portName);
    if (ihIt != g_innerkitHandles.end()) {
        existingHandle = ihIt->second;
    } else {
        needRegister = true;
    }
    return false;
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
          manufacturer_(std::move(manufacturer)),
          isOpened_(false),
          invalidated_(false)
    {
        std::lock_guard<std::mutex> lock(g_holderRegistryMutex);
        g_holderRegistry.emplace(portName_, this);
    }

    ~SerialPortHolder()
    {
        std::lock_guard<std::mutex> lock(g_holderRegistryMutex);
        auto range = g_holderRegistry.equal_range(portName_);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == this) {
                g_holderRegistry.erase(it);
                break;
            }
        }
    }

    void setInvalidated(bool value)
    {
        invalidated_ = value;
    }

    bool isInvalidated() const
    {
        return invalidated_;
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
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
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
        isOpened_ = true;
        SERIAL_HILOGI(MODULE_TAIHE, "open finish.");
    }

    void close()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "close start, portName=%{public}s", portName_.c_str());
        RemoveAllCallbacksForPort(portName_);
        int32_t ret = g_serialClient.CloseSerial(portName_);
        isOpened_ = false;
        if (ret == SERIAL_OK) {
            SERIAL_HILOGI(MODULE_TAIHE, "close finish.");
            return;
        }
        SERIAL_HILOGE(MODULE_TAIHE, "close failed, ret=%{public}d", ret);
        int32_t errCode = (ret == SERIAL_JS_ERR_DEVICE_NOT_OPENED || ret == SERIAL_JS_ERR_DEVICE_DISCONNECTED) ?
            SERIAL_JS_ERR_DEVICE_NOT_OPENED :
            SERIAL_JS_ERR_SERVICE;
        taihe::set_business_error(errCode, SerialJsErrorMessage(errCode));
    }

    int32_t write(array_view<uint8_t> data, optional_view<int32_t> timeout)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "write start, portName=%{public}s", portName_.c_str());
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_NOT_OPENED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_NOT_OPENED));
            return 0;
        }
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

    ListenerHandle DoRegisterListener(
        bool needRegister, ListenerHandle existingHandle, taihe::callback<void(array_view<uint8_t>)> callback)
    {
        ListenerHandle finalHandle = existingHandle;
        if (needRegister) {
            ListenerHandle newHandle = 0;
            std::string name = portName_;
            int32_t regRet = g_serialClient.RegisterDataListener(
                name,
                [name](const std::vector<uint8_t>& data) {
                    InvokeCallbacks(name, data);
                },
                newHandle);
            if (regRet != SERIAL_OK || newHandle == 0) {
                SERIAL_HILOGE(MODULE_TAIHE, "RegisterDataListener failed for %{public}s, ret=%{public}d",
                    portName_.c_str(), regRet);
                taihe::set_business_error(regRet, SerialJsErrorMessage(regRet));
                return 0;
            }
            {
                std::lock_guard<std::mutex> lock(g_callbackMutex);
                if (g_innerkitHandles.find(portName_) != g_innerkitHandles.end()) {
                    g_serialClient.UnregisterDataListener(portName_, newHandle);
                    finalHandle = g_innerkitHandles[portName_];
                } else {
                    g_innerkitHandles[portName_] = newHandle;
                    finalHandle = newHandle;
                }
                CallbackEntry entry {callback, finalHandle};
                g_callbacks[portName_].push_back(entry);
            }
        } else {
            std::lock_guard<std::mutex> lock(g_callbackMutex);
            CallbackEntry entry {callback, finalHandle};
            g_callbacks[portName_].push_back(entry);
        }
        return finalHandle;
    }

    void onDataRead(taihe::callback<void(array_view<uint8_t>)> callback)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "onDataRead start, portName=%{public}s", portName_.c_str());
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
        if (!isOpened_) {
            SERIAL_HILOGE(MODULE_TAIHE, "onDataRead failed: port not opened, portName=%{public}s", portName_.c_str());
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_NOT_OPENED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_NOT_OPENED));
            return;
        }
        EnsureNapiEnvStored();
        bool needRegister = false;
        ListenerHandle existingHandle = 0;
        if (CheckCallbackDuplicate(portName_, callback, needRegister, existingHandle)) {
            SERIAL_HILOGI(MODULE_TAIHE, "onDataRead callback already registered, skip");
            return;
        }

        ListenerHandle finalHandle = DoRegisterListener(needRegister, existingHandle, callback);
        if (finalHandle == 0) {
            return;
        }

        SERIAL_HILOGI(
            MODULE_TAIHE, "onDataRead finish, handle=%{public}llu", static_cast<unsigned long long>(finalHandle));
    }

    void offDataRead(optional_view<taihe::callback<void(array_view<uint8_t>)>> callback)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "offDataRead start, portName=%{public}s", portName_.c_str());
        if (!isOpened_) {
            SERIAL_HILOGE(MODULE_TAIHE, "offDataRead failed: port not opened, portName=%{public}s", portName_.c_str());
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_NOT_OPENED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_NOT_OPENED));
            return;
        }
        if (!callback.has_value()) {
            ListenerHandle handle = 0;
            {
                std::lock_guard<std::mutex> lock(g_callbackMutex);
                auto ihIt = g_innerkitHandles.find(portName_);
                if (ihIt != g_innerkitHandles.end()) {
                    handle = ihIt->second;
                }
                g_callbacks.erase(portName_);
                g_innerkitHandles.erase(portName_);
            }
            if (handle != 0) {
                int32_t ret = g_serialClient.UnregisterDataListener(portName_, handle);
                if (ret != SERIAL_OK && ret != SERIAL_JS_ERR_DEVICE_DISCONNECTED) {
                    SERIAL_HILOGE(MODULE_TAIHE, "offDataRead UnregisterDataListener failed, ret=%{public}d", ret);
                    taihe::set_business_error(SERIAL_JS_ERR_SERVICE, SerialJsErrorMessage(SERIAL_JS_ERR_SERVICE));
                    return;
                }
            }
            SERIAL_HILOGI(MODULE_TAIHE, "offDataRead finish.");
            return;
        }
        ListenerHandle removedHandle = 0;
        bool portEmpty = false;
        if (!RemoveOneListener(callback.value(), removedHandle, portEmpty)) {
            return;
        }
        if (portEmpty && removedHandle != 0) {
            int32_t ret = g_serialClient.UnregisterDataListener(portName_, removedHandle);
            if (ret != SERIAL_OK && ret != SERIAL_JS_ERR_DEVICE_DISCONNECTED) {
                SERIAL_HILOGE(MODULE_TAIHE, "offDataRead UnregisterDataListener failed, ret=%{public}d", ret);
                taihe::set_business_error(SERIAL_JS_ERR_SERVICE, SerialJsErrorMessage(SERIAL_JS_ERR_SERVICE));
                return;
            }
        }
        SERIAL_HILOGI(MODULE_TAIHE, "offDataRead finish.");
    }

    void flush()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "flush start, portName=%{public}s", portName_.c_str());
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
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
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
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
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
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
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return false;
        }
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
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
        int32_t ret = g_serialClient.SendBrkSignal(portName_);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "sendBrk failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "sendBrk finish.");
    }

    void setDtr(bool enable)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "setDtr start, portName=%{public}s", portName_.c_str());
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
        int32_t ret = g_serialClient.SetDtrSignal(portName_, enable);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "setDtr failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "setDtr finish.");
    }

    bool getDsr()
    {
        SERIAL_HILOGI(MODULE_TAIHE, "getDsr start, portName=%{public}s", portName_.c_str());
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return false;
        }
        bool dsr = false;
        int32_t ret = g_serialClient.GetDsrSignal(portName_, dsr);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "getDsr failed, ret=%{public}d", ret);
            taihe::set_business_error(ret, SerialJsErrorMessage(ret));
            return false;
        }
        SERIAL_HILOGI(MODULE_TAIHE, "getDsr finish, dsr=%{public}d", static_cast<int>(dsr));
        return dsr;
    }

    void onDisconnected(taihe::callback<void()> callback)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "onDisconnected start, portName=%{public}s", portName_.c_str());
        if (invalidated_) {
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
            return;
        }
        if (!isOpened_) {
            SERIAL_HILOGE(
                MODULE_TAIHE, "onDisconnected failed: port not opened, portName=%{public}s", portName_.c_str());
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_NOT_OPENED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_NOT_OPENED));
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_disconnectedCallbackMutex);
            auto& callbacks = g_disconnectedCallbacks[portName_];
            for (const auto& existing : callbacks) {
                if (existing == callback) {
                    SERIAL_HILOGI(MODULE_TAIHE, "onDisconnected callback already registered, skip");
                    return;
                }
            }
            callbacks.push_back(callback);
        }
        ListenerHandle handle = 0;
        std::string name = portName_;
        int32_t regRet = g_serialClient.RegisterDisconnectedListener(
            name,
            [name](const std::string&) {
                InvokeDisconnectedCallbacks(name);
            },
            handle);
        if (regRet != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_TAIHE, "RegisterDisconnectedListener failed, ret=%{public}d", regRet);
            {
                std::lock_guard<std::mutex> lock(g_disconnectedCallbackMutex);
                auto& callbacks = g_disconnectedCallbacks[portName_];
                for (auto it = callbacks.begin(); it != callbacks.end(); ++it) {
                    if (*it == callback) {
                        callbacks.erase(it);
                        break;
                    }
                }
            }
            taihe::set_business_error(regRet, SerialJsErrorMessage(regRet));
            return;
        }
        SERIAL_HILOGI(
            MODULE_TAIHE, "onDisconnected finish, handle=%{public}llu", static_cast<unsigned long long>(handle));
    }

    void offDisconnected(optional_view<taihe::callback<void()>> callback)
    {
        SERIAL_HILOGI(MODULE_TAIHE, "offDisconnected start, portName=%{public}s", portName_.c_str());
        if (!isOpened_) {
            SERIAL_HILOGE(
                MODULE_TAIHE, "offDisconnected failed: port not opened, portName=%{public}s", portName_.c_str());
            taihe::set_business_error(
                SERIAL_JS_ERR_DEVICE_NOT_OPENED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_NOT_OPENED));
            return;
        }
        if (!callback.has_value()) {
            std::lock_guard<std::mutex> lock(g_disconnectedCallbackMutex);
            g_disconnectedCallbacks.erase(portName_);
            SERIAL_HILOGI(MODULE_TAIHE, "offDisconnected finish.");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_disconnectedCallbackMutex);
            auto it = g_disconnectedCallbacks.find(portName_);
            if (it == g_disconnectedCallbacks.end()) {
                SERIAL_HILOGW(MODULE_TAIHE, "offDisconnected no callbacks for %{public}s", portName_.c_str());
                return;
            }
            auto& entries = it->second;
            bool found = false;
            for (auto eit = entries.begin(); eit != entries.end(); ++eit) {
                if (*eit == callback.value()) {
                    entries.erase(eit);
                    found = true;
                    break;
                }
            }
            if (!found) {
                SERIAL_HILOGW(MODULE_TAIHE, "offDisconnected callback not found for %{public}s", portName_.c_str());
            }
            if (entries.empty()) {
                g_disconnectedCallbacks.erase(it);
            }
        }
        SERIAL_HILOGI(MODULE_TAIHE, "offDisconnected finish.");
    }

private:
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
    bool isOpened_;
    bool invalidated_;
};

void InvalidateAllHoldersForPort(const std::string& portName)
{
    std::lock_guard<std::mutex> lock(g_holderRegistryMutex);
    auto range = g_holderRegistry.equal_range(portName);
    for (auto it = range.first; it != range.second; ++it) {
        it->second->setInvalidated(true);
    }
}

void UpdateCachedPorts(const std::vector<OHOS::Serial::SerialPortInfo>& devices,
    const std::set<std::string>& currentPorts, std::vector<std::string>& disappearedPorts,
    std::vector<std::string>& invalidatedPorts, std::vector<TaiheSerialPort>& ports)
{
    std::lock_guard<std::mutex> lock(g_portCacheMutex);
    for (auto it = g_cachedPorts.begin(); it != g_cachedPorts.end();) {
        if (currentPorts.find(it->first) == currentPorts.end()) {
            disappearedPorts.push_back(it->first);
            it = g_cachedPorts.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& dev : devices) {
        auto cacheIt = g_cachedPorts.find(dev.portName);
        if (cacheIt != g_cachedPorts.end()) {
            if (IsPortInfoEqual(cacheIt->second, dev)) {
                ports.push_back(cacheIt->second.port);
                continue;
            }
            SERIAL_HILOGI(MODULE_TAIHE, "GetSerialPortListSync info changed for %{public}s", dev.portName.c_str());
            invalidatedPorts.push_back(dev.portName);
            g_cachedPorts.erase(cacheIt);
        }
        auto port = taihe::make_holder<SerialPortHolder, TaiheSerialPort>(
            dev.portName, dev.productId, dev.vendorId, dev.manufacturer);
        CachedPortInfo info {port, dev.productId, dev.vendorId, dev.manufacturer};
        g_cachedPorts.emplace(dev.portName, std::move(info));
        ports.push_back(port);
    }
}

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
    std::vector<std::string> disappearedPorts;
    std::vector<std::string> invalidatedPorts;
    std::vector<TaiheSerialPort> ports;
    UpdateCachedPorts(devices, currentPorts, disappearedPorts, invalidatedPorts, ports);
    for (const auto& name : disappearedPorts) {
        SERIAL_HILOGI(MODULE_TAIHE, "GetSerialPortListSync port disappeared: %{public}s", name.c_str());
        InvalidateAllHoldersForPort(name);
        RemoveAllCallbacksForPort(name);
    }
    for (const auto& name : invalidatedPorts) {
        SERIAL_HILOGI(MODULE_TAIHE, "GetSerialPortListSync port info changed: %{public}s", name.c_str());
        InvalidateAllHoldersForPort(name);
        RemoveAllCallbacksForPort(name);
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
