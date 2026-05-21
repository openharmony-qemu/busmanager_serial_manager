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

#ifndef SERIAL_RIGHT_MANAGER_H
#define SERIAL_RIGHT_MANAGER_H

#include <map>
#include <mutex>
#include <semaphore.h>
#include <string>
#include <vector>

#include "ability_connect_callback_stub.h"
#include "bundle_mgr_interface.h"
#include "bundle_resource_interface.h"
#include "serial_right_db_helper.h"

// 前向声明，避免头文件依赖
namespace OHOS {
namespace Security {
namespace AccessToken {
using AccessTokenID = unsigned int;
class HapTokenInfo;
} // namespace AccessToken
} // namespace Security
} // namespace OHOS

namespace OHOS {
namespace Serial {

using namespace OHOS::AppExecFwk;

constexpr int32_t SERIAL_RIGHT_USERID_INVALID = -1;
constexpr int32_t SERIAL_RIGHT_USERID_DEFAULT = 100;
constexpr int32_t SERIAL_RIGHT_USERID_CONSOLE = 0;
constexpr int32_t SERIAL_RIGHT_OK = 0;
constexpr int32_t SERIAL_RIGHT_FAILURE = -1;
constexpr int32_t SERIAL_RIGHT_NOP = 1;

const std::string SERIAL_RIGHT_DIALOG_TITLE = "Serial Port Authorization";

struct SerialDeviceInfoForDialog {
    std::string deviceId;
    std::string bundleName;
    std::string abilityName;
    std::string tokenId;
    std::string deviceName;
    std::string productId;
    std::string vendorId;
    std::string serialNumber;
    std::string portName;
    int32_t portId = 0;
};

class SerialRightManager {
public:
    static SerialRightManager& GetInstance();

    // OnStart: 加载缓存（数据库存在则加载，不存在则空缓存）
    int32_t Init();

    // OnStop: 缓存有数据时创建数据库并保存
    int32_t OnStop();

    // HasRight: 查缓存（不访问数据库）
    bool HasRight(
        const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId);

    // RequestRight: 弹窗授权流程
    int32_t RequestRight(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId);

    // AddDeviceRight: 写缓存（不访问数据库）
    bool AddDeviceRight(const std::string& deviceId, const std::string& tokenIdStr);
    bool AddDeviceRight(
        const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId);

    bool IsSystemAppOrSa();

private:
    SerialRightManager();
    ~SerialRightManager();
    DISALLOW_COPY_AND_MOVE(SerialRightManager);

    class SerialAbilityConn : public AAFwk::AbilityConnectionStub {
    public:
        SerialAbilityConn() = default;
        ~SerialAbilityConn() override = default;

        void OnAbilityConnectDone(
            const ElementName& element, const sptr<IRemoteObject>& remoteObject, int32_t resultCode) override;
        void OnAbilityDisconnectDone(const ElementName& element, int32_t resultCode) override;
        void CloseDialog();
        int32_t GetLastResultCode() const
        {
            return lastResultCode_;
        }

    private:
        std::mutex remoteMutex_;
        sptr<IRemoteObject> remoteObject_ = nullptr;
        int32_t lastResultCode_ = -1;
    };

    // 授权弹窗相关
    bool GetUserAgreementByDiag(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId);
    bool ShowSerialDialog(const SerialDeviceInfoForDialog& deviceInfo, const int32_t userId);
    int32_t ConnectAbility(const int32_t userId);
    bool UnShowSerialDialog();

    // 缓存操作
    bool LoadCacheFromDb();
    bool SaveCacheToDb();
    std::string BuildCacheKey(
        const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, int32_t userId);

    // 辅助函数
    bool ValidateTokenAndGetHapInfo(const std::string& tokenIdStr, Security::AccessToken::AccessTokenID& tokenId,
        Security::AccessToken::HapTokenInfo& hapTokenInfoRes, int32_t& uid);
    uint64_t GetCurrentTimestamp();
    bool IsAllDigits(const std::string& str);
    sptr<IBundleMgr> GetBundleMgr();
    sptr<IBundleResource> GetBundleResMgr();
    bool GetAppName(const std::string& bundleName, std::string& appName, int32_t userId);

    static sem_t waitDialogDisappear_;
    static std::mutex dialogParamsMutex_;
    static std::map<std::string, std::string> dialogParams_;

    std::mutex dialogRunning_;
    std::mutex cacheMutex_;
    std::map<std::string, SerialRightFullRecord> rightCache_; // 权限缓存
    bool cacheLoaded_ = false;
    sptr<SerialAbilityConn> abilityConn_ = nullptr;
};

} // namespace Serial
} // namespace OHOS

#endif // SERIAL_RIGHT_MANAGER_H