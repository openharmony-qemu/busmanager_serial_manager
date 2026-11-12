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

/**
 * @file mock_hdi_env.h
 * @brief 真实设备环境探测辅助类
 * @details 用于 Mock HDI 覆盖测试：全部用例基于 Mock HDI 运行，不依赖真实串口设备。
 *          仅当个别用例需要探测真实 HDI 服务时使用本类；探测失败（无真实设备/
 *          无 HDI 服务）时，调用方按需求默认返回 success，不判失败。
 * @since 2026-05-12
 */

#ifndef MOCK_HDI_ENV_H
#define MOCK_HDI_ENV_H

#include "v1_0/iserials.h"
#include <iremote_object.h>

namespace OHOS {
namespace Serial {

class MockHdiEnv {
public:
    MockHdiEnv() = default;
    ~MockHdiEnv() = default;

    /**
     * @brief 尝试获取真实 HDI 服务代理（非阻塞探测）
     * @return 成功返回代理指针；无真实设备/无 HDI 服务时返回 nullptr
     */
    static sptr<OHOS::HDI::Serial::V1_0::ISerials> TryGetRealHdiProxy();

    /**
     * @brief 是否存在真实 HDI 服务环境（即存在真实串口设备）
     */
    static bool HasRealHdiEnvironment();
};

} // namespace Serial
} // namespace OHOS

#endif // MOCK_HDI_ENV_H
