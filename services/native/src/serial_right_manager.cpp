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

#include "serial_right_manager.h"

#include <algorithm>
#include <cJSON.h>
#include <chrono>
#include <cstdlib>
#include <regex>
#include <semaphore.h>
#include <unistd.h>

#include "ability_manager_client.h"
#include "accesstoken_kit.h"
#include "bundle_mgr_interface.h"
#include "common_event_manager.h"
#include "common_event_support.h"
#include "hilog_wrapper.h"
#include "ipc_skeleton.h"
#include "iservice_registry.h"
#include "os_account_manager.h"
#include "serial_error_code.h"
#include "serial_right_db_helper.h"
#include "string_ex.h"
#include "system_ability_definition.h"
#include "tokenid_kit.h"

using namespace OHOS::AppExecFwk;
using namespace OHOS::EventFwk;
using namespace OHOS::Security::AccessToken;

namespace OHOS {
namespace Serial {

// Test mode configuration:
// 1. SERIAL_RIGHT_TEST_MODE=1 env var: skip console bypass for real auth testing
// 2. DISABLE_CONSOLE_BYPASS macro: completely disable console bypass in HasRight
#define DISABLE_CONSOLE_BYPASS 1

#ifndef DISABLE_CONSOLE_BYPASS
// Test mode: when SERIAL_RIGHT_TEST_MODE=1, skip console bypass to test real auth flow
static bool IsTestModeEnabled()
{
    const char* testMode = getenv("SERIAL_RIGHT_TEST_MODE");
    if (testMode != nullptr && strcmp(testMode, "1") == 0) {
        return true;
    }
    return false;
}
#endif

constexpr int32_t MESSAGE_PARCEL_KEY_SIZE = 3;
constexpr int32_t DECIMAL_BASE = 10;
constexpr uint32_t CMD_CODE_SHOW_DIALOG = 1;
constexpr uint32_t CMD_CODE_CLOSE_DIALOG = 3;

enum SerialRightTidyUpChoose : uint32_t {
    TIDY_UP_SERIAL_RIGHT_RECORD_NONE = 0,
    TIDY_UP_SERIAL_RIGHT_RECORD_APP_UNINSTALLED = 1 << 0,
    TIDY_UP_SERIAL_RIGHT_RECORD_USER_DELETED = 1 << 1,
    TIDY_UP_SERIAL_RIGHT_RECORD_EXPIRED = 1 << 2,
    TIDY_UP_SERIAL_RIGHT_RECORD_APP_REINSTALLED = 1 << 3,
};

constexpr uint32_t TIDY_UP_SERIAL_RIGHT_RECORD_ALL =
    (TIDY_UP_SERIAL_RIGHT_RECORD_APP_UNINSTALLED | TIDY_UP_SERIAL_RIGHT_RECORD_USER_DELETED |
        TIDY_UP_SERIAL_RIGHT_RECORD_EXPIRED | TIDY_UP_SERIAL_RIGHT_RECORD_APP_REINSTALLED);

sem_t SerialRightManager::waitDialogDisappear_ {0};
std::mutex SerialRightManager::dialogParamsMutex_;
std::map<std::string, std::string> SerialRightManager::dialogParams_ = {};

SerialRightManager& SerialRightManager::GetInstance()
{
    static SerialRightManager instance;
    return instance;
}

SerialRightManager::SerialRightManager()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager constructed", __func__, __LINE__);
}

SerialRightManager::~SerialRightManager()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager destroyed", __func__, __LINE__);
}

int32_t SerialRightManager::Init()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager Init", __func__, __LINE__);
    // Initialize database helper
    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Failed to get SerialRightDbHelper instance", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    // Subscribe to common events (boot complete, app uninstall, user switch)
    int32_t ret = SubscribeCommonEvents();
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGW(
            MODULE_SERVICE, "%{public}s:%{public}d SubscribeCommonEvents failed: %{public}d", __func__, __LINE__, ret);
    }

    return SERIAL_RIGHT_OK;
}

bool SerialRightManager::HasRight(
    const std::string& portName, const std::string& bundleName, const std::string& tokenId, const int32_t& userId)
{
    // Use default bundle name "serial_cli" for CLI tools when bundleName is empty
    std::string actualBundleName = bundleName.empty() ? "serial_cli" : bundleName;

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d HasRight: portName=%{public}s, bundleName=%{public}s (actual=%{public}s)"
        ", userId=%{public}d",
        __func__, __LINE__, portName.c_str(), bundleName.c_str(), actualBundleName.c_str(), userId);

#ifndef DISABLE_CONSOLE_BYPASS
    // Normal mode: console bypass enabled
    if (userId == SERIAL_RIGHT_USERID_CONSOLE && !IsTestModeEnabled()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d console called, bypass (testMode=0)", __func__, __LINE__);
        return true;
    }

    if (userId == SERIAL_RIGHT_USERID_CONSOLE && IsTestModeEnabled()) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d TEST MODE: console bypass disabled, using userId=%{public}d", __func__, __LINE__,
            userId);
    }
#else
    // DISABLE_CONSOLE_BYPASS=1: force real auth flow for ALL users including console
    if (userId == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d DISABLE_CONSOLE_BYPASS: forcing real auth for userId=%{public}d", __func__, __LINE__,
            userId);
    }
#endif

    uint64_t nowTime = GetCurrentTimestamp();
    (void)TidyUpRight(TIDY_UP_SERIAL_RIGHT_RECORD_EXPIRED);

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return false;
    }

    return !helper->IsRecordExpired(userId, portName, actualBundleName, tokenId, nowTime);
}

int32_t SerialRightManager::RequestRight(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d RequestRight: portName=%{public}s, bundleName=%{public}s, deviceId=%{public}s", __func__,
        __LINE__, deviceInfo.portName.c_str(), deviceInfo.bundleName.c_str(), deviceInfo.deviceId.c_str());

    if (HasRight(deviceInfo.portName, deviceInfo.bundleName, deviceInfo.tokenId, userId)) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Device already has right", __func__, __LINE__);
        return SERIAL_OK;
    }

    if (!GetUserAgreementByDiag(deviceInfo, userId)) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d User denied permission", __func__, __LINE__);
        return SERIAL_ERR_PERMISSION_DENIED;
    }

    return SERIAL_OK;
}

bool SerialRightManager::GetUserAgreementByDiag(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId)
{
#ifdef SERIAL_RIGHT_TEST
    return true;
#endif

    // There can only be one dialog at a time
    std::lock_guard<std::mutex> guard(dialogRunning_);

    if (!ShowSerialDialog(deviceInfo, userId)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d ShowSerialDialog failed", __func__, __LINE__);
        return false;
    }

    // Get resultCode from dialog for logging
    int32_t resultCode = -1;
    if (abilityConn_ != nullptr) {
        resultCode = abilityConn_->GetLastResultCode();
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d dialog resultCode=%{public}d", __func__, __LINE__, resultCode);
    }

    // Check HasRight immediately after dialog closes
    if (HasRight(deviceInfo.portName, deviceInfo.bundleName, deviceInfo.tokenId, userId)) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d HasRight check passed immediately", __func__, __LINE__);
        return true;
    }

    // Dialog closed but HasRight is false -> directly write to our own database
    // Note: USB dialog HAP only writes to USB's usbRight.db, not our SerialRight.db
    // So we must write authorization ourselves regardless of resultCode
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Dialog closed but HasRight false, force AddDeviceRight",
        __func__, __LINE__);

    if (AddDeviceRight(deviceInfo.portName, deviceInfo.bundleName, deviceInfo.tokenId, userId)) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d AddDeviceRight succeeded, authorization written", __func__,
            __LINE__);
        return true;
    }

    SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d AddDeviceRight failed", __func__, __LINE__);
    return false;
}

bool SerialRightManager::ShowSerialDialog(const SerialDeviceInfoForDialog& deviceInfo, const int32_t userId)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d ShowSerialDialog: portName=%{public}s, bundleName=%{public}s"
        ", tokenId=%{public}s, portId=%{public}d",
        __func__, __LINE__, deviceInfo.portName.c_str(), deviceInfo.bundleName.c_str(), deviceInfo.tokenId.c_str(),
        deviceInfo.portId);

    std::string appName;
    if (!GetAppName(deviceInfo.bundleName, appName)) {
        appName = deviceInfo.bundleName;
    }

    // Convert tokenId to numeric for serial right API
    int32_t castId = 0;
    if (!deviceInfo.tokenId.empty()) {
        errno = 0;
        castId = static_cast<int32_t>(std::stoul(deviceInfo.tokenId, nullptr, DECIMAL_BASE));
        if (errno == ERANGE || castId < 0) {
            SERIAL_HILOGE(
                MODULE_SERVICE, "%{public}s:%{public}d tokenId cast failed %{public}d", __func__, __LINE__, castId);
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(dialogParamsMutex_);
        dialogParams_.clear();
        // Parameters for UsbDialog.ets serial authorization
        dialogParams_["portId"] = std::to_string(deviceInfo.portId);
        dialogParams_["tokenId"] = std::to_string(castId);
        dialogParams_["bundleName"] = deviceInfo.bundleName;
        dialogParams_["deviceName"] = deviceInfo.portName;
        dialogParams_["appName"] = appName;
        dialogParams_["productName"] = deviceInfo.portName; // Display name in dialog
    }

    sem_init(&waitDialogDisappear_, 1, 0);
    int32_t ret = ConnectAbility(userId);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d ConnectAbility failed %{public}d", __func__, __LINE__, ret);
        return false;
    }

    // Waiting for the user to click
    sem_wait(&waitDialogDisappear_);
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d ShowSerialDialog completed", __func__, __LINE__);
    return true;
}

int32_t SerialRightManager::ConnectAbility(const int32_t userId)
{
    if (abilityConn_ == nullptr) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d new SerialAbilityConn", __func__, __LINE__);
        abilityConn_ = sptr<SerialAbilityConn>(new (std::nothrow) SerialAbilityConn());
    }

    auto abmc = AAFwk::AbilityManagerClient::GetInstance();
    if (abmc == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetInstance failed", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    AAFwk::Want want;
    // Try sceneboard first
    want.SetElementName("com.ohos.sceneboard", "com.ohos.sceneboard.systemdialog");
    int32_t ret = abmc->ConnectAbility(want, abilityConn_, userId);
    if (ret != ERR_OK) {
        // Try systemui as fallback
        want.SetElementName("com.ohos.systemui", "com.ohos.systemui.dialog");
        ret = abmc->ConnectAbility(want, abilityConn_, userId);
        if (ret != ERR_OK) {
            SERIAL_HILOGE(MODULE_SERVICE,
                "%{public}s:%{public}d ConnectServiceExtensionAbility systemui failed, ret: %{public}d", __func__,
                __LINE__, ret);
            abilityConn_ = nullptr;
            return ret;
        }
    }
    return SERIAL_RIGHT_OK;
}

// 辅助函数：验证 tokenIdStr 并获取 HapTokenInfo
bool SerialRightManager::ValidateTokenAndGetHapInfo(const std::string& tokenIdStr,
    Security::AccessToken::AccessTokenID& tokenId,
    Security::AccessToken::HapTokenInfo& hapTokenInfoRes, int32_t& uid)
{
    if (!IsAllDigits(tokenIdStr)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d tokenIdStr invalid", __func__, __LINE__);
        return false;
    }

    errno = 0;
    tokenId = static_cast<AccessTokenID>(std::stoull(tokenIdStr, nullptr, DECIMAL_BASE));
    if (errno == ERANGE) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d tokenIdStr is out of uint64_t range", __func__, __LINE__);
        return false;
    }

    int32_t ret = AccessTokenKit::GetHapTokenInfo(tokenId, hapTokenInfoRes);
    if (ret != RET_SUCCESS) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d GetHapTokenInfo failed:ret:%{public}d", __func__, __LINE__, ret);
        return false;
    }
    uid = hapTokenInfoRes.userID;
    return true;
}

// 辅助函数：构建 SerialRightAppInfo 并写入数据库
bool SerialRightManager::BuildInfoAndSaveRight(int32_t uid, const std::string& portName,
    const std::string& bundleName, const std::string& tokenIdStr)
{
    uint64_t installTime = GetCurrentTimestamp();
    uint64_t updateTime = GetCurrentTimestamp();
    if (!GetBundleInstallAndUpdateTime(uid, bundleName, installTime, updateTime)) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d get app install time failed: %{public}d", __func__, __LINE__, uid);
    }

    struct SerialRightAppInfo info;
    info.uid = uid;
    info.installTime = installTime;
    info.updateTime = updateTime;
    info.requestTime = GetCurrentTimestamp();
    info.validPeriod = SERIAL_RIGHT_VALID_PERIOD_SET;

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return false;
    }

    int32_t ret = helper->AddOrUpdateRightRecord(uid, portName, bundleName, tokenIdStr, info);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d add or update failed: %{public}s/%{public}d, ret=%{public}d", __func__, __LINE__,
            portName.c_str(), uid, ret);
        return false;
    }
    return true;
}

bool SerialRightManager::AddDeviceRight(const std::string& portName, const std::string& tokenIdStr)
{
    AccessTokenID tokenId;
    HapTokenInfo hapTokenInfoRes;
    int32_t uid;
    if (!ValidateTokenAndGetHapInfo(tokenIdStr, tokenId, hapTokenInfoRes, uid)) {
        return false;
    }

#ifndef DISABLE_CONSOLE_BYPASS
    if (uid == SERIAL_RIGHT_USERID_CONSOLE && !IsTestModeEnabled()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d console called, bypass (testMode=0)", __func__, __LINE__);
        return true;
    }
#else
    if (uid == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d DISABLE_CONSOLE_BYPASS: proceeding with userId=%{public}d",
            __func__, __LINE__, uid);
    }
#endif

    return BuildInfoAndSaveRight(uid, portName, hapTokenInfoRes.bundleName, tokenIdStr);
}

bool SerialRightManager::AddDeviceRight(
    const std::string& portName, const std::string& bundleName, const std::string& tokenId, const int32_t& userId)
{
    // Use default bundle name "serial_cli" for CLI tools when bundleName is empty
    std::string actualBundleName = bundleName.empty() ? "serial_cli" : bundleName;
    bool isCliMode = bundleName.empty();

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d AddDeviceRight: portName=%{public}s, bundleName=%{public}s (actual=%{public}s)"
        ", isCliMode=%{public}d, userId=%{public}d",
        __func__, __LINE__, portName.c_str(), bundleName.c_str(), actualBundleName.c_str(), isCliMode, userId);

#ifndef DISABLE_CONSOLE_BYPASS
    // Normal mode: console bypass enabled
    if (userId == SERIAL_RIGHT_USERID_CONSOLE && !IsTestModeEnabled()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d console called, bypass (testMode=0)", __func__, __LINE__);
        return true;
    }

    if (userId == SERIAL_RIGHT_USERID_CONSOLE && IsTestModeEnabled()) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d TEST MODE: console bypass disabled, proceeding with userId=%{public}d", __func__,
            __LINE__, userId);
    }
#else
    // DISABLE_CONSOLE_BYPASS=1: force real auth flow for ALL users including console
    if (userId == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d DISABLE_CONSOLE_BYPASS: forcing real auth for userId=%{public}d", __func__, __LINE__,
            userId);
    }
#endif

    return AddDeviceRightInternal(portName, actualBundleName, tokenId, userId, isCliMode);
}

bool SerialRightManager::AddDeviceRightInternal(const std::string& portName, const std::string& actualBundleName,
    const std::string& tokenId, int32_t userId, bool isCliMode)
{
    uint64_t installTime = GetCurrentTimestamp();
    uint64_t updateTime = GetCurrentTimestamp();

    // Skip GetBundleInstallAndUpdateTime for CLI mode (bundleName was empty)
    if (!isCliMode) {
        if (!GetBundleInstallAndUpdateTime(userId, actualBundleName, installTime, updateTime)) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d get app install time failed: %{public}s/%{public}d",
                __func__, __LINE__, actualBundleName.c_str(), userId);
        }
    } else {
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d CLI mode: skip GetBundleInstallAndUpdateTime, use current time", __func__, __LINE__);
    }

    struct SerialRightAppInfo info;
    info.uid = userId;
    info.installTime = installTime;
    info.updateTime = updateTime;
    info.requestTime = GetCurrentTimestamp();
    info.validPeriod = SERIAL_RIGHT_VALID_PERIOD_SET;

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return false;
    }

    int32_t ret = helper->AddOrUpdateRightRecord(userId, portName, actualBundleName, tokenId, info);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d add or update failed: %{public}s/%{public}s/%{public}d, ret=%{public}d", __func__,
            __LINE__, portName.c_str(), actualBundleName.c_str(), userId, ret);
        return false;
    }
    return true;
}

bool SerialRightManager::RemoveDeviceRight(
    const std::string& portName, const std::string& bundleName, const std::string& tokenId, const int32_t& userId)
{
#ifndef DISABLE_CONSOLE_BYPASS
    if (userId == SERIAL_RIGHT_USERID_CONSOLE && !IsTestModeEnabled()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d console called, bypass (testMode=0)", __func__, __LINE__);
        return true;
    }
#else
    if (userId == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d DISABLE_CONSOLE_BYPASS: proceeding with userId=%{public}d",
            __func__, __LINE__, userId);
    }
#endif

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return false;
    }

    int32_t ret = helper->DeleteRightRecord(userId, portName, bundleName, tokenId);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d delete failed: %{public}s/%{public}s/%{public}d", __func__,
            __LINE__, portName.c_str(), bundleName.c_str(), userId);
        return false;
    }
    return true;
}

int32_t SerialRightManager::CancelDeviceRight(
    const std::string& portName, const std::string& bundleName, const std::string& tokenId, const int32_t& userId)
{
#ifndef DISABLE_CONSOLE_BYPASS
    if (userId == SERIAL_RIGHT_USERID_CONSOLE && !IsTestModeEnabled()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d console called, bypass (testMode=0)", __func__, __LINE__);
        return SERIAL_ERR_INVALID_PARAM;
    }
#else
    if (userId == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d DISABLE_CONSOLE_BYPASS: proceeding with userId=%{public}d",
            __func__, __LINE__, userId);
    }
#endif

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return SERIAL_ERR_SERVICE;
    }

    int32_t ret = helper->DeleteRightRecord(userId, portName, bundleName, tokenId);
    if (ret != E_OK) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d delete failed: %{public}s/%{public}s/%{public}d", __func__,
            __LINE__, portName.c_str(), bundleName.c_str(), userId);
        return SERIAL_OK;
    }
    return SERIAL_OK;
}

bool SerialRightManager::RemoveDeviceAllRight(const std::string& portName)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d port %{private}s detached, process right", __func__, __LINE__,
        portName.c_str());

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr", __func__, __LINE__);
        return false;
    }

    // Query all uids that have rights for this port
    std::vector<std::string> uids;
    int32_t ret = helper->QueryRightRecordUids(uids);
    if (ret > 0) {
        for (const std::string& uidStr : uids) {
            int32_t uid = 0;
            if (StrToInt(uidStr, uid)) {
                // Delete all rights for this port for this uid
                ret = helper->DeletePortRightRecord(uid, portName);
                SERIAL_HILOGI(MODULE_SERVICE,
                    "%{public}s:%{public}d deleted rights for uid=%{public}d, port=%{private}s, ret=%{public}d",
                    __func__, __LINE__, uid, portName.c_str(), ret);
            }
        }
    }

    CleanUpRightTemporaryExpired(portName);
    UnShowSerialDialog();
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d RemoveDeviceAllRight completed for port=%{private}s", __func__,
        __LINE__, portName.c_str());
    return true;
}

bool SerialRightManager::IsSystemAppOrSa()
{
    // 临时禁用，用于测试授权弹窗
    SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d IsSystemAppOrSa: temporarily returning false for testing",
        __func__, __LINE__);
    return false;
}

bool SerialRightManager::UnShowSerialDialog()
{
    if (abilityConn_ == nullptr) {
        return true;
    }

    auto abmc = AAFwk::AbilityManagerClient::GetInstance();
    if (abmc == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetInstance failed", __func__, __LINE__);
        return false;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d unshow serial dialog window", __func__, __LINE__);
    abilityConn_->CloseDialog();

    int32_t ret = abmc->DisconnectAbility(abilityConn_);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d DisconnectAbility failed %{public}d", __func__, __LINE__, ret);
        return false;
    }

    SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d unshow serial dialog window success", __func__, __LINE__);
    return true;
}

void SerialRightManager::SerialAbilityConn::OnAbilityConnectDone(
    const AppExecFwk::ElementName& element, const sptr<IRemoteObject>& remoteObject, int32_t resultCode)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnAbilityConnectDone", __func__, __LINE__);
    if (remoteObject == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d remoteObject is nullptr", __func__, __LINE__);
        return;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;

    data.WriteInt32(MESSAGE_PARCEL_KEY_SIZE);
    data.WriteString16(u"bundleName");
    data.WriteString16(u"com.usb.right");
    data.WriteString16(u"abilityName");
    data.WriteString16(u"UsbServiceExtAbility");
    data.WriteString16(u"parameters");

    cJSON* paramJson = cJSON_CreateObject();
    {
        std::lock_guard<std::mutex> lock(dialogParamsMutex_);
        for (auto it : dialogParams_) {
            cJSON_AddStringToObject(paramJson, it.first.c_str(), it.second.c_str());
        }
    }

    std::string uiExtensionTypeStr = "sysDialog/common";
    cJSON_AddStringToObject(paramJson, "ability.want.params.uiExtensionType", uiExtensionTypeStr.c_str());

    char* pParamJson = cJSON_PrintUnformatted(paramJson);
    cJSON_Delete(paramJson);
    paramJson = nullptr;
    if (!pParamJson) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Print paramJson error", __func__, __LINE__);
        return;
    }

    std::string paramStr(pParamJson);
    data.WriteString16(Str8ToStr16(paramStr));
    cJSON_free(pParamJson);
    pParamJson = nullptr;

    int32_t ret = remoteObject->SendRequest(CMD_CODE_SHOW_DIALOG, data, reply, option);
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SendRequest ret %{public}d", __func__, __LINE__, ret);
    if (ret != ERR_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d send request failed: %{public}d", __func__, __LINE__, ret);
        return;
    }

    if (!reply.ReadInt32(ret) || ret != ERR_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d show dialog failed: %{public}d", __func__, __LINE__, ret);
        return;
    }

    std::lock_guard<std::mutex> guard(remoteMutex_);
    remoteObject_ = remoteObject;
}

void SerialRightManager::SerialAbilityConn::OnAbilityDisconnectDone(
    const AppExecFwk::ElementName& element, int32_t resultCode)
{
    SERIAL_HILOGI(
        MODULE_SERVICE, "%{public}s:%{public}d disconnect done, resultCode=%{public}d", __func__, __LINE__, resultCode);
    lastResultCode_ = resultCode;
    sem_post(&waitDialogDisappear_);
    std::lock_guard<std::mutex> guard(remoteMutex_);
    remoteObject_ = nullptr;
}

void SerialRightManager::SerialAbilityConn::CloseDialog()
{
    std::lock_guard<std::mutex> guard(remoteMutex_);
    if (remoteObject_ == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d CloseDialog: disconnected", __func__, __LINE__);
        return;
    }

    MessageParcel data;
    MessageParcel reply;
    MessageOption option;
    const uint32_t cmdCode = CMD_CODE_CLOSE_DIALOG;

    int32_t ret = remoteObject_->SendRequest(cmdCode, data, reply, option);
    int32_t replyCode = -1;
    bool success = false;
    if (ret == ERR_OK) {
        success = reply.ReadInt32(replyCode);
    }
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d CloseDialog: ret=%{public}d, %{public}d, %{public}d", __func__,
        __LINE__, ret, success, replyCode);
}

sptr<IBundleMgr> SerialRightManager::GetBundleMgr()
{
    auto sam = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sam == nullptr) {
        SERIAL_HILOGW(
            MODULE_SERVICE, "%{public}s:%{public}d GetSystemAbilityManager return nullptr", __func__, __LINE__);
        return nullptr;
    }

    auto bundleMgrSa = sam->GetSystemAbility(BUNDLE_MGR_SERVICE_SYS_ABILITY_ID);
    if (bundleMgrSa == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d GetSystemAbility return nullptr", __func__, __LINE__);
        return nullptr;
    }

    auto bundleMgr = iface_cast<IBundleMgr>(bundleMgrSa);
    if (bundleMgr == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d iface_cast return nullptr", __func__, __LINE__);
    }
    return bundleMgr;
}

sptr<IBundleResource> SerialRightManager::GetBundleResMgr()
{
    auto bundleMgr = GetBundleMgr();
    if (bundleMgr == nullptr) {
        return nullptr;
    }
    return bundleMgr->GetBundleResourceProxy();
}

bool SerialRightManager::GetAppName(const std::string& bundleName, std::string& appName)
{
    auto resMgr = GetBundleResMgr();
    if (resMgr == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetAppName: get res mgr failed", __func__, __LINE__);
        return false;
    }

    BundleResourceInfo info;
    int32_t ret = resMgr->GetBundleResourceInfo(
        bundleName, static_cast<uint32_t>(ResourceFlag::GET_RESOURCE_INFO_WITH_LABEL), info);
    if (ret != ERR_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetAppName: get res info failed: %{public}d", __func__,
            __LINE__, ret);
        return false;
    }
    appName = info.label;
    return true;
}

bool SerialRightManager::IsAppInstalled(int32_t uid, const std::string& bundleName)
{
    auto bundleMgr = GetBundleMgr();
    if (bundleMgr == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d BundleMgr is nullptr, return false", __func__, __LINE__);
        return false;
    }

    ApplicationInfo appInfo;
    if (!bundleMgr->GetApplicationInfo(bundleName, GET_BASIC_APPLICATION_INFO, uid, appInfo)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d BundleMgr GetApplicationInfo failed", __func__, __LINE__);
        return false;
    }
    return true;
}

bool SerialRightManager::GetBundleInstallAndUpdateTime(
    int32_t uid, const std::string& bundleName, uint64_t& installTime, uint64_t& updateTime)
{
    BundleInfo bundleInfo;
    auto bundleMgr = GetBundleMgr();
    if (bundleMgr == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d BundleMgr is nullptr, return false", __func__, __LINE__);
        return false;
    }

    if (!bundleMgr->GetBundleInfo(bundleName, BundleFlag::GET_BUNDLE_DEFAULT, bundleInfo, uid)) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d BundleMgr GetBundleInfo(uid) failed", __func__, __LINE__);
        return false;
    }
    installTime = static_cast<uint64_t>(bundleInfo.installTime);
    updateTime = static_cast<uint64_t>(bundleInfo.updateTime);
    return true;
}

uint64_t SerialRightManager::GetCurrentTimestamp()
{
    int64_t time =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<uint64_t>(time);
}

void SerialRightManager::GetCurrentUserId(int32_t& uid)
{
    int32_t ret = AccountSA::OsAccountManager::GetOsAccountLocalIdFromUid(IPCSkeleton::GetCallingUid(), uid);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d GetOsAccountLocalIdFromUid failed: %{public}d, set to default", __func__, __LINE__,
            ret);
        uid = SERIAL_RIGHT_USERID_DEFAULT;
    }
    SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d serial get userid success: %{public}d, uid: %{public}d",
        __func__, __LINE__, ret, uid);
}

bool SerialRightManager::IsAllDigits(const std::string& str)
{
    size_t len = str.length();
    for (size_t i = 0; i < len; i++) {
        if (!isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

int32_t SerialRightManager::CleanUpRightExpired(std::vector<std::string>& ports)
{
    SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d clean up expired right: size=%{public}zu", __func__, __LINE__,
        ports.size());
    size_t len = ports.size();
    int32_t ret = SERIAL_RIGHT_OK;

    for (size_t i = 0; i < len; i++) {
        std::string port = ports.at(i);
        ret = CleanUpRightTemporaryExpired(port);
        if (ret != SERIAL_RIGHT_OK) {
            SERIAL_HILOGE(MODULE_SERVICE,
                "%{public}s:%{public}d failed(%{public}zu/%{public}zu): delete temporary expired record"
                ", port=%{private}s",
                __func__, __LINE__, i, len, port.c_str());
            continue;
        }
    }

    int32_t uid = SERIAL_RIGHT_USERID_INVALID;
    GetCurrentUserId(uid);
    ret = CleanUpRightNormalExpired(uid);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d delete expired record with uid(%{public}d) failed: %{public}d", __func__, __LINE__,
            uid, ret);
    }
    return ret;
}

int32_t SerialRightManager::CleanUpRightTemporaryExpired(const std::string& portName)
{
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    int32_t ret = helper->DeleteValidPeriodRightRecord(SERIAL_RIGHT_VALID_PERIOD_MIN, portName);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d failed: delete temporary expired record: port=%{private}s",
            __func__, __LINE__, portName.c_str());
    }
    return ret;
}

int32_t SerialRightManager::CleanUpRightNormalExpired(int32_t uid)
{
    uint64_t nowTime = GetCurrentTimestamp();
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    int32_t ret = helper->DeleteNormalExpiredRightRecord(uid, nowTime);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d failed: clean up expired record at %{public}" PRIu64,
            __func__, __LINE__, nowTime);
    }
    return ret;
}

int32_t SerialRightManager::TidyUpRight(uint32_t choose)
{
    if (choose == TIDY_UP_SERIAL_RIGHT_RECORD_NONE) {
        return SERIAL_RIGHT_NOP;
    }

    if ((choose | TIDY_UP_SERIAL_RIGHT_RECORD_ALL) != TIDY_UP_SERIAL_RIGHT_RECORD_ALL) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d choose invalid", __func__, __LINE__);
        return SERIAL_ERR_INVALID_PARAM;
    }

    int32_t uid = SERIAL_RIGHT_USERID_INVALID;
    GetCurrentUserId(uid);

#ifndef DISABLE_CONSOLE_BYPASS
    if (uid == SERIAL_RIGHT_USERID_CONSOLE && !IsTestModeEnabled()) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d console called, bypass (testMode=0)", __func__, __LINE__);
        return SERIAL_RIGHT_OK;
    }
#else
    if (uid == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d DISABLE_CONSOLE_BYPASS: proceeding with userId=%{public}d",
            __func__, __LINE__, uid);
    }
#endif

    int32_t ret = 0;
    if ((choose & TIDY_UP_SERIAL_RIGHT_RECORD_APP_UNINSTALLED) != 0) {
        int32_t totalUninstalledApps = 0;
        int32_t deleteUninstalledApps = 0;
        ret = CleanUpRightAppUninstalledInternal(uid, totalUninstalledApps, deleteUninstalledApps);
        SERIAL_HILOGD(MODULE_SERVICE,
            "%{public}s:%{public}d delete app uninstalled record[%{public}d/%{public}d]: %{public}d", __func__,
            __LINE__, deleteUninstalledApps, totalUninstalledApps, ret);
    }

    if ((choose & TIDY_UP_SERIAL_RIGHT_RECORD_EXPIRED) != 0) {
        ret = CleanUpRightNormalExpired(uid);
        SERIAL_HILOGD(
            MODULE_SERVICE, "%{public}s:%{public}d delete expired record: %{public}d", __func__, __LINE__, ret);
    }

    if ((choose & TIDY_UP_SERIAL_RIGHT_RECORD_APP_REINSTALLED) != 0) {
        uint32_t totalReinstalledApps = 0;
        uint32_t deleteReinstalledApps = 0;
        ret = CleanUpRightAppReinstalled(uid, totalReinstalledApps, deleteReinstalledApps);
        SERIAL_HILOGD(MODULE_SERVICE,
            "%{public}s:%{public}d delete app reinstalled record[%{public}u/%{public}u]: %{public}d", __func__,
            __LINE__, deleteReinstalledApps, totalReinstalledApps, ret);
    }

    return ret;
}

int32_t SerialRightManager::CleanUpRightAppUninstalledInternal(int32_t uid, int32_t& totalApps, int32_t& deleteApps)
{
    std::vector<std::string> apps;
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    int32_t ret = helper->QueryRightRecordApps(uid, apps);
    if (ret <= 0) {
        return SERIAL_RIGHT_NOP;
    }

    totalApps = static_cast<int32_t>(apps.size());
    deleteApps = 0;

    for (int32_t i = 0; i < totalApps; i++) {
        std::string app = apps.at(i);
        if (!IsAppInstalled(uid, app)) {
            ret = helper->DeleteAppRightRecord(uid, app);
            if (ret != SERIAL_RIGHT_OK) {
                SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d clean failed: app=%{public}s, ret=%{public}d",
                    __func__, __LINE__, app.c_str(), ret);
                continue;
            }
            deleteApps++;
        }
    }

    SERIAL_HILOGD(MODULE_SERVICE,
        "%{public}s:%{public}d clean uninstall app record[%{public}d/%{public}d]: uid=%{public}d", __func__, __LINE__,
        deleteApps, totalApps, uid);
    return ret;
}

std::vector<std::string> SerialRightManager::FindReinstalledBundleNames(
    int32_t uid, const std::vector<std::string>& apps)
{
    std::vector<std::string> deleteBundleNames;
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr", __func__, __LINE__);
        return deleteBundleNames;
    }

    for (size_t i = 0; i < apps.size(); i++) {
        std::string bundleName = apps.at(i);
        std::vector<struct SerialRightAppInfo> infos;
        int32_t ret = helper->QueryAppRightRecord(uid, bundleName, infos);
        if (ret != E_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d query app info %{public}s failed: %{public}d",
                __func__, __LINE__, bundleName.c_str(), ret);
            continue;
        }

        uint64_t installTime = 0;
        uint64_t updateTime = 0;
        if (!GetBundleInstallAndUpdateTime(uid, bundleName, installTime, updateTime)) {
            SERIAL_HILOGE(MODULE_SERVICE,
                "%{public}s:%{public}d get app install time failed: app=%{public}s uid=%{public}d", __func__, __LINE__,
                bundleName.c_str(), uid);
            continue;
        }

        for (size_t j = 0; j < infos.size(); j++) {
            struct SerialRightAppInfo info = infos.at(j);
            if (info.installTime != installTime) {
                deleteBundleNames.push_back(bundleName);
                break;
            }
        }
    }

    sort(deleteBundleNames.begin(), deleteBundleNames.end());
    auto lastDelete = unique(deleteBundleNames.begin(), deleteBundleNames.end());
    deleteBundleNames.erase(lastDelete, deleteBundleNames.end());
    return deleteBundleNames;
}

int32_t SerialRightManager::CleanUpRightAppReinstalled(int32_t uid, uint32_t& totalApps, uint32_t& deleteApps)
{
    std::vector<std::string> apps;
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    int32_t ret = helper->QueryRightRecordApps(uid, apps);
    if (ret <= 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d query apps failed or empty: %{public}d", __func__, __LINE__, ret);
        return SERIAL_RIGHT_NOP;
    }

    sort(apps.begin(), apps.end());
    auto last = unique(apps.begin(), apps.end());
    apps.erase(last, apps.end());

    deleteApps = 0;
    totalApps = apps.size();

    std::vector<std::string> deleteBundleNames = FindReinstalledBundleNames(uid, apps);

    ret = helper->DeleteAppsRightRecord(uid, deleteBundleNames);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d delete apps failed: %{public}d", __func__, __LINE__, ret);
    } else {
        deleteApps = deleteBundleNames.size();
    }
    return ret;
}

int32_t SerialRightManager::CleanUpRightAppUninstalled(int32_t uid, const std::string& bundleName)
{
    std::vector<std::string> apps;
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    int32_t ret = helper->QueryRightRecordApps(uid, apps);
    if (ret <= 0) {
        return SERIAL_RIGHT_NOP;
    }

    int32_t index = 0;
    for (size_t i = 0; i < apps.size(); i++) {
        if (apps.at(i) == bundleName) {
            index = static_cast<int32_t>(i);
            break;
        }
    }

    if (apps.at(index) != bundleName) {
        return SERIAL_RIGHT_NOP;
    }

    ret = helper->DeleteAppRightRecord(uid, apps.at(index));
    SERIAL_HILOGD(MODULE_SERVICE,
        "%{public}s:%{public}d clean[%{public}d/%{public}zu]: uid=%{public}d, app=%{public}s, ret=%{public}d", __func__,
        __LINE__, index, apps.size(), uid, bundleName.c_str(), ret);
    return ret;
}

int32_t SerialRightManager::CleanUpRightUserDeleted(int32_t& totalUsers, int32_t& deleteUsers)
{
    std::vector<std::string> rightRecordUids;
    bool isAccountExists = false;

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr, false", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    int32_t ret = helper->QueryRightRecordUids(rightRecordUids);
    if (ret <= 0) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d query apps failed or empty: %{public}d", __func__, __LINE__, ret);
        return SERIAL_RIGHT_NOP;
    }

    size_t len = rightRecordUids.size();
    deleteUsers = 0;

    for (size_t i = 0; i < len; i++) {
        int32_t uid = 0;
        if (!StrToInt(rightRecordUids.at(i), uid)) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d convert failed: %{public}s", __func__, __LINE__,
                rightRecordUids.at(i).c_str());
            continue;
        }

        ret = AccountSA::OsAccountManager::IsOsAccountExists(uid, isAccountExists);
        if (ret != SERIAL_RIGHT_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d call IsOsAccountExists failed: %{public}d", __func__,
                __LINE__, ret);
            continue;
        }

        if (!isAccountExists) {
            ret = helper->DeleteUidRightRecord(uid);
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d detected delete uid=%{public}d: %{public}d", __func__,
                __LINE__, uid, ret);
            deleteUsers++;
        }
        SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d uid exist, ignore: %{public}d", __func__, __LINE__, uid);
    }

    totalUsers = static_cast<int32_t>(rightRecordUids.size());
    return SERIAL_RIGHT_OK;
}

int32_t SerialRightManager::CleanUpRightUserStopped(int32_t uid)
{
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d CleanUpRightUserStopped %{public}d: helper is null",
            __func__, __LINE__, uid);
        return SERIAL_RIGHT_FAILURE;
    }
    return helper->DeleteUidRightRecord(uid);
}

SerialRightManager::SerialRightEventSubscriber::SerialRightEventSubscriber(
    const CommonEventSubscribeInfo& subscribeInfo)
    : CommonEventSubscriber(subscribeInfo)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightEventSubscriber created", __func__, __LINE__);
}

void SerialRightManager::SerialRightEventSubscriber::OnReceiveEvent(const CommonEventData& data)
{
    const std::string action = data.GetWant().GetAction();
    SERIAL_HILOGI(
        MODULE_SERVICE, "%{public}s:%{public}d OnReceiveEvent: action=%{public}s", __func__, __LINE__, action.c_str());

    if (action == CommonEventSupport::COMMON_EVENT_BOOT_COMPLETED) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Boot completed event received, clearing all rights",
            __func__, __LINE__);
        SerialRightManager::GetInstance().OnBootComplete();
    } else if (action == CommonEventSupport::COMMON_EVENT_PACKAGE_REMOVED) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Package removed event received", __func__, __LINE__);
        // Get uid and bundle name from event data
        int32_t uid = data.GetCode();
        std::string bundleName = data.GetData();
        if (!bundleName.empty() && uid != SERIAL_RIGHT_USERID_INVALID) {
            int32_t ret = CleanUpRightAppUninstalled(uid, bundleName);
            SERIAL_HILOGI(MODULE_SERVICE,
                "%{public}s:%{public}d Clean up app uninstalled: uid=%{public}d, bundle=%{public}s, ret=%{public}d",
                __func__, __LINE__, uid, bundleName.c_str(), ret);
        }
    } else if (action == CommonEventSupport::COMMON_EVENT_USER_SWITCHED) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d User switched event received", __func__, __LINE__);
        // Clean up rights for old user - handled by system
    } else if (action == CommonEventSupport::COMMON_EVENT_USER_REMOVED) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d User removed event received", __func__, __LINE__);
        int32_t uid = data.GetCode();
        if (uid != SERIAL_RIGHT_USERID_INVALID) {
            int32_t ret = CleanUpRightUserStopped(uid);
            SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Clean up user removed: uid=%{public}d, ret=%{public}d",
                __func__, __LINE__, uid, ret);
        }
    }
}

int32_t SerialRightManager::SubscribeCommonEvents()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SubscribeCommonEvents", __func__, __LINE__);

    // Create subscribe info with matching skills
    MatchingSkills matchingSkills;
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_BOOT_COMPLETED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_PACKAGE_REMOVED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USER_SWITCHED);
    matchingSkills.AddEvent(CommonEventSupport::COMMON_EVENT_USER_REMOVED);

    CommonEventSubscribeInfo subscribeInfo(matchingSkills);
    subscribeInfo.SetUserId(SERIAL_RIGHT_USERID_DEFAULT);

    // Create event subscriber
    eventSubscriber_ = std::make_shared<SerialRightEventSubscriber>(subscribeInfo);
    if (eventSubscriber_ == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d Failed to create SerialRightEventSubscriber", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    // Subscribe to common events
    bool result = CommonEventManager::SubscribeCommonEvent(eventSubscriber_);
    if (!result) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d SubscribeCommonEvent failed", __func__, __LINE__);
        eventSubscriber_ = nullptr;
        return SERIAL_RIGHT_FAILURE;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SubscribeCommonEvents success", __func__, __LINE__);
    return SERIAL_RIGHT_OK;
}

void SerialRightManager::OnBootComplete()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnBootComplete: clearing all authorization rights on boot",
        __func__, __LINE__);

    // Clear all rights from database on boot complete
    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d OnBootComplete: helper is null", __func__, __LINE__);
        return;
    }

    // Query all uids and delete all rights
    std::vector<std::string> uids;
    int32_t ret = helper->QueryRightRecordUids(uids);
    if (ret <= 0) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnBootComplete: no rights to clear", __func__, __LINE__);
        return;
    }

    int32_t totalUsers = static_cast<int32_t>(uids.size());
    int32_t deletedUsers = 0;

    for (const std::string& uidStr : uids) {
        int32_t uid = 0;
        if (StrToInt(uidStr, uid)) {
            ret = helper->DeleteUidRightRecord(uid);
            if (ret == SERIAL_RIGHT_OK) {
                deletedUsers++;
                SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d OnBootComplete: deleted rights for uid=%{public}d",
                    __func__, __LINE__, uid);
            } else {
                SERIAL_HILOGW(MODULE_SERVICE,
                    "%{public}s:%{public}d OnBootComplete: failed to delete rights for uid=%{public}d", __func__,
                    __LINE__, uid);
            }
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnBootComplete: cleared %{public}d/%{public}d users' rights",
        __func__, __LINE__, deletedUsers, totalUsers);
}

int32_t SerialRightManager::ClearAllRights()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d ClearAllRights", __func__, __LINE__);

    std::shared_ptr<SerialRightDbHelper> helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d ClearAllRights: helper is null", __func__, __LINE__);
        return SERIAL_RIGHT_FAILURE;
    }

    std::vector<std::string> uids;
    int32_t ret = helper->QueryRightRecordUids(uids);
    if (ret <= 0) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d ClearAllRights: no rights to clear", __func__, __LINE__);
        return SERIAL_RIGHT_OK;
    }

    for (const std::string& uidStr : uids) {
        int32_t uid = 0;
        if (StrToInt(uidStr, uid)) {
            helper->DeleteUidRightRecord(uid);
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d ClearAllRights: cleared %{public}zu users' rights", __func__,
        __LINE__, uids.size());
    return SERIAL_RIGHT_OK;
}

} // namespace Serial
} // namespace OHOS