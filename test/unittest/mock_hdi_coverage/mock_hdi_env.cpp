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

#include "mock_hdi_env.h"

#include "hilog_wrapper.h"

namespace OHOS {
namespace Serial {

sptr<OHOS::HDI::Serial::V1_0::ISerials> MockHdiEnv::TryGetRealHdiProxy()
{
    // 非阻塞探测：无真实设备/HDI 服务时立即返回 nullptr
    return OHOS::HDI::Serial::V1_0::ISerials::Get(false);
}

bool MockHdiEnv::HasRealHdiEnvironment()
{
    return TryGetRealHdiProxy() != nullptr;
}

} // namespace Serial
} // namespace OHOS
