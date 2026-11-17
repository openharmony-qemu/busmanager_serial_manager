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

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ability_connect_callback_stub.h"
#include "bundle_mgr_interface.h"
#include "bundle_resource_interface.h"
#include "message_parcel.h"
#include "serial_right_db_helper.h"

// Forward declaration to avoid header dependency
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
constexpr int32_t SERIAL_DIALOG_TIMEOUT_SECONDS = 60;

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

    // OnStart: load cache (from database if exists, otherwise empty cache)
    int32_t Init();

    // OnStop: create database and save cache if cache has data
    int32_t OnStop();

    // HasRight: query cache (does not access database)
    bool HasRight(
        const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId);

    // RequestRight: dialog authorization flow
    int32_t RequestRight(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId);

    // AddDeviceRight: write to cache (does not access database)
    bool AddDeviceRight(const std::string& deviceId, const std::string& tokenIdStr);
    bool AddDeviceRight(
        const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId);

    bool AuthorizePendingRequest(
        const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, int32_t userId);
    bool RevokeDeviceRights(const std::string& deviceId);

    bool IsSystemAppOrSa();

private:
    SerialRightManager();
    ~SerialRightManager();
    DISALLOW_COPY_AND_MOVE(SerialRightManager);

    class SerialAbilityConn : public AAFwk::AbilityConnectionStub {
    public:
        explicit SerialAbilityConn(const std::map<std::string, std::string>& parameters = {},
            const std::string& deviceId = {}, int32_t userId = SERIAL_RIGHT_USERID_INVALID)
            : dialogRequestParams_(parameters), deviceId_(deviceId), userId_(userId) {}
        ~SerialAbilityConn() override = default;

        void OnAbilityConnectDone(
            const ElementName& element, const sptr<IRemoteObject>& remoteObject, int32_t resultCode) override;
        void OnAbilityDisconnectDone(const ElementName& element, int32_t resultCode) override;
        void CloseDialog();
        void CancelDialog();
        bool AcceptAuthorization(
            const std::string& requestId, const std::string& bundleName, const std::string& tokenId, int32_t userId);
        bool WaitForDisconnect(std::chrono::milliseconds timeout = std::chrono::seconds(SERIAL_DIALOG_TIMEOUT_SECONDS));
        int32_t GetLastResultCode() const {
            return lastResultCode_;
        }

    private:
        bool BuildDialogRequest(MessageParcel& data);
        void FinishDialog(int32_t resultCode);
        const std::map<std::string, std::string> dialogRequestParams_;
        const std::string deviceId_;
        const int32_t userId_;
        std::chrono::steady_clock::time_point deadline_ =
            std::chrono::steady_clock::now() + std::chrono::seconds(SERIAL_DIALOG_TIMEOUT_SECONDS);
        std::mutex remoteMutex_;
        std::condition_variable dialogFinishedCondition_;
        bool dialogFinished_ = false;
        sptr<IRemoteObject> remoteObject_ = nullptr;
        int32_t lastResultCode_ = -1;
    };

    // Authorization dialog related
    bool GetUserAgreementByDiag(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId);
    bool ShowSerialDialog(const SerialDeviceInfoForDialog& deviceInfo, const int32_t userId);
    int32_t ConnectAbility(const int32_t userId);
    bool UnShowSerialDialog();
    void ClearDialogRequest();

    // Cache operations
    bool LoadCacheFromDb();
    bool SaveCacheToDb();
    std::string BuildCacheKey(
        const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, int32_t userId);

    // Helper functions
    bool ValidateTokenAndGetHapInfo(const std::string& tokenIdStr, Security::AccessToken::AccessTokenID& tokenId,
        Security::AccessToken::HapTokenInfo& hapTokenInfoRes, int32_t& uid);
    uint64_t GetCurrentTimestamp();
    bool IsAllDigits(const std::string& str);
    sptr<IBundleMgr> GetBundleMgr();
    sptr<IBundleResource> GetBundleResMgr();
    bool GetAppName(const std::string& bundleName, std::string& appName, int32_t userId);

    static std::mutex dialogParamsMutex_;
    static std::map<std::string, std::string> dialogParams_;
    static int32_t pendingUserId_;
    static std::string pendingDeviceId_;
    static sptr<SerialAbilityConn> pendingConnection_;

    std::mutex dialogRunning_;
    std::mutex cacheMutex_;
    std::map<std::string, SerialRightFullRecord> rightCache_; // Permission cache
    bool cacheLoaded_ = false;
    sptr<SerialAbilityConn> abilityConn_ = nullptr;
};

} // namespace Serial
} // namespace OHOS

#endif // SERIAL_RIGHT_MANAGER_H
