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

#ifndef SERIAL_HILOG_WRAPPER_H
#define SERIAL_HILOG_WRAPPER_H

#define CONFIG_HILOG
#ifdef CONFIG_HILOG
#include "hilog/log.h"
#include <cstdint>
namespace OHOS {
namespace Serial {

#define SERIALFILENAME (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef SERIAL_HILOGF
#undef SERIAL_HILOGF
#endif

#ifdef SERIAL_HILOGE
#undef SERIAL_HILOGE
#endif

#ifdef SERIAL_HILOGW
#undef SERIAL_HILOGW
#endif

#ifdef SERIAL_HILOGI
#undef SERIAL_HILOGI
#endif

#ifdef SERIAL_HILOGD
#undef SERIAL_HILOGD
#endif

enum SerialMgrSubModule {
    MODULE_INNERKIT = 0,
    MODULE_SERVICE,
    MODULE_JS_NAPI,
    MODULE_TAIHE,
    SERIALMGR_MODULE_BUTT,
};

constexpr unsigned int BASE_SERIAL_DOMAIN_ID = 0xD002A08;

enum SerialMgrDomainId {
    SERIALMGR_INNERKIT_DOMAIN = BASE_SERIAL_DOMAIN_ID,
    SERIALMGR_SERVICE_DOMAIN = BASE_SERIAL_DOMAIN_ID,
    SERIALMGR_JS_NAPI = BASE_SERIAL_DOMAIN_ID,
    SERIALMGR_TAIHE = BASE_SERIAL_DOMAIN_ID,
    SERIALMGR_BUTT,
};

static const struct {
    uint32_t domainId;
    const char* tag;
} SERIAL_MGR_LABEL[SERIALMGR_MODULE_BUTT] = {
    {SERIALMGR_INNERKIT_DOMAIN, "SerialSrvClient"},
    {SERIALMGR_SERVICE_DOMAIN, "SerialService"},
    {SERIALMGR_JS_NAPI, "SerialNapi"},
    {SERIALMGR_TAIHE, "SerialTaihe"},
};

#define SERIAL_HILOGF(module, ...) \
    ((void)HILOG_IMPL(             \
        LOG_CORE, LOG_FATAL, SERIAL_MGR_LABEL[module].domainId, SERIAL_MGR_LABEL[module].tag, ##__VA_ARGS__))
#define SERIAL_HILOGE(module, ...) \
    ((void)HILOG_IMPL(             \
        LOG_CORE, LOG_ERROR, SERIAL_MGR_LABEL[module].domainId, SERIAL_MGR_LABEL[module].tag, ##__VA_ARGS__))
#define SERIAL_HILOGW(module, ...) \
    ((void)HILOG_IMPL(             \
        LOG_CORE, LOG_WARN, SERIAL_MGR_LABEL[module].domainId, SERIAL_MGR_LABEL[module].tag, ##__VA_ARGS__))
#define SERIAL_HILOGI(module, ...) \
    ((void)HILOG_IMPL(             \
        LOG_CORE, LOG_INFO, SERIAL_MGR_LABEL[module].domainId, SERIAL_MGR_LABEL[module].tag, ##__VA_ARGS__))
#define SERIAL_HILOGD(module, ...) \
    ((void)HILOG_IMPL(             \
        LOG_CORE, LOG_DEBUG, SERIAL_MGR_LABEL[module].domainId, SERIAL_MGR_LABEL[module].tag, ##__VA_ARGS__))
} // namespace Serial
} // namespace OHOS

#else

#define SERIAL_HILOGF(...)
#define SERIAL_HILOGE(...)
#define SERIAL_HILOGW(...)
#define SERIAL_HILOGI(...)
#define SERIAL_HILOGD(...)

#endif // CONFIG_HILOG

#endif // SERIAL_HILOG_WRAPPER_H
