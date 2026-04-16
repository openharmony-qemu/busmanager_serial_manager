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

#include "serial_error_code.h"

namespace OHOS {
namespace Serial {

int32_t SerialInternalToJsCode(int32_t internalCode)
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

const char* SerialJsErrorMessage(int32_t jsCode)
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
