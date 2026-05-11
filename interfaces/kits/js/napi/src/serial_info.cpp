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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "hilog_wrapper.h"
#include "napi/native_api.h"
#include "napi/native_node_api.h"
#include "napi_common.h"
#include "securec.h"
#include "serial_error_code.h"
#include "serial_server_types.h"
#include "serial_srv_client.h"

using namespace OHOS;
using namespace OHOS::Serial;

namespace {
constexpr size_t PORT_NAME_BUF_SIZE = 256;
constexpr int32_t DEFAULT_BAUD_RATE = 115200;
constexpr int32_t DEFAULT_DATA_BITS = 8;
constexpr int32_t DEFAULT_STOP_BITS = 1;
constexpr int32_t DEFAULT_PARITY = 0;
constexpr size_t PARITY_BUF_SIZE = 16;

constexpr int32_t PARITY_NONE = 0;
constexpr int32_t PARITY_EVEN = 1;
constexpr int32_t PARITY_ODD = 2;
constexpr int32_t PARITY_MARK = 3;
constexpr int32_t PARITY_SPACE = 4;

constexpr size_t DATA_READ_CALLBACK_ARGC = 1;
constexpr size_t PORT_NAME_PROP_COUNT = 1;
constexpr uint32_t NAPI_REF_COUNT_STRONG = 1;
constexpr uint32_t NAPI_REF_COUNT_WEAK = 0;
constexpr size_t WRITE_MAX_ARGC = 2;
constexpr size_t ADD_AUTH_MAX_ARGC = 2;

constexpr int32_t DATA_BITS_FIVE = 5;
constexpr int32_t DATA_BITS_SIX = 6;
constexpr int32_t DATA_BITS_SEVEN = 7;
constexpr int32_t DATA_BITS_EIGHT = 8;
constexpr int32_t STOP_BITS_ONE = 1;
constexpr int32_t STOP_BITS_TWO = 2;

constexpr size_t WRITE_DATA_MAX_SIZE = 4096;
constexpr int32_t WRITE_TIMEOUT_MIN = 0;
constexpr int32_t WRITE_TIMEOUT_MAX = 300000;
constexpr int32_t VALID_DATA_BITS[] = {DATA_BITS_FIVE, DATA_BITS_SIX, DATA_BITS_SEVEN, DATA_BITS_EIGHT};
constexpr int32_t VALID_STOP_BITS[] = {STOP_BITS_ONE, STOP_BITS_TWO};
} // namespace

constexpr ListenerHandle INVALID_LISTENER_HANDLE = 0;

static SerialSrvClient& g_serialClient = SerialSrvClient::GetInstance();

struct AsyncCallbackContext {
    napi_env env;
    napi_async_work asyncWork;
    napi_deferred deferred;
    int32_t ret;
};

struct SerialPortListAsyncContext : AsyncCallbackContext {
    std::vector<SerialPortInfo> deviceList;
};

struct SerialOpenAsyncContext : AsyncCallbackContext {
    SerialPortInfo portInfo;
    SerialConfig config;
};

struct SerialCloseAsyncContext : AsyncCallbackContext {
    std::string portName;
};

struct SerialWriteAsyncContext : AsyncCallbackContext {
    std::string portName;
    std::vector<uint8_t> data;
    int32_t timeout;
    int32_t bytesWritten;
};

struct SerialFlushAsyncContext : AsyncCallbackContext {
    std::string portName;
};

struct SerialDrainAsyncContext : AsyncCallbackContext {
    std::string portName;
};

struct SerialSendBrkAsyncContext : AsyncCallbackContext {
    std::string portName;
};

struct SerialSetRtsAsyncContext : AsyncCallbackContext {
    std::string portName;
    bool rts;
};

struct SerialGetCtsAsyncContext : AsyncCallbackContext {
    std::string portName;
    bool cts;
};

struct SerialAddPortAuthAsyncContext : AsyncCallbackContext {
    std::string tokenId;
    std::string deviceId;
};

struct CallbackData {
    std::vector<uint8_t> data;
    napi_ref callbackRef;
};

struct CallbackInfo {
    napi_ref callbackRef;
    napi_env env;
    ListenerHandle listenerHandle;
    napi_threadsafe_function tsfn;
};

static std::mutex g_callbackMutex;
static std::map<std::string, std::map<napi_ref, CallbackInfo>> g_dataCallbacks;

struct SerialPortNative {
    std::string portName;
    SerialPortInfo info;
    bool invalidated = false;
    bool isOpened = false;
    napi_ref objectRef = nullptr;
    napi_env env = nullptr;
};

static std::mutex g_portCacheMutex;
static std::map<std::string, SerialPortNative*> g_activePorts;
static std::vector<SerialPortNative*> g_invalidatedPorts;

static bool IsSerialPortInfoEqual(const SerialPortInfo& a, const SerialPortInfo& b)
{
    return a.portName == b.portName && a.productId == b.productId && a.vendorId == b.vendorId &&
        a.manufacturer == b.manufacturer;
}

static void RemoveAllListenersForPort(napi_env env, const std::string& portName)
{
    std::vector<std::pair<napi_ref, ListenerHandle>> refsAndHandles;
    std::vector<napi_threadsafe_function> tsfnsToRelease;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        auto portIt = g_dataCallbacks.find(portName);
        if (portIt != g_dataCallbacks.end()) {
            for (auto& pair : portIt->second) {
                refsAndHandles.push_back({pair.second.callbackRef, pair.second.listenerHandle});
                tsfnsToRelease.push_back(pair.second.tsfn);
            }
            g_dataCallbacks.erase(portIt);
        }
    }
    for (auto& rh : refsAndHandles) {
        g_serialClient.UnregisterDataListener(portName, rh.second);
    }
    for (auto& rh : refsAndHandles) {
        napi_delete_reference(env, rh.first);
    }
    for (auto& tsfn : tsfnsToRelease) {
        napi_release_threadsafe_function(tsfn, napi_tsfn_release);
    }
}

static void SerialPortFinalize(napi_env env, void* data, void* hint)
{
    SerialPortNative* native = static_cast<SerialPortNative*>(data);
    if (native == nullptr) {
        return;
    }
    SERIAL_HILOGI(MODULE_JS_NAPI, "[LIFECYCLE] RECYCLE(GC): portName=%{public}s, wasInvalidated=%{public}d",
        native->portName.c_str(), native->invalidated ? 1 : 0);
    {
        std::lock_guard<std::mutex> lock(g_portCacheMutex);
        auto activeIt = g_activePorts.find(native->portName);
        if (activeIt != g_activePorts.end() && activeIt->second == native) {
            g_activePorts.erase(activeIt);
        }
        auto invalidIt = std::find(g_invalidatedPorts.begin(), g_invalidatedPorts.end(), native);
        if (invalidIt != g_invalidatedPorts.end()) {
            g_invalidatedPorts.erase(invalidIt);
        }
    }
    if (native->objectRef != nullptr) {
        napi_delete_reference(env, native->objectRef);
        native->objectRef = nullptr;
    }
    delete native;
}

static napi_value CreateBusinessError(napi_env env, int32_t code, const char* message)
{
    napi_value errorCode = nullptr;
    napi_value errorMessage = nullptr;
    napi_create_int32(env, code, &errorCode);
    napi_create_string_utf8(env, message, NAPI_AUTO_LENGTH, &errorMessage);
    napi_value error = nullptr;
    napi_create_error(env, errorCode, errorMessage, &error);
    return error;
}

static void ThrowBusinessError(napi_env env, int32_t code, const char* message)
{
    napi_value error = CreateBusinessError(env, code, message);
    napi_throw(env, error);
}

static bool IsPortInvalidated(napi_env env, napi_value thisVar)
{
    void* nativePtr = nullptr;
    napi_unwrap(env, thisVar, &nativePtr);
    SerialPortNative* native = static_cast<SerialPortNative*>(nativePtr);
    if (native != nullptr && native->invalidated) {
        SERIAL_HILOGW(MODULE_JS_NAPI, "[IsPortInvalidated] Operation on invalidated port: portName=%{public}s",
            native->portName.c_str());
        return true;
    }
    return false;
}

static napi_value CreateRejectedPromise(napi_env env, int32_t errCode)
{
    napi_value promise = nullptr;
    napi_deferred deferred = nullptr;
    napi_create_promise(env, &deferred, &promise);
    napi_value error = CreateBusinessError(env, errCode, SerialJsErrorMessage(errCode));
    napi_reject_deferred(env, deferred, error);
    return promise;
}

static bool CheckPortOpened(napi_env env, napi_value thisVar)
{
    void* nativePtr = nullptr;
    napi_unwrap(env, thisVar, &nativePtr);
    SerialPortNative* native = static_cast<SerialPortNative*>(nativePtr);
    if (native != nullptr && !native->isOpened) {
        SERIAL_HILOGW(
            MODULE_JS_NAPI, "[CheckPortOpened] Port not opened: portName=%{public}s", native->portName.c_str());
        ThrowBusinessError(env, SERIAL_JS_ERR_DEVICE_NOT_OPENED, "Port not open");
        return false;
    }
    return true;
}

static void InvokeJsCallback(napi_env env, napi_ref callbackRef, const std::vector<uint8_t>& data)
{
    napi_value jsCallback = nullptr;
    napi_get_reference_value(env, callbackRef, &jsCallback);
    if (jsCallback == nullptr) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[InvokeJsCallback] Failed to get callback reference");
        return;
    }

    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);

    void* bufferPtr = nullptr;
    napi_value arrayBuffer = nullptr;
    napi_create_arraybuffer(env, data.size(), &bufferPtr, &arrayBuffer);
    if (bufferPtr != nullptr && !data.empty()) {
        errno_t err = memcpy_s(bufferPtr, data.size(), data.data(), data.size());
        if (err != EOK) {
            SERIAL_HILOGE(MODULE_JS_NAPI, "[InvokeJsCallback] memcpy_s failed");
            return;
        }
    }

    napi_value uint8Array = nullptr;
    napi_create_typedarray(env, napi_uint8_array, data.size(), arrayBuffer, 0, &uint8Array);

    napi_value callResult = nullptr;
    napi_call_function(env, undefined, jsCallback, DATA_READ_CALLBACK_ARGC, &uint8Array, &callResult);
}

static napi_value SerialOpenNapi(napi_env env, napi_callback_info info);
static napi_value SerialCloseNapi(napi_env env, napi_callback_info info);
static napi_value SerialWriteNapi(napi_env env, napi_callback_info info);
static napi_value SerialOnDataReadNapi(napi_env env, napi_callback_info info);
static napi_value SerialOffDataReadNapi(napi_env env, napi_callback_info info);
static napi_value SerialFlushNapi(napi_env env, napi_callback_info info);
static napi_value SerialDrainNapi(napi_env env, napi_callback_info info);
static napi_value SerialSetRtsNapi(napi_env env, napi_callback_info info);
static napi_value SerialSendBrkNapi(napi_env env, napi_callback_info info);
static napi_value SerialGetCtsNapi(napi_env env, napi_callback_info info);

static napi_value CreateSerialPortInfoObject(napi_env env, const SerialPortInfo& deviceInfo)
{
    napi_value portInfoObj;
    napi_create_object(env, &portInfoObj);

    napi_value portNameValue;
    napi_create_string_utf8(env, deviceInfo.portName.c_str(), deviceInfo.portName.length(), &portNameValue);
    napi_set_named_property(env, portInfoObj, "portName", portNameValue);

    if (!deviceInfo.manufacturer.empty()) {
        napi_value manufacturerValue;
        napi_create_string_utf8(
            env, deviceInfo.manufacturer.c_str(), deviceInfo.manufacturer.length(), &manufacturerValue);
        napi_set_named_property(env, portInfoObj, "manufacturer", manufacturerValue);
    }

    if (deviceInfo.productId != 0) {
        napi_value productIdValue;
        napi_create_int32(env, deviceInfo.productId, &productIdValue);
        napi_set_named_property(env, portInfoObj, "productId", productIdValue);
    }

    if (deviceInfo.vendorId != 0) {
        napi_value vendorIdValue;
        napi_create_int32(env, deviceInfo.vendorId, &vendorIdValue);
        napi_set_named_property(env, portInfoObj, "vendorId", vendorIdValue);
    }

    return portInfoObj;
}

static void SetNamedFunction(napi_env env, napi_value obj, const char* name, napi_callback cb)
{
    napi_value func;
    napi_create_function(env, name, NAPI_AUTO_LENGTH, cb, nullptr, &func);
    napi_set_named_property(env, obj, name, func);
}

static napi_value SerialPortToJSObject(napi_env env, const SerialPortInfo& deviceInfo, SerialPortNative* native)
{
    napi_value serialPortObj;
    napi_create_object(env, &serialPortObj);

    napi_value portNameValue;
    napi_create_string_utf8(env, deviceInfo.portName.c_str(), deviceInfo.portName.length(), &portNameValue);
    napi_property_descriptor portNameProp = {"portName", nullptr, nullptr, nullptr, nullptr, portNameValue,
        static_cast<napi_property_attributes>(napi_writable | napi_configurable), nullptr};
    napi_define_properties(env, serialPortObj, PORT_NAME_PROP_COUNT, &portNameProp);

    napi_value portInfoObj = CreateSerialPortInfoObject(env, deviceInfo);
    napi_set_named_property(env, serialPortObj, "portInfo", portInfoObj);

    SetNamedFunction(env, serialPortObj, "open", SerialOpenNapi);
    SetNamedFunction(env, serialPortObj, "close", SerialCloseNapi);
    SetNamedFunction(env, serialPortObj, "write", SerialWriteNapi);
    SetNamedFunction(env, serialPortObj, "onDataRead", SerialOnDataReadNapi);
    SetNamedFunction(env, serialPortObj, "offDataRead", SerialOffDataReadNapi);
    SetNamedFunction(env, serialPortObj, "flush", SerialFlushNapi);
    SetNamedFunction(env, serialPortObj, "drain", SerialDrainNapi);
    SetNamedFunction(env, serialPortObj, "setRts", SerialSetRtsNapi);
    SetNamedFunction(env, serialPortObj, "sendBrk", SerialSendBrkNapi);
    SetNamedFunction(env, serialPortObj, "getCts", SerialGetCtsNapi);

    napi_status wrapStatus = napi_wrap(env, serialPortObj, native, SerialPortFinalize, nullptr, nullptr);
    if (wrapStatus != napi_ok) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[SerialPortToJSObject] napi_wrap failed for portName=%{public}s",
            native->portName.c_str());
        return serialPortObj;
    }

    napi_create_reference(env, serialPortObj, NAPI_REF_COUNT_STRONG, &native->objectRef);

    return serialPortObj;
}

static void InvalidatePort(napi_env env, SerialPortNative* native)
{
    native->invalidated = true;
    RemoveAllListenersForPort(env, native->portName);

    napi_value oldObj = nullptr;
    napi_get_reference_value(env, native->objectRef, &oldObj);
    napi_delete_reference(env, native->objectRef);
    if (oldObj != nullptr) {
        napi_create_reference(env, oldObj, NAPI_REF_COUNT_WEAK, &native->objectRef);
        g_invalidatedPorts.push_back(native);
    } else {
        delete native;
    }
}

static napi_value RestoreInvalidatedPort(napi_env env, const SerialPortInfo& deviceInfo)
{
    for (auto it = g_invalidatedPorts.begin(); it != g_invalidatedPorts.end();) {
        SerialPortNative* native = *it;
        if (native->portName != deviceInfo.portName || !IsSerialPortInfoEqual(native->info, deviceInfo)) {
            ++it;
            continue;
        }

        napi_value jsObj = nullptr;
        napi_get_reference_value(env, native->objectRef, &jsObj);
        if (jsObj != nullptr) {
            SERIAL_HILOGI(MODULE_JS_NAPI, "[LIFECYCLE] RESTORE: portName=%{public}s, ptr=0x%{public}04x",
                deviceInfo.portName.c_str(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native) & 0xFFFF));
            napi_delete_reference(env, native->objectRef);
            napi_create_reference(env, jsObj, NAPI_REF_COUNT_STRONG, &native->objectRef);
            native->invalidated = false;
            g_activePorts[deviceInfo.portName] = native;
            g_invalidatedPorts.erase(it);
            return jsObj;
        }

        SERIAL_HILOGI(MODULE_JS_NAPI,
            "[LIFECYCLE] GC(detected in restore check): portName=%{public}s, ptr=0x%{public}04x",
            native->portName.c_str(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native) & 0xFFFF));
        napi_delete_reference(env, native->objectRef);
        delete native;
        it = g_invalidatedPorts.erase(it);
    }
    return nullptr;
}

static void InvalidateDisappearedPorts(napi_env env, const std::set<std::string>& activePortNames)
{
    for (auto it = g_activePorts.begin(); it != g_activePorts.end();) {
        if (activePortNames.find(it->first) == activePortNames.end()) {
            SerialPortNative* native = it->second;
            SERIAL_HILOGI(MODULE_JS_NAPI,
                "[LIFECYCLE] INVALIDATE(disappeared): portName=%{public}s, ptr=0x%{public}04x",
                native->portName.c_str(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native) & 0xFFFF));
            InvalidatePort(env, native);
            it = g_activePorts.erase(it);
        } else {
            ++it;
        }
    }
}

static bool TryReuseActivePort(napi_env env, const SerialPortInfo& deviceInfo, uint32_t index, napi_value result,
    std::set<std::string>& activePortNames)
{
    auto activeIt = g_activePorts.find(deviceInfo.portName);
    if (activeIt == g_activePorts.end()) {
        return false;
    }

    SerialPortNative* native = activeIt->second;
    if (IsSerialPortInfoEqual(native->info, deviceInfo)) {
        napi_value serialPortObj = nullptr;
        napi_get_reference_value(env, native->objectRef, &serialPortObj);
        if (serialPortObj != nullptr) {
            SERIAL_HILOGI(MODULE_JS_NAPI, "[LIFECYCLE] REUSE(active): portName=%{public}s, ptr=0x%{public}04x",
                deviceInfo.portName.c_str(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native) & 0xFFFF));
            activePortNames.insert(deviceInfo.portName);
            napi_set_element(env, result, index, serialPortObj);
            return true;
        }
    }

    SERIAL_HILOGI(MODULE_JS_NAPI, "[LIFECYCLE] INVALIDATE: portName=%{public}s, ptr=0x%{public}04x (info changed)",
        deviceInfo.portName.c_str(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native) & 0xFFFF));
    InvalidatePort(env, native);
    g_activePorts.erase(activeIt);
    return false;
}

static napi_value CreateNewPortObject(napi_env env, const SerialPortInfo& deviceInfo)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "[LIFECYCLE] CREATE: portName=%{public}s", deviceInfo.portName.c_str());
    SerialPortNative* native = new SerialPortNative();
    native->portName = deviceInfo.portName;
    native->info = deviceInfo;
    native->invalidated = false;
    native->env = env;

    napi_value serialPortObj = SerialPortToJSObject(env, deviceInfo, native);
    if (native->objectRef == nullptr) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[CreateNewPortObject] napi_wrap failed, cleanup portName=%{public}s",
            native->portName.c_str());
        delete native;
        return serialPortObj;
    }
    g_activePorts[deviceInfo.portName] = native;
    return serialPortObj;
}

static napi_value CreateDeviceListArray(napi_env env, const std::vector<SerialPortInfo>& deviceList)
{
    napi_value result = nullptr;
    napi_create_array(env, &result);

    std::lock_guard<std::mutex> lock(g_portCacheMutex);

    std::set<std::string> activePortNames;

    for (uint32_t i = 0; i < deviceList.size(); ++i) {
        const auto& deviceInfo = deviceList[i];

        if (TryReuseActivePort(env, deviceInfo, i, result, activePortNames)) {
            continue;
        }

        napi_value serialPortObj = RestoreInvalidatedPort(env, deviceInfo);
        if (serialPortObj == nullptr) {
            serialPortObj = CreateNewPortObject(env, deviceInfo);
        }

        activePortNames.insert(deviceInfo.portName);
        napi_set_element(env, result, i, serialPortObj);
    }

    InvalidateDisappearedPorts(env, activePortNames);

    return result;
}

static void CreateAndQueueAsyncWork(napi_env env, napi_value resourceName, napi_async_execute_callback execute,
    napi_async_complete_callback complete, AsyncCallbackContext* context)
{
    napi_status status =
        napi_create_async_work(env, nullptr, resourceName, execute, complete, context, &context->asyncWork);
    if (status == napi_ok) {
        status = napi_queue_async_work(env, context->asyncWork);
    }

    if (status != napi_ok) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[CreateAndQueueAsyncWork] failed, status=%{public}d", status);
        if (context->deferred != nullptr) {
            napi_value error = CreateBusinessError(env, SERIAL_JS_ERR_SERVICE, "Service error");
            napi_reject_deferred(env, context->deferred, error);
        }
        if (context->asyncWork != nullptr) {
            napi_delete_async_work(env, context->asyncWork);
        }
        delete context;
    }
}

static void SerialGetSerialPortListExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialPortListAsyncContext*>(data);
    context->ret = g_serialClient.QuerySerialDevices(context->deviceList);
}

static void SerialGetSerialPortListComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialPortListAsyncContext*>(data);

    if (context->ret == SERIAL_OK) {
        napi_value result = CreateDeviceListArray(env, context->deviceList);
        napi_resolve_deferred(env, context->deferred, result);
    } else {
        SERIAL_HILOGE(
            MODULE_JS_NAPI, "[GetSerialPortListComplete] QuerySerialDevices failed, ret=%{public}d", context->ret);
        napi_value error = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
        napi_reject_deferred(env, context->deferred, error);
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialGetSerialPortListNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "getSerialPortList start");
    size_t argc = 0;
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, nullptr, &thisVar, &data);

    SerialPortListAsyncContext* context = new SerialPortListAsyncContext();
    context->env = env;
    context->asyncWork = nullptr;

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "getSerialPortList", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(
        env, resourceName, SerialGetSerialPortListExecute, SerialGetSerialPortListComplete, context);

    return promise;
}

static void SerialOpenExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialOpenAsyncContext*>(data);
    context->ret = g_serialClient.OpenSerial(context->portInfo, context->config);
}

static void SerialOpenComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialOpenAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        std::lock_guard<std::mutex> lock(g_portCacheMutex);
        auto it = g_activePorts.find(context->portInfo.portName);
        if (it != g_activePorts.end() && it->second != nullptr) {
            it->second->isOpened = true;
        }
        napi_get_undefined(env, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static bool GetPortNameFromThisVar(napi_env env, napi_value thisVar, std::string& portName)
{
    napi_value portNameValue;
    if (thisVar != nullptr && napi_get_named_property(env, thisVar, "portName", &portNameValue) == napi_ok) {
        char buf[PORT_NAME_BUF_SIZE] = {0};
        size_t len = 0;
        if (napi_get_value_string_utf8(env, portNameValue, buf, sizeof(buf), &len) == napi_ok && len > 0) {
            portName = std::string(buf, len);
            return true;
        }
    }
    return false;
}

static void GetBoolProperty(napi_env env, napi_value obj, const char* name, bool& out, bool defaultValue)
{
    napi_value value;
    if (napi_get_named_property(env, obj, name, &value) == napi_ok) {
        napi_valuetype type;
        napi_typeof(env, value, &type);
        if (type == napi_boolean) {
            napi_get_value_bool(env, value, &out);
        } else {
            out = defaultValue;
        }
    } else {
        out = defaultValue;
    }
}

static bool IsStrictInteger(napi_env env, napi_value value)
{
    double doubleVal = 0;
    napi_get_value_double(env, value, &doubleVal);
    if (!std::isfinite(doubleVal)) {
        return false;
    }
    return doubleVal == static_cast<double>(static_cast<int64_t>(doubleVal));
}

static bool GetStrictInt32Property(napi_env env, napi_value obj, const char* name, int32_t& out, int32_t defaultValue)
{
    napi_value value;
    if (napi_get_named_property(env, obj, name, &value) == napi_ok) {
        napi_valuetype type;
        napi_typeof(env, value, &type);
        if (type == napi_number) {
            if (!IsStrictInteger(env, value)) {
                return false;
            }
            napi_get_value_int32(env, value, &out);
        } else {
            out = defaultValue;
        }
    } else {
        out = defaultValue;
    }
    return true;
}

static void GetPortInfoFromThisVar(napi_env env, napi_value thisVar, SerialPortInfo& portInfo)
{
    napi_value portInfoObj = nullptr;
    if (thisVar == nullptr || napi_get_named_property(env, thisVar, "portInfo", &portInfoObj) != napi_ok) {
        return;
    }
    napi_valuetype type = napi_undefined;
    napi_typeof(env, portInfoObj, &type);
    if (type != napi_object) {
        return;
    }

    GetStrictInt32Property(env, portInfoObj, "productId", portInfo.productId, 0);
    GetStrictInt32Property(env, portInfoObj, "vendorId", portInfo.vendorId, 0);

    napi_value manufacturerValue = nullptr;
    if (napi_get_named_property(env, portInfoObj, "manufacturer", &manufacturerValue) == napi_ok) {
        napi_valuetype strType = napi_undefined;
        napi_typeof(env, manufacturerValue, &strType);
        if (strType == napi_string) {
            char buf[PORT_NAME_BUF_SIZE] = {0};
            size_t len = 0;
            if (napi_get_value_string_utf8(env, manufacturerValue, buf, sizeof(buf), &len) == napi_ok) {
                portInfo.manufacturer = std::string(buf, len);
            }
        }
    }
}

static int32_t ParseParityString(const std::string& parityStr)
{
    if (parityStr == "none") {
        return PARITY_NONE;
    }
    if (parityStr == "even") {
        return PARITY_EVEN;
    }
    if (parityStr == "odd") {
        return PARITY_ODD;
    }
    if (parityStr == "mark") {
        return PARITY_MARK;
    }
    if (parityStr == "space") {
        return PARITY_SPACE;
    }
    return PARITY_NONE;
}

static bool IsValueInArray(int32_t value, const int32_t* arr, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (value == arr[i]) {
            return true;
        }
    }
    return false;
}

static bool ParseSerialConfig(napi_env env, napi_value configObj, SerialConfig& config)
{
    napi_valuetype type;
    napi_typeof(env, configObj, &type);

    if (type != napi_object) {
        return false;
    }

    if (!GetStrictInt32Property(env, configObj, "baudRate", config.baudRate, DEFAULT_BAUD_RATE)) {
        return false;
    }
    if (config.baudRate <= 0) {
        return false;
    }

    if (!GetStrictInt32Property(env, configObj, "dataBits", config.dataBits, DEFAULT_DATA_BITS)) {
        return false;
    }
    if (!IsValueInArray(config.dataBits, VALID_DATA_BITS, sizeof(VALID_DATA_BITS) / sizeof(VALID_DATA_BITS[0]))) {
        return false;
    }

    if (!GetStrictInt32Property(env, configObj, "stopBits", config.stopBits, DEFAULT_STOP_BITS)) {
        return false;
    }
    if (!IsValueInArray(config.stopBits, VALID_STOP_BITS, sizeof(VALID_STOP_BITS) / sizeof(VALID_STOP_BITS[0]))) {
        return false;
    }

    napi_value configValue;
    if (napi_get_named_property(env, configObj, "parity", &configValue) == napi_ok) {
        napi_valuetype parityType;
        napi_typeof(env, configValue, &parityType);
        if (parityType == napi_string) {
            char parityBuf[PARITY_BUF_SIZE] = {0};
            size_t parityLen = 0;
            napi_get_value_string_utf8(env, configValue, parityBuf, sizeof(parityBuf), &parityLen);
            config.parity = ParseParityString(std::string(parityBuf));
        } else {
            config.parity = DEFAULT_PARITY;
        }
    } else {
        config.parity = DEFAULT_PARITY;
    }

    GetBoolProperty(env, configObj, "rtscts", config.rtscts, false);
    GetBoolProperty(env, configObj, "xon", config.xon, false);
    GetBoolProperty(env, configObj, "xoff", config.xoff, false);
    GetBoolProperty(env, configObj, "xany", config.xany, false);
    return true;
}

static napi_value SerialOpenNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "open start");
    size_t argc = 1;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        return CreateRejectedPromise(env, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
    }

    SerialOpenAsyncContext* context = new SerialOpenAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;

    GetPortNameFromThisVar(env, thisVar, context->portInfo.portName);
    GetPortInfoFromThisVar(env, thisVar, context->portInfo);

    if (argc >= 1) {
        napi_valuetype paramType;
        napi_typeof(env, argv[0], &paramType);
        if (paramType != napi_object) {
            ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
            delete context;
            return nullptr;
        }
        if (!ParseSerialConfig(env, argv[0], context->config)) {
            ThrowBusinessError(env, SERIAL_JS_ERR_INVALID_PARAM, "Invalid parameter");
            delete context;
            return nullptr;
        }
    } else {
        context->config.baudRate = DEFAULT_BAUD_RATE;
        context->config.dataBits = DEFAULT_DATA_BITS;
        context->config.stopBits = DEFAULT_STOP_BITS;
        context->config.parity = DEFAULT_PARITY;
        context->config.rtscts = false;
        context->config.xon = false;
        context->config.xoff = false;
        context->config.xany = false;
    }

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "open", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialOpenExecute, SerialOpenComplete, context);

    return promise;
}

static void SerialCloseExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialCloseAsyncContext*>(data);
    context->ret = g_serialClient.CloseSerial(context->portName);
}

static void SerialCloseComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialCloseAsyncContext*>(data);
    napi_value result = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_portCacheMutex);
        auto it = g_activePorts.find(context->portName);
        if (it != g_activePorts.end() && it->second != nullptr) {
            it->second->isOpened = false;
        }
    }

    bool success = (context->ret == SERIAL_OK);
    if (success) {
        napi_get_undefined(env, &result);
    } else {
        SERIAL_HILOGE(MODULE_JS_NAPI, "close failed, ret=%{public}d", context->ret);
        int32_t errCode =
            (context->ret == SERIAL_JS_ERR_DEVICE_NOT_OPENED || context->ret == SERIAL_JS_ERR_DEVICE_DISCONNECTED) ?
            SERIAL_JS_ERR_DEVICE_NOT_OPENED :
            SERIAL_JS_ERR_SERVICE;
        result = CreateBusinessError(env, errCode, SerialJsErrorMessage(errCode));
    }

    if (context->deferred != nullptr) {
        if (success) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialCloseNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "close start");
    size_t argc = 0;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    SerialCloseAsyncContext* context = new SerialCloseAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;

    GetPortNameFromThisVar(env, thisVar, context->portName);

    RemoveAllListenersForPort(env, context->portName);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "close", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialCloseExecute, SerialCloseComplete, context);

    return promise;
}

static void SerialWriteExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialWriteAsyncContext*>(data);
    context->ret =
        g_serialClient.WriteSerial(context->portName, context->data, context->timeout, context->bytesWritten);
}

static void SerialWriteComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialWriteAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        napi_create_int32(env, context->bytesWritten, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static bool ValidateWriteParams(
    napi_env env, size_t argc, napi_value argv[], std::vector<uint8_t>& outData, int32_t& outTimeout)
{
    if (argc < 1) {
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return false;
    }

    bool isTypedArray = false;
    napi_is_typedarray(env, argv[0], &isTypedArray);
    if (!isTypedArray) {
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return false;
    }

    {
        uint8_t* dataPtr = nullptr;
        size_t length = 0;
        napi_get_typedarray_info(env, argv[0], nullptr, &length, reinterpret_cast<void**>(&dataPtr), nullptr, nullptr);
        outData.assign(dataPtr, dataPtr + length);
    }

    if (outData.empty() || outData.size() > WRITE_DATA_MAX_SIZE) {
        ThrowBusinessError(env, SERIAL_JS_ERR_INVALID_PARAM, "Invalid parameter");
        return false;
    }

    if (argc >= WRITE_MAX_ARGC) {
        napi_valuetype timeoutType;
        napi_typeof(env, argv[1], &timeoutType);
        if (timeoutType != napi_number) {
            ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
            return false;
        }
        if (!IsStrictInteger(env, argv[1])) {
            ThrowBusinessError(env, SERIAL_JS_ERR_INVALID_PARAM, "Invalid parameter");
            return false;
        }
        napi_get_value_int32(env, argv[1], &outTimeout);
    }

    if (outTimeout < WRITE_TIMEOUT_MIN || outTimeout > WRITE_TIMEOUT_MAX) {
        ThrowBusinessError(env, SERIAL_JS_ERR_INVALID_PARAM, "Invalid parameter");
        return false;
    }

    return true;
}

static napi_value SerialWriteNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "write start");
    size_t argc = WRITE_MAX_ARGC;
    napi_value argv[WRITE_MAX_ARGC] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        return CreateRejectedPromise(env, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
    }

    std::vector<uint8_t> writeData;
    int32_t timeout = 0;
    if (!ValidateWriteParams(env, argc, argv, writeData, timeout)) {
        return nullptr;
    }

    SerialWriteAsyncContext* context = new SerialWriteAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;
    context->timeout = timeout;
    context->bytesWritten = 0;
    context->data = std::move(writeData);

    GetPortNameFromThisVar(env, thisVar, context->portName);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "write", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialWriteExecute, SerialWriteComplete, context);

    return promise;
}

static void TsfnFinalize(napi_env env, void* finalizeData, void* hint)
{
    auto* pendingQueue = static_cast<std::queue<CallbackData*>*>(finalizeData);
    while (!pendingQueue->empty()) {
        delete pendingQueue->front();
        pendingQueue->pop();
    }
    delete pendingQueue;
}

static void CallJsCallback(napi_env env, napi_value jsCb, void* context, void* data)
{
    if (data == nullptr) {
        return;
    }
    CallbackData* callbackData = static_cast<CallbackData*>(data);
    InvokeJsCallback(env, callbackData->callbackRef, callbackData->data);
    delete callbackData;
}

static std::function<void(const std::vector<uint8_t>&)> CreateDataReadCallback(
    napi_ref callbackRef, napi_env env, napi_threadsafe_function tsfn)
{
    return [callbackRef, tsfn](const std::vector<uint8_t>& data) {
        CallbackData* callbackData = new CallbackData();
        callbackData->data = data;
        callbackData->callbackRef = callbackRef;

        napi_status status = napi_call_threadsafe_function(tsfn, callbackData, napi_tsfn_nonblocking);
        if (status != napi_ok) {
            SERIAL_HILOGE(MODULE_JS_NAPI, "[DataReadCallback] napi_call_threadsafe_function failed");
            delete callbackData;
        }
    };
}

static napi_threadsafe_function CreateThreadSafeFunction(napi_env env)
{
    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "dataReadCallback", NAPI_AUTO_LENGTH, &resourceName);

    auto* pendingQueue = new std::queue<CallbackData*>();

    napi_threadsafe_function tsfn = nullptr;
    napi_create_threadsafe_function(
        env, nullptr, nullptr, resourceName, 0, 1, pendingQueue, TsfnFinalize, nullptr, CallJsCallback, &tsfn);
    if (tsfn == nullptr) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[CreateThreadSafeFunction] napi_create_threadsafe_function failed");
        delete pendingQueue;
    }
    return tsfn;
}

static bool ValidateOnDataReadArgs(napi_env env, size_t argc, napi_value argv[])
{
    if (argc < 1) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[ValidateOnDataReadArgs] onDataRead requires callback parameter");
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return false;
    }
    napi_valuetype type;
    napi_typeof(env, argv[0], &type);
    if (type != napi_function) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[ValidateOnDataReadArgs] onDataRead parameter is not a function");
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return false;
    }
    return true;
}

static bool IsCallbackAlreadyRegistered(napi_env env, const std::string& portName, napi_value callback)
{
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    auto portIt = g_dataCallbacks.find(portName);
    if (portIt == g_dataCallbacks.end()) {
        return false;
    }
    for (auto& pair : portIt->second) {
        napi_value storedCallback = nullptr;
        napi_get_reference_value(env, pair.first, &storedCallback);
        bool isEqual = false;
        napi_strict_equals(env, callback, storedCallback, &isEqual);
        if (isEqual) {
            return true;
        }
    }
    return false;
}

static bool RegisterOnDataReadListener(napi_env env, const std::string& portName, napi_ref callbackRef)
{
    napi_threadsafe_function tsfn = CreateThreadSafeFunction(env);
    if (tsfn == nullptr) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[SerialOnDataRead] CreateThreadSafeFunction failed");
        napi_delete_reference(env, callbackRef);
        ThrowBusinessError(env, SERIAL_JS_ERR_SERVICE, SerialJsErrorMessage(SERIAL_JS_ERR_SERVICE));
        return false;
    }

    auto callback = CreateDataReadCallback(callbackRef, env, tsfn);

    ListenerHandle listenerHandle = INVALID_LISTENER_HANDLE;
    int32_t regRet = g_serialClient.RegisterDataListener(portName, callback, listenerHandle);
    if (regRet != SERIAL_OK || listenerHandle == INVALID_LISTENER_HANDLE) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "[SerialOnDataRead] RegisterDataListener failed, ret=%{public}d", regRet);
        napi_release_threadsafe_function(tsfn, napi_tsfn_release);
        napi_delete_reference(env, callbackRef);
        ThrowBusinessError(env, regRet, SerialJsErrorMessage(regRet));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        CallbackInfo cbInfo = {callbackRef, env, listenerHandle, tsfn};
        g_dataCallbacks[portName][callbackRef] = cbInfo;
    }

    SERIAL_HILOGI(MODULE_JS_NAPI, "onDataRead success, portName=%{public}s, listenerHandle=%{public}llu",
        portName.c_str(), static_cast<unsigned long long>(listenerHandle));
    return true;
}

static napi_value SerialOnDataReadNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "onDataRead start");
    size_t argc = 1;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        ThrowBusinessError(
            env, SERIAL_JS_ERR_DEVICE_DISCONNECTED, SerialJsErrorMessage(SERIAL_JS_ERR_DEVICE_DISCONNECTED));
        return nullptr;
    }

    if (!CheckPortOpened(env, thisVar)) {
        return nullptr;
    }

    std::string portName;
    GetPortNameFromThisVar(env, thisVar, portName);

    if (!ValidateOnDataReadArgs(env, argc, argv)) {
        return nullptr;
    }

    if (IsCallbackAlreadyRegistered(env, portName, argv[0])) {
        SERIAL_HILOGI(MODULE_JS_NAPI, "[SerialOnDataRead] onDataRead callback already registered, skip");
        napi_value undefined = nullptr;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    napi_ref callbackRef = nullptr;
    napi_create_reference(env, argv[0], NAPI_REF_COUNT_STRONG, &callbackRef);

    if (!RegisterOnDataReadListener(env, portName, callbackRef)) {
        return nullptr;
    }

    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);
    return undefined;
}

struct FoundListener {
    ListenerHandle handle;
    napi_ref callbackRef;
    napi_threadsafe_function tsfn;
};

static FoundListener FindAndRemoveSpecificListener(napi_env env, const std::string& portName, napi_value jsCallback)
{
    FoundListener result = {INVALID_LISTENER_HANDLE, nullptr, nullptr};
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    auto portIt = g_dataCallbacks.find(portName);
    if (portIt == g_dataCallbacks.end()) {
        return result;
    }

    for (auto it = portIt->second.begin(); it != portIt->second.end(); ++it) {
        napi_value storedCallback = nullptr;
        napi_get_reference_value(env, it->first, &storedCallback);

        bool isEqual = false;
        napi_strict_equals(env, jsCallback, storedCallback, &isEqual);

        if (isEqual) {
            result.handle = it->second.listenerHandle;
            result.callbackRef = it->first;
            result.tsfn = it->second.tsfn;
            break;
        }
    }

    if (result.handle != INVALID_LISTENER_HANDLE && result.callbackRef != nullptr) {
        portIt->second.erase(result.callbackRef);
        if (portIt->second.empty()) {
            g_dataCallbacks.erase(portIt);
        }
    }
    return result;
}

static bool UnregisterListenerChecked(napi_env env, const std::string& portName, ListenerHandle handle)
{
    int32_t ret = g_serialClient.UnregisterDataListener(portName, handle);
    if (ret != SERIAL_OK && ret != SERIAL_JS_ERR_DEVICE_DISCONNECTED) {
        SERIAL_HILOGE(MODULE_JS_NAPI, "UnregisterDataListener failed, ret=%{public}d", ret);
        ThrowBusinessError(env, SERIAL_JS_ERR_SERVICE, SerialJsErrorMessage(SERIAL_JS_ERR_SERVICE));
        return false;
    }
    return true;
}

static napi_value RemoveAllListenersChecked(napi_env env, const std::string& portName)
{
    std::vector<std::pair<napi_ref, ListenerHandle>> refsAndHandles;
    std::vector<napi_threadsafe_function> tsfnsToRelease;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        auto portIt = g_dataCallbacks.find(portName);
        if (portIt != g_dataCallbacks.end()) {
            for (auto& pair : portIt->second) {
                refsAndHandles.push_back({pair.second.callbackRef, pair.second.listenerHandle});
                tsfnsToRelease.push_back(pair.second.tsfn);
            }
            g_dataCallbacks.erase(portIt);
        }
    }
    for (auto& rh : refsAndHandles) {
        if (!UnregisterListenerChecked(env, portName, rh.second)) {
            return nullptr;
        }
    }
    for (auto& rh : refsAndHandles) {
        napi_delete_reference(env, rh.first);
    }
    for (auto& tsfn : tsfnsToRelease) {
        napi_release_threadsafe_function(tsfn, napi_tsfn_release);
    }
    SERIAL_HILOGI(MODULE_JS_NAPI, "offDataRead all callbacks for portName=%{public}s", portName.c_str());
    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static napi_value SerialOffDataReadNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "offDataRead start");
    size_t argc = 1;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (!CheckPortOpened(env, thisVar)) {
        return nullptr;
    }

    std::string portName;
    GetPortNameFromThisVar(env, thisVar, portName);

    napi_valuetype type = napi_undefined;
    if (argc >= 1 && argv[0] != nullptr) {
        napi_typeof(env, argv[0], &type);
    }

    if (argc < 1 || type == napi_undefined || type == napi_null) {
        return RemoveAllListenersChecked(env, portName);
    }

    if (type != napi_function) {
        SERIAL_HILOGW(MODULE_JS_NAPI, "[SerialOffDataRead] offDataRead parameter is not a function");
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return nullptr;
    }

    FoundListener found = FindAndRemoveSpecificListener(env, portName, argv[0]);
    if (found.handle != INVALID_LISTENER_HANDLE) {
        if (!UnregisterListenerChecked(env, portName, found.handle)) {
            return nullptr;
        }
        SERIAL_HILOGI(MODULE_JS_NAPI, "offDataRead specific callback, portName=%{public}s", portName.c_str());
    } else {
        SERIAL_HILOGW(
            MODULE_JS_NAPI, "[SerialOffDataRead] Callback not found for portName=%{public}s", portName.c_str());
    }

    if (found.callbackRef != nullptr) {
        napi_delete_reference(env, found.callbackRef);
    }
    if (found.tsfn != nullptr) {
        napi_release_threadsafe_function(found.tsfn, napi_tsfn_release);
    }

    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);
    return undefined;
}

static void SerialFlushExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialFlushAsyncContext*>(data);
    context->ret = g_serialClient.Flush(context->portName);
}

static void SerialFlushComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialFlushAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        napi_get_undefined(env, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialFlushNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "flush start");
    size_t argc = 0;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        return CreateRejectedPromise(env, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
    }

    SerialFlushAsyncContext* context = new SerialFlushAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;

    GetPortNameFromThisVar(env, thisVar, context->portName);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "flush", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialFlushExecute, SerialFlushComplete, context);

    return promise;
}

static void SerialDrainExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialDrainAsyncContext*>(data);
    context->ret = g_serialClient.Drain(context->portName);
}

static void SerialDrainComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialDrainAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        napi_get_undefined(env, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialDrainNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "drain start");
    size_t argc = 0;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        return CreateRejectedPromise(env, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
    }

    SerialDrainAsyncContext* context = new SerialDrainAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;

    GetPortNameFromThisVar(env, thisVar, context->portName);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "drain", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialDrainExecute, SerialDrainComplete, context);

    return promise;
}

static void SerialSetRtsExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialSetRtsAsyncContext*>(data);
    context->ret = g_serialClient.SetRtsSignal(context->portName, context->rts);
}

static void SerialSetRtsComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialSetRtsAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        napi_get_undefined(env, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialSetRtsNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "setRts start");
    size_t argc = 1;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        return CreateRejectedPromise(env, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
    }

    if (argc < 1) {
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return nullptr;
    }

    {
        napi_valuetype paramType;
        napi_typeof(env, argv[0], &paramType);
        if (paramType != napi_boolean) {
            ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
            return nullptr;
        }
    }

    SerialSetRtsAsyncContext* context = new SerialSetRtsAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;

    GetPortNameFromThisVar(env, thisVar, context->portName);

    napi_get_value_bool(env, argv[0], &context->rts);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "setRts", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialSetRtsExecute, SerialSetRtsComplete, context);

    return promise;
}

static void SerialSendBrkExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialSendBrkAsyncContext*>(data);
    context->ret = g_serialClient.SendBrkSignal(context->portName);
}

static void SerialSendBrkComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialSendBrkAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        napi_get_undefined(env, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialSendBrkNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "sendBrk start");
    size_t argc = 0;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        return CreateRejectedPromise(env, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
    }

    SerialSendBrkAsyncContext* context = new SerialSendBrkAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;

    GetPortNameFromThisVar(env, thisVar, context->portName);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "sendBrk", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialSendBrkExecute, SerialSendBrkComplete, context);

    return promise;
}

static void SerialGetCtsExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialGetCtsAsyncContext*>(data);
    context->ret = g_serialClient.GetCtsSignal(context->portName, context->cts);
}

static void SerialGetCtsComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialGetCtsAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        napi_get_boolean(env, context->cts, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialGetCtsNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "getCts start");
    size_t argc = 0;
    napi_value argv[1] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (IsPortInvalidated(env, thisVar)) {
        return CreateRejectedPromise(env, SERIAL_JS_ERR_DEVICE_DISCONNECTED);
    }

    SerialGetCtsAsyncContext* context = new SerialGetCtsAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;
    context->cts = false;

    GetPortNameFromThisVar(env, thisVar, context->portName);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "getCts", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialGetCtsExecute, SerialGetCtsComplete, context);

    return promise;
}

static napi_value CreateDataBitsEnum(napi_env env)
{
    napi_value result;
    napi_create_object(env, &result);

    napi_value five;
    napi_create_int32(env, DATA_BITS_FIVE, &five);
    napi_set_named_property(env, result, "FIVE", five);

    napi_value six;
    napi_create_int32(env, DATA_BITS_SIX, &six);
    napi_set_named_property(env, result, "SIX", six);

    napi_value seven;
    napi_create_int32(env, DATA_BITS_SEVEN, &seven);
    napi_set_named_property(env, result, "SEVEN", seven);

    napi_value eight;
    napi_create_int32(env, DATA_BITS_EIGHT, &eight);
    napi_set_named_property(env, result, "EIGHT", eight);

    return result;
}

static napi_value CreateStopBitsEnum(napi_env env)
{
    napi_value result;
    napi_create_object(env, &result);

    napi_value one;
    napi_create_int32(env, STOP_BITS_ONE, &one);
    napi_set_named_property(env, result, "ONE", one);

    napi_value two;
    napi_create_int32(env, STOP_BITS_TWO, &two);
    napi_set_named_property(env, result, "TWO", two);

    return result;
}

static napi_value CreateParityEnum(napi_env env)
{
    napi_value result;
    napi_create_object(env, &result);

    napi_value none;
    napi_create_string_utf8(env, "none", NAPI_AUTO_LENGTH, &none);
    napi_set_named_property(env, result, "NONE", none);

    napi_value even;
    napi_create_string_utf8(env, "even", NAPI_AUTO_LENGTH, &even);
    napi_set_named_property(env, result, "EVEN", even);

    napi_value odd;
    napi_create_string_utf8(env, "odd", NAPI_AUTO_LENGTH, &odd);
    napi_set_named_property(env, result, "ODD", odd);

    napi_value mark;
    napi_create_string_utf8(env, "mark", NAPI_AUTO_LENGTH, &mark);
    napi_set_named_property(env, result, "MARK", mark);

    napi_value space;
    napi_create_string_utf8(env, "space", NAPI_AUTO_LENGTH, &space);
    napi_set_named_property(env, result, "SPACE", space);

    return result;
}

static void SerialAddPortAuthExecute(napi_env env, void* data)
{
    auto* context = static_cast<SerialAddPortAuthAsyncContext*>(data);
    context->ret = g_serialClient.AddPortAuthorization(context->tokenId, context->deviceId);
}

static void SerialAddPortAuthComplete(napi_env env, napi_status status, void* data)
{
    auto* context = static_cast<SerialAddPortAuthAsyncContext*>(data);
    napi_value result = nullptr;

    if (context->ret == SERIAL_OK) {
        napi_get_undefined(env, &result);
    } else {
        result = CreateBusinessError(env, context->ret, SerialJsErrorMessage(context->ret));
    }

    if (context->deferred != nullptr) {
        if (context->ret == SERIAL_OK) {
            napi_resolve_deferred(env, context->deferred, result);
        } else {
            napi_reject_deferred(env, context->deferred, result);
        }
    }

    napi_delete_async_work(env, context->asyncWork);
    delete context;
}

static napi_value SerialAddPortAuthorizationNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "addPortAuthorization start");
    size_t argc = ADD_AUTH_MAX_ARGC;
    napi_value argv[ADD_AUTH_MAX_ARGC] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;

    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (argc < ADD_AUTH_MAX_ARGC) {
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return nullptr;
    }

    {
        napi_valuetype paramType;
        napi_typeof(env, argv[0], &paramType);
        if (paramType != napi_string) {
            ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
            return nullptr;
        }
        napi_typeof(env, argv[1], &paramType);
        if (paramType != napi_string) {
            ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
            return nullptr;
        }
    }

    SerialAddPortAuthAsyncContext* context = new SerialAddPortAuthAsyncContext();
    context->env = env;
    context->deferred = nullptr;
    context->asyncWork = nullptr;

    char tokenIdBuf[PORT_NAME_BUF_SIZE] = {0};
    size_t tokenIdLen = 0;
    napi_get_value_string_utf8(env, argv[0], tokenIdBuf, sizeof(tokenIdBuf), &tokenIdLen);
    context->tokenId = std::string(tokenIdBuf);

    char deviceIdBuf[PORT_NAME_BUF_SIZE] = {0};
    size_t deviceIdLen = 0;
    napi_get_value_string_utf8(env, argv[1], deviceIdBuf, sizeof(deviceIdBuf), &deviceIdLen);
    context->deviceId = std::string(deviceIdBuf);

    napi_value promise = nullptr;
    napi_create_promise(env, &context->deferred, &promise);

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, "addPortAuthorization", NAPI_AUTO_LENGTH, &resourceName);

    CreateAndQueueAsyncWork(env, resourceName, SerialAddPortAuthExecute, SerialAddPortAuthComplete, context);

    return promise;
}

napi_value SerialInit(napi_env env, napi_value exports)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "SerialInit enter");
    napi_property_descriptor desc[] = {
        DECLARE_NAPI_FUNCTION("getSerialPortList", SerialGetSerialPortListNapi),
        DECLARE_NAPI_FUNCTION("addPortAuthorization", SerialAddPortAuthorizationNapi),
    };
    NAPI_CALL(env, napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc));

    napi_value dataBitsEnum = CreateDataBitsEnum(env);
    napi_set_named_property(env, exports, "DataBits", dataBitsEnum);

    napi_value stopBitsEnum = CreateStopBitsEnum(env);
    napi_set_named_property(env, exports, "StopBits", stopBitsEnum);

    napi_value parityEnum = CreateParityEnum(env);
    napi_set_named_property(env, exports, "Parity", parityEnum);

    SERIAL_HILOGI(MODULE_JS_NAPI, "SerialInit return");
    return exports;
}

static napi_module g_serialModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = "busManager.serial",
    .nm_register_func = SerialInit,
    .nm_modname = "busManager.serial",
    .nm_priv = nullptr,
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterSerialModule(void)
{
    napi_module_register(&g_serialModule);
}
