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
#include "common_event_subscriber.h"
#include "common_event_subscribe_info.h"
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
using namespace OHOS::EventFwk;

constexpr int32_t SERIAL_RIGHT_USERID_INVALID = -1;
constexpr int32_t SERIAL_RIGHT_USERID_DEFAULT = 100;
constexpr int32_t SERIAL_RIGHT_USERID_CONSOLE = 0;
constexpr int32_t SERIAL_RIGHT_OK = 0;
constexpr int32_t SERIAL_RIGHT_FAILURE = -1;
constexpr int32_t SERIAL_RIGHT_NOP = 1;

const std::string SERIAL_RIGHT_DIALOG_TITLE = "Serial Port Authorization";

struct SerialDeviceInfoForDialog {
    std::string portName;
    std::string bundleName;
    std::string abilityName;
    std::string tokenId;
    std::string deviceName;
    std::string productId;
    std::string vendorId;
    std::string serialNumber;
    std::string deviceId;
    int32_t portId = 0;
};

class SerialRightManager {
public:
    static SerialRightManager& GetInstance();

    int32_t Init();

    bool HasRight(const std::string& portName, const std::string& bundleName,
        const std::string& tokenId, const int32_t& userId);
    int32_t RequestRight(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId);
    bool AddDeviceRight(const std::string& portName, const std::string& tokenIdStr);
    bool AddDeviceRight(const std::string& portName, const std::string& bundleName,
        const std::string& tokenId, const int32_t& userId);
    bool RemoveDeviceRight(const std::string& portName, const std::string& bundleName,
        const std::string& tokenId, const int32_t& userId);
    int32_t CancelDeviceRight(const std::string& portName, const std::string& bundleName,
        const std::string& tokenId, const int32_t& userId);
    bool RemoveDeviceAllRight(const std::string& portName);
    bool IsSystemAppOrSa();
    int32_t CleanUpRightExpired(std::vector<std::string>& ports);

    static int32_t CleanUpRightUserDeleted(int32_t& totalUsers, int32_t& deleteUsers);
    static int32_t CleanUpRightUserStopped(int32_t uid);
    static int32_t CleanUpRightAppUninstalled(int32_t uid, const std::string& bundleName);

    void OnBootComplete();
    int32_t ClearAllRights();

private:
    SerialRightManager();
    ~SerialRightManager();
    DISALLOW_COPY_AND_MOVE(SerialRightManager);

    class SerialRightEventSubscriber : public CommonEventSubscriber {
    public:
        explicit SerialRightEventSubscriber(const CommonEventSubscribeInfo& subscribeInfo);
        ~SerialRightEventSubscriber() override = default;

        void OnReceiveEvent(const CommonEventData& data) override;
    };

    class SerialAbilityConn : public AAFwk::AbilityConnectionStub {
    public:
        SerialAbilityConn() = default;
        ~SerialAbilityConn() override = default;

        void OnAbilityConnectDone(const ElementName& element,
            const sptr<IRemoteObject>& remoteObject, int32_t resultCode) override;
        void OnAbilityDisconnectDone(const ElementName& element, int32_t resultCode) override;
        void CloseDialog();
        int32_t GetLastResultCode() const { return lastResultCode_; }

    private:
        std::mutex remoteMutex_;
        sptr<IRemoteObject> remoteObject_ = nullptr;
        int32_t lastResultCode_ = -1;
    };

    bool GetUserAgreementByDiag(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId);
    bool ShowSerialDialog(const SerialDeviceInfoForDialog& deviceInfo, const int32_t userId);
    int32_t ConnectAbility(const int32_t userId);
    bool UnShowSerialDialog();
    int32_t SubscribeCommonEvents();

    sptr<IBundleMgr> GetBundleMgr();
    sptr<IBundleResource> GetBundleResMgr();
    bool GetAppName(const std::string& bundleName, std::string& appName);
    bool IsAppInstalled(int32_t uid, const std::string& bundleName);
    bool GetBundleInstallAndUpdateTime(int32_t uid, const std::string& bundleName,
        uint64_t& installTime, uint64_t& updateTime);
    void GetCurrentUserId(int32_t& uid);
    uint64_t GetCurrentTimestamp();
    bool IsAllDigits(const std::string& str);

    int32_t TidyUpRight(uint32_t choose);
    int32_t CleanUpRightTemporaryExpired(const std::string& portName);
    int32_t CleanUpRightNormalExpired(int32_t uid);
    int32_t CleanUpRightAppUninstalledInternal(int32_t uid, int32_t& totalApps, int32_t& deleteApps);
    int32_t CleanUpRightAppReinstalled(int32_t uid, uint32_t& totalApps, uint32_t& deleteApps);

    // Internal helper functions for function decomposition
    bool ValidateTokenAndGetHapInfo(const std::string& tokenIdStr,
        Security::AccessToken::AccessTokenID& tokenId,
        Security::AccessToken::HapTokenInfo& hapTokenInfoRes, int32_t& uid);
    bool BuildInfoAndSaveRight(int32_t uid, const std::string& portName,
        const std::string& bundleName, const std::string& tokenIdStr);
    bool AddDeviceRightInternal(const std::string& portName, const std::string& actualBundleName,
        const std::string& tokenId, int32_t userId, bool isCliMode);
    std::vector<std::string> FindReinstalledBundleNames(int32_t uid, const std::vector<std::string>& apps);

    static sem_t waitDialogDisappear_;
    static std::mutex dialogParamsMutex_;
    static std::map<std::string, std::string> dialogParams_;

    std::mutex dialogRunning_;
    sptr<SerialAbilityConn> abilityConn_ = nullptr;
    std::shared_ptr<SerialRightEventSubscriber> eventSubscriber_ = nullptr;
};

} // namespace Serial
} // namespace OHOS

#endif // SERIAL_RIGHT_MANAGER_H