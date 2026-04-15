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

#ifndef SERIAL_ERROR_CODE_H
#define SERIAL_ERROR_CODE_H

#include <cstdint>

namespace OHOS {
namespace Serial {

constexpr int32_t SERIAL_OK = 0;

constexpr int32_t SERIAL_ERR_SERVICE = -1;
constexpr int32_t SERIAL_ERR_INVALID_PARAM = -2;
constexpr int32_t SERIAL_ERR_DEVICE_DISCONNECTED = -3;
constexpr int32_t SERIAL_ERR_DEVICE_BUSY = -4;
constexpr int32_t SERIAL_ERR_DEVICE_NOT_OPENED = -5;
constexpr int32_t SERIAL_ERR_TIMEOUT = -6;
constexpr int32_t SERIAL_ERR_PERMISSION_DENIED = -7;
constexpr int32_t SERIAL_ERR_NOT_ALLOWED = -8;

constexpr int32_t SERIAL_JS_ERR_SERVICE = 35700001;
constexpr int32_t SERIAL_JS_ERR_INVALID_PARAM = 35700002;
constexpr int32_t SERIAL_JS_ERR_DEVICE_DISCONNECTED = 35700003;
constexpr int32_t SERIAL_JS_ERR_DEVICE_BUSY = 35700004;
constexpr int32_t SERIAL_JS_ERR_DEVICE_NOT_OPENED = 35700005;
constexpr int32_t SERIAL_JS_ERR_TIMEOUT = 35700006;
constexpr int32_t SERIAL_JS_ERR_PERMISSION_DENIED = 35700007;
constexpr int32_t SERIAL_JS_ERR_NOT_ALLOWED = 35700008;

inline int32_t SerialInternalToJsCode(int32_t internalCode)
{
    switch (internalCode) {
        case SERIAL_ERR_SERVICE:
            return SERIAL_JS_ERR_SERVICE;
        case SERIAL_ERR_INVALID_PARAM:
            return SERIAL_JS_ERR_INVALID_PARAM;
        case SERIAL_ERR_DEVICE_DISCONNECTED:
            return SERIAL_JS_ERR_DEVICE_DISCONNECTED;
        case SERIAL_ERR_DEVICE_BUSY:
            return SERIAL_JS_ERR_DEVICE_BUSY;
        case SERIAL_ERR_DEVICE_NOT_OPENED:
            return SERIAL_JS_ERR_DEVICE_NOT_OPENED;
        case SERIAL_ERR_TIMEOUT:
            return SERIAL_JS_ERR_TIMEOUT;
        case SERIAL_ERR_PERMISSION_DENIED:
            return SERIAL_JS_ERR_PERMISSION_DENIED;
        case SERIAL_ERR_NOT_ALLOWED:
            return SERIAL_JS_ERR_NOT_ALLOWED;
        case SERIAL_OK:
            return SERIAL_OK;
        default:
            return SERIAL_JS_ERR_SERVICE;
    }
}

inline const char* SerialJsErrorMessage(int32_t jsCode)
{
    switch (jsCode) {
        case SERIAL_JS_ERR_SERVICE:
            return "Service error";
        case SERIAL_JS_ERR_INVALID_PARAM:
            return "Invalid parameter";
        case SERIAL_JS_ERR_DEVICE_DISCONNECTED:
            return "Device disconnected or info changed";
        case SERIAL_JS_ERR_DEVICE_BUSY:
            return "Device busy";
        case SERIAL_JS_ERR_DEVICE_NOT_OPENED:
            return "Device not opened";
        case SERIAL_JS_ERR_TIMEOUT:
            return "Timeout";
        case SERIAL_JS_ERR_PERMISSION_DENIED:
            return "Permission denied";
        case SERIAL_JS_ERR_NOT_ALLOWED:
            return "Not allowed";
        default:
            return "Service error";
    }
}

}
}

#endif
