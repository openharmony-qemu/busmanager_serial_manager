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

#include <cstddef>
#include <cstdint>

#include "hilog_wrapper.h"
#include "napi/native_api.h"
#include "napi/native_node_api.h"
#include "napi_common.h"
#include "serial_error_code.h"

using namespace OHOS::Serial;

namespace {
constexpr int32_t DATA_BITS_FIVE = 5;
constexpr int32_t DATA_BITS_SIX = 6;
constexpr int32_t DATA_BITS_SEVEN = 7;
constexpr int32_t DATA_BITS_EIGHT = 8;
constexpr int32_t STOP_BITS_ONE = 1;
constexpr int32_t STOP_BITS_TWO = 2;
constexpr size_t ADD_AUTH_MAX_ARGC = 2;

void ThrowBusinessError(napi_env env, int32_t code, const char* message)
{
    napi_value errorCode = nullptr;
    napi_value errorMessage = nullptr;
    napi_create_int32(env, code, &errorCode);
    napi_create_string_utf8(env, message, NAPI_AUTO_LENGTH, &errorMessage);
    napi_value error = nullptr;
    napi_create_error(env, errorCode, errorMessage, &error);
    napi_throw(env, error);
}

napi_value SerialGetSerialPortListNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "getSerialPortList stub (service not deployed)");
    napi_value promise = nullptr;
    napi_deferred deferred = nullptr;
    napi_create_promise(env, &deferred, &promise);
    napi_value result = nullptr;
    napi_create_array(env, &result);
    napi_resolve_deferred(env, deferred, result);
    return promise;
}

napi_value SerialAddPortAuthorizationNapi(napi_env env, napi_callback_info info)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "addPortAuthorization stub (service not deployed)");
    size_t argc = ADD_AUTH_MAX_ARGC;
    napi_value argv[ADD_AUTH_MAX_ARGC] = {nullptr};
    napi_value thisVar = nullptr;
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, argv, &thisVar, &data);

    if (argc < ADD_AUTH_MAX_ARGC) {
        ThrowBusinessError(env, SERIAL_JS_ERR_PARAM_ERROR, "Parameter error");
        return nullptr;
    }
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

    napi_value promise = nullptr;
    napi_deferred deferred = nullptr;
    napi_create_promise(env, &deferred, &promise);
    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);
    napi_resolve_deferred(env, deferred, undefined);
    return promise;
}

napi_value CreateDataBitsEnum(napi_env env)
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

napi_value CreateStopBitsEnum(napi_env env)
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

napi_value CreateParityEnum(napi_env env)
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
} // namespace

napi_value SerialInit(napi_env env, napi_value exports)
{
    SERIAL_HILOGI(MODULE_JS_NAPI, "SerialInit enter (service not deployed)");
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
