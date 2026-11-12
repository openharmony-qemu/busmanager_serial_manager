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
 * @file mock_hdi_test_helper.h
 * @brief Mock HDI 覆盖测试共享辅助函数
 * @details 提供「向服务注入测试端口上下文」的通用能力：
 *          - tokenId 与当前调用方一致，使 CheckPortAccessPermission 确定性通过，
 *            从而解锁已打开端口的 HDI 错误注入测试（无需真实权限环境）
 *          - 返回注入的 MockSerialDevice 指针，便于按用例注入 HDI 错误码
 *          所有场景均基于 Mock HDI，可在无真实设备环境执行。
 * @since 2026-05-12
 */

#ifndef MOCK_HDI_TEST_HELPER_H
#define MOCK_HDI_TEST_HELPER_H

#include <chrono>
#include <memory>
#include <string>

#include "mock_serial_hdi.h"
#include "serial_service.h"

namespace OHOS {
namespace Serial {

/**
 * @brief 构造测试端口上下文并注入服务
 * @param service 目标服务实例（须已注入 Mock HDI 代理）
 * @param portName 端口名（与 mock 设备枚举结果一致，不带 /dev/ 前缀）
 * @param callback 服务端→客户端回调（Mock 实现）
 * @param config 打开时使用的配置
 * @return 注入上下文持有的 MockSerialDevice，用于按用例注入 HDI 错误码
 */
inline sptr<MockSerialDevice> InsertTestDeviceContext(const sptr<SerialService> &service, const std::string &portName,
    const sptr<ISerialServiceCallback> &callback, const SerialConfig &config)
{
    auto context = std::make_shared<DeviceContext>();
    context->portName = portName;
    context->serviceCallback = callback;
    context->hdiCallback = new SerialHdiCallback(callback, portName);
    context->hdiDevice = new MockSerialDevice();
    context->config = config;
    context->openTime = std::chrono::steady_clock::now();
    // 与当前调用方 tokenId 保持一致，使权限校验确定性通过
    context->tokenId = service->GetCallingTokenId();
    service->deviceContexts_[portName] = context;
    return sptr<MockSerialDevice>(static_cast<MockSerialDevice *>(context->hdiDevice.GetRefPtr()));
}

/**
 * @brief 防御性清理：注销延时卸载定时器并清空端口上下文
 * @details 避免卸载定时器在服务对象析构后触发；各测试夹具 TearDown 调用
 */
inline void CleanupTestService(const sptr<SerialService> &service)
{
    if (service == nullptr) {
        return;
    }
    if (service->unloadTimerId_ != 0) {
        service->unloadTimer_.Unregister(service->unloadTimerId_);
        service->unloadTimerId_ = 0;
    }
    service->deviceContexts_.clear();
    service->SetSerialManagePolicy(false);
}

} // namespace Serial
} // namespace OHOS

#endif // MOCK_HDI_TEST_HELPER_H
