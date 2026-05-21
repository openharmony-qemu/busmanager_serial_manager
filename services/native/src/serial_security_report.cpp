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

#include "serial_security_report.h"
#include "hilog_wrapper.h"
#include "serial_error_code.h"
#include <chrono>
#ifdef SECURITY_GUARD_ENABLE
#include "event_info.h"
#include "sg_collect_client.h"
#endif // SECURITY_GUARD_ENABLE

namespace OHOS {
namespace Serial {

uint64_t SerialSecurityReport::GetCurrentTime()
{
    // get ms value since unix timestamp
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void SerialSecurityReport::ReportSecurityInfo(int64_t eventId, std::string version, nlohmann::json& json, bool isAsync)
{
#ifdef SECURITY_GUARD_ENABLE
    auto eventInfo = std::make_shared<Security::SecurityGuard::EventInfo>(eventId, version, json.dump());
    if (isAsync) {
        (void)Security::SecurityGuard::NativeDataCollectKit::ReportSecurityInfoAsync(eventInfo);
    } else {
        int32_t ret = Security::SecurityGuard::NativeDataCollectKit::ReportSecurityInfo(eventInfo);
        if (ret != SERIAL_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s: failed to report security with ret = %{public}d", __func__, ret);
        }
    }
#else
    SERIAL_HILOGD(MODULE_SERVICE, "%{public}s: Security Guard not enabled", __func__);
#endif // SECURITY_GUARD_ENABLE
}

} // namespace Serial
} // namespace OHOS