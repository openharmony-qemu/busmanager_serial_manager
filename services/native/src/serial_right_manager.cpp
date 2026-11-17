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
#include <iterator>
#include <unistd.h>

#include "ability_manager_client.h"
#include "accesstoken_kit.h"
#include "bundle_mgr_interface.h"
#include "bundle_resource_interface.h"
#include "hilog_wrapper.h"
#include "ipc_skeleton.h"
#include "iservice_registry.h"
#include "serial_authorization_request.h"
#include "serial_device_identity.h"
#include "serial_error_code.h"
#include "serial_right_db_helper.h"
#include "serial_token_id.h"
#include "string_ex.h"
#include "system_ability_definition.h"
#include "tokenid_kit.h"

using namespace OHOS::AppExecFwk;
using namespace OHOS::Security::AccessToken;

namespace OHOS {
namespace Serial {

constexpr int32_t MESSAGE_PARCEL_KEY_SIZE = 3;
constexpr uint32_t CMD_CODE_SHOW_DIALOG = 1;
constexpr uint32_t CMD_CODE_CLOSE_DIALOG = 3;
constexpr int32_t SERIAL_DIALOG_IPC_TIMEOUT_SECONDS = 5;
constexpr int32_t SERIAL_ATTACHMENT_POLL_MILLISECONDS = 100;

std::mutex SerialRightManager::dialogParamsMutex_;
std::map<std::string, std::string> SerialRightManager::dialogParams_ = {};
int32_t SerialRightManager::pendingUserId_ = SERIAL_RIGHT_USERID_INVALID;
std::string SerialRightManager::pendingDeviceId_;
sptr<SerialRightManager::SerialAbilityConn> SerialRightManager::pendingConnection_;

SerialRightManager& SerialRightManager::GetInstance() {
    static SerialRightManager instance;
    return instance;
}

SerialRightManager::SerialRightManager() {
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager constructed", __func__, __LINE__);
}

SerialRightManager::~SerialRightManager() {
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager destroyed", __func__, __LINE__);
}

int32_t SerialRightManager::Init() {
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager Init (OnStart)", __func__, __LINE__);

    // Load cache: from database if exists, otherwise empty cache
    LoadCacheFromDb();

    return SERIAL_RIGHT_OK;
}

int32_t SerialRightManager::OnStop() {
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager OnStop", __func__, __LINE__);

    // Create database and save if cache has data
    SaveCacheToDb();

    return SERIAL_RIGHT_OK;
}

std::string SerialRightManager::BuildCacheKey(
    const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, int32_t userId) {
    // Key format: userId_deviceId_bundleName_tokenId
    return std::to_string(userId) + "_" + deviceId + "_" + bundleName + "_" + tokenId;
}

bool SerialRightManager::LoadCacheFromDb() {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    rightCache_.clear();

    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr", __func__, __LINE__);
        cacheLoaded_ = true;
        return false;
    }

    // Try to open database (returns false if not exists)
    if (!helper->OpenDatabaseIfExists()) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database not exists, use empty cache", __func__, __LINE__);
        cacheLoaded_ = true;
        return true; // Database not existing is normal, return true
    }

    // Load all records into cache
    std::vector<SerialRightFullRecord> records;
    int32_t ret = helper->QueryAllRightRecords(records);
    if (ret <= 0) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d No records in database", __func__, __LINE__);
        cacheLoaded_ = true;
        return true;
    }

    for (const auto& record : records) {
        std::string key = BuildCacheKey(record.deviceId, record.bundleName, record.tokenId, record.uid);
        rightCache_[key] = record;
    }

    cacheLoaded_ = true;
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Cache loaded: %{public}zu records", __func__, __LINE__,
        rightCache_.size());
    return true;
}

bool SerialRightManager::SaveCacheToDb() {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    if (rightCache_.empty()) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Cache empty, skip save", __func__, __LINE__);
        return true;
    }

    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr", __func__, __LINE__);
        return false;
    }

    // Create database if not exists
    if (!helper->CreateDatabaseAndOpen()) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d CreateDatabase failed", __func__, __LINE__);
        return false;
    }

    // Convert cache to record list
    std::vector<SerialRightFullRecord> records;
    for (const auto& pair : rightCache_) {
        records.push_back(pair.second);
    }

    // Batch insert
    int32_t ret = helper->InsertAllRecords(records);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d InsertAllRecords failed", __func__, __LINE__);
        return false;
    }

    SERIAL_HILOGI(
        MODULE_SERVICE, "%{public}s:%{public}d Cache saved: %{public}zu records", __func__, __LINE__, records.size());

    // Clear cache
    rightCache_.clear();

    return true;
}

bool SerialRightManager::HasRight(
    const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId) {
    // Use default bundle name "serial_cli" for CLI tools when bundleName is empty
    std::string actualBundleName = bundleName.empty() ? "serial_cli" : bundleName;

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d HasRight", __func__, __LINE__);

    if (userId == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d console user, proceeding with auth check", __func__, __LINE__);
    }

    // Query cache (does not access database)
    std::lock_guard<std::mutex> lock(cacheMutex_);
    std::string key = BuildCacheKey(deviceId, actualBundleName, tokenId, userId);
    bool hasRecord = rightCache_.find(key) != rightCache_.end();

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d HasRight result: %{public}s (cache size=%{public}zu)",
        __func__, __LINE__, hasRecord ? "true" : "false", rightCache_.size());
    return hasRecord;
}

int32_t SerialRightManager::RequestRight(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId) {
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d RequestRight", __func__, __LINE__);

    if (!SerialAttachmentMatches(deviceInfo.portName, deviceInfo.deviceId)) {
        RevokeDeviceRights(deviceInfo.deviceId);
        return SERIAL_ERR_DEVICE_DISCONNECTED;
    }
    if (HasRight(deviceInfo.deviceId, deviceInfo.bundleName, deviceInfo.tokenId, userId)) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Device already has right", __func__, __LINE__);
        return SERIAL_OK;
    }

    if (!GetUserAgreementByDiag(deviceInfo, userId)) {
        if (!SerialAttachmentMatches(deviceInfo.portName, deviceInfo.deviceId)) {
            return SERIAL_ERR_DEVICE_DISCONNECTED;
        }
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d User denied permission", __func__, __LINE__);
        return SERIAL_ERR_PERMISSION_DENIED;
    }

    return SERIAL_OK;
}

bool SerialRightManager::GetUserAgreementByDiag(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId) {
    // There can only be one dialog at a time
    std::lock_guard<std::mutex> guard(dialogRunning_);

    if (!ShowSerialDialog(deviceInfo, userId)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d ShowSerialDialog failed", __func__, __LINE__);
        return false;
    }

    // Check HasRight after dialog closes - dialog HAP should have called AddPortAuthorization
    if (HasRight(deviceInfo.deviceId, deviceInfo.bundleName, deviceInfo.tokenId, userId)) {
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d HasRight check passed, user granted permission", __func__, __LINE__);
        return true;
    }

    // No permission after dialog closed means user denied or dialog HAP failed
    SERIAL_HILOGW(MODULE_SERVICE,
        "%{public}s:%{public}d HasRight check failed after dialog, user denied or dialog HAP did not call "
        "AddPortAuthorization",
        __func__, __LINE__);
    return false;
}

bool SerialRightManager::ShowSerialDialog(const SerialDeviceInfoForDialog& deviceInfo, const int32_t userId) {
    uint32_t tokenId = 0;
    std::string requestId;
    if (!ParseSerialTokenId(deviceInfo.tokenId, tokenId) ||
        !BuildSerialAuthorizationRequestId(deviceInfo.deviceId, requestId)) {
        return false;
    }

    std::string appName;
    if (!GetAppName(deviceInfo.bundleName, appName, userId)) {
        appName = deviceInfo.bundleName;
    }
    {
        std::lock_guard<std::mutex> lock(dialogParamsMutex_);
        dialogParams_.clear();
        pendingUserId_ = userId;
        pendingDeviceId_ = deviceInfo.deviceId;
        dialogParams_["appName"] = appName;
        dialogParams_["portName"] = deviceInfo.portName;
        dialogParams_["tokenId"] = std::to_string(tokenId);
        dialogParams_["deviceId"] = requestId;
        dialogParams_["ability.want.params.uiExtensionType"] = "sysDialog/common";
    }

    int32_t ret = ConnectAbility(userId);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d ConnectAbility failed %{public}d", __func__, __LINE__, ret);
        ClearDialogRequest();
        return false;
    }

    bool completed = abilityConn_->WaitForDisconnect();
    if (!completed) {
        RevokeDeviceRights(deviceInfo.deviceId);
    }
    ClearDialogRequest();
    UnShowSerialDialog();
    abilityConn_ = nullptr;
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d ShowSerialDialog completed", __func__, __LINE__);
    return completed;
}

void SerialRightManager::ClearDialogRequest() {
    std::lock_guard<std::mutex> lock(dialogParamsMutex_);
    pendingUserId_ = SERIAL_RIGHT_USERID_INVALID;
    pendingDeviceId_.clear();
    pendingConnection_ = nullptr;
    dialogParams_.clear();
}

bool SerialRightManager::AuthorizePendingRequest(
    const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, int32_t userId) {
    std::lock_guard<std::mutex> lock(dialogParamsMutex_);
    if (pendingUserId_ != userId || pendingDeviceId_.empty() || pendingConnection_ == nullptr ||
        dialogParams_["deviceId"] != deviceId || dialogParams_["tokenId"] != tokenId ||
        !SerialAttachmentMatches(dialogParams_["portName"], pendingDeviceId_)) {
        return false;
    }
    return pendingConnection_->AcceptAuthorization(deviceId, bundleName, tokenId, userId);
}

bool SerialRightManager::RevokeDeviceRights(const std::string& deviceId) {
    if (deviceId.empty()) {
        return false;
    }
    sptr<SerialAbilityConn> connection;
    {
        std::lock_guard<std::mutex> lock(dialogParamsMutex_);
        if (pendingDeviceId_ == deviceId) {
            connection = pendingConnection_;
            pendingConnection_ = nullptr;
            pendingUserId_ = SERIAL_RIGHT_USERID_INVALID;
            pendingDeviceId_.clear();
            dialogParams_.clear();
        }
    }
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        for (auto it = rightCache_.begin(); it != rightCache_.end();) {
            it = it->second.deviceId == deviceId ? rightCache_.erase(it) : std::next(it);
        }
        auto helper = SerialRightDbHelper::GetInstance();
        removed = helper != nullptr && helper->DeleteDeviceRightRecords(deviceId) == E_OK;
    }
    if (!removed) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s: attachment database revocation failed", __func__);
    }
    if (connection != nullptr) {
        connection->CancelDialog();
    }
    return removed;
}

int32_t SerialRightManager::ConnectAbility(const int32_t userId) {
    {
        std::lock_guard<std::mutex> lock(dialogParamsMutex_);
        if (pendingDeviceId_.empty()) {
            return SERIAL_RIGHT_FAILURE;
        }
        abilityConn_ =
            sptr<SerialAbilityConn>(new (std::nothrow) SerialAbilityConn(dialogParams_, pendingDeviceId_, userId));
        pendingConnection_ = abilityConn_;
    }
    if (abilityConn_ == nullptr) {
        return SERIAL_RIGHT_FAILURE;
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

bool SerialRightManager::ValidateTokenAndGetHapInfo(const std::string& tokenIdStr,
    Security::AccessToken::AccessTokenID& tokenId, Security::AccessToken::HapTokenInfo& hapTokenInfoRes, int32_t& uid) {
    if (!ParseSerialTokenId(tokenIdStr, tokenId)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d tokenIdStr invalid", __func__, __LINE__);
        return false;
    }

    int32_t ret = AccessTokenKit::GetHapTokenInfo(tokenId, hapTokenInfoRes);
    if (ret != RET_SUCCESS) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d GetHapTokenInfo failed:ret:%{public}d", __func__, __LINE__, ret);
        return false;
    }
    uid = hapTokenInfoRes.userID;
    return true;
}

bool SerialRightManager::AddDeviceRight(const std::string& deviceId, const std::string& tokenIdStr) {
    AccessTokenID tokenId;
    HapTokenInfo hapTokenInfoRes;
    int32_t uid;
    if (!ValidateTokenAndGetHapInfo(tokenIdStr, tokenId, hapTokenInfoRes, uid)) {
        return false;
    }

    if (uid == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d console user, proceeding", __func__, __LINE__);
    }

    return AddDeviceRight(deviceId, hapTokenInfoRes.bundleName, tokenIdStr, uid);
}

bool SerialRightManager::AddDeviceRight(
    const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId) {
    // Use default bundle name "serial_cli" for CLI tools when bundleName is empty
    std::string actualBundleName = bundleName.empty() ? "serial_cli" : bundleName;

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d AddDeviceRight", __func__, __LINE__);

    if (userId == SERIAL_RIGHT_USERID_CONSOLE) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d console user, proceeding with auth", __func__, __LINE__);
    }

    // Write to cache (does not access database)
    std::lock_guard<std::mutex> lock(cacheMutex_);

    SerialRightFullRecord record = {};
    record.uid = userId;
    record.deviceId = deviceId;
    record.bundleName = actualBundleName;
    record.tokenId = tokenId;
    record.installTime = GetCurrentTimestamp();
    record.updateTime = GetCurrentTimestamp();
    record.requestTime = GetCurrentTimestamp();
    record.validPeriod = SERIAL_RIGHT_VALID_PERIOD_MAX; // Permanently valid

    std::string key = BuildCacheKey(deviceId, actualBundleName, tokenId, userId);
    rightCache_[key] = record;

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Added to cache: cache size=%{public}zu", __func__, __LINE__,
        rightCache_.size());

    return true;
}

bool SerialRightManager::IsSystemAppOrSa() {
    AccessTokenID callerTokenId = IPCSkeleton::GetCallingTokenID();
    auto tokenType = AccessTokenKit::GetTokenTypeFlag(callerTokenId);
    if (tokenType == TOKEN_HAP) {
        uint64_t fullTokenId = IPCSkeleton::GetCallingFullTokenID();
        bool isSystemApp = TokenIdKit::IsSystemAppByFullTokenID(fullTokenId);
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d IsSystemAppOrSa: isSystemApp=%{public}d", __func__,
            __LINE__, static_cast<int32_t>(isSystemApp));
        return isSystemApp;
    }
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d IsSystemAppOrSa: tokenType=%{public}d, default false",
        __func__, __LINE__, static_cast<int32_t>(tokenType));
    return false;
}

bool SerialRightManager::UnShowSerialDialog() {
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

bool SerialRightManager::SerialAbilityConn::BuildDialogRequest(MessageParcel& data) {
    if (dialogRequestParams_.empty() || !data.WriteInt32(MESSAGE_PARCEL_KEY_SIZE) ||
        !data.WriteString16(u"bundleName") || !data.WriteString16(u"com.usb.right") ||
        !data.WriteString16(u"abilityName") || !data.WriteString16(u"UsbServiceExtAbility") ||
        !data.WriteString16(u"parameters")) {
        return false;
    }

    cJSON* paramJson = cJSON_CreateObject();
    if (paramJson == nullptr) {
        return false;
    }
    for (const auto& parameter : dialogRequestParams_) {
        if (cJSON_AddStringToObject(paramJson, parameter.first.c_str(), parameter.second.c_str()) == nullptr) {
            cJSON_Delete(paramJson);
            return false;
        }
    }

    char* pParamJson = cJSON_PrintUnformatted(paramJson);
    cJSON_Delete(paramJson);
    paramJson = nullptr;
    if (!pParamJson) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Print paramJson error", __func__, __LINE__);
        return false;
    }

    std::string paramStr(pParamJson);
    bool written = data.WriteString16(Str8ToStr16(paramStr));
    cJSON_free(pParamJson);
    pParamJson = nullptr;

    return written;
}

void SerialRightManager::SerialAbilityConn::OnAbilityConnectDone(
    const AppExecFwk::ElementName& element, const sptr<IRemoteObject>& remoteObject, int32_t resultCode) {
    if (remoteObject == nullptr || resultCode != ERR_OK) {
        FinishDialog(SERIAL_RIGHT_FAILURE);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(remoteMutex_);
        if (dialogFinished_) {
            return;
        }
    }
    MessageParcel data;
    MessageParcel reply;
    MessageOption option(MessageOption::TF_SYNC, SERIAL_DIALOG_IPC_TIMEOUT_SECONDS);
    int32_t replyCode = SERIAL_RIGHT_FAILURE;
    if (!BuildDialogRequest(data) || remoteObject->SendRequest(CMD_CODE_SHOW_DIALOG, data, reply, option) != ERR_OK ||
        !reply.ReadInt32(replyCode) || replyCode != ERR_OK) {
        FinishDialog(SERIAL_RIGHT_FAILURE);
        return;
    }
    bool expired = false;
    {
        std::lock_guard<std::mutex> guard(remoteMutex_);
        remoteObject_ = remoteObject;
        expired = dialogFinished_;
    }
    if (expired) {
        CloseDialog();
    }
}

void SerialRightManager::SerialAbilityConn::FinishDialog(int32_t resultCode) {
    std::lock_guard<std::mutex> guard(remoteMutex_);
    if (!dialogFinished_) {
        lastResultCode_ = resultCode;
        dialogFinished_ = true;
        dialogFinishedCondition_.notify_all();
    }
}

bool SerialRightManager::SerialAbilityConn::WaitForDisconnect(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(remoteMutex_);
    deadline_ = std::min(deadline_, std::chrono::steady_clock::now() + timeout);
    auto port = dialogRequestParams_.find("portName");
    while (!dialogFinished_) {
        if ((!deviceId_.empty() && port != dialogRequestParams_.end() &&
                !SerialAttachmentMatches(port->second, deviceId_)) ||
            std::chrono::steady_clock::now() >= deadline_) {
            dialogFinished_ = true;
            lastResultCode_ = SERIAL_RIGHT_FAILURE;
            break;
        }
        auto nextPoll =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(SERIAL_ATTACHMENT_POLL_MILLISECONDS);
        dialogFinishedCondition_.wait_until(lock, std::min(deadline_, nextPoll));
    }
    return lastResultCode_ == ERR_OK;
}

bool SerialRightManager::SerialAbilityConn::AcceptAuthorization(
    const std::string& requestId, const std::string& bundleName, const std::string& tokenId, int32_t userId) {
    // The caller holds dialogParamsMutex_. Serialize the actual grant with
    // timeout/disconnect/cancellation on this captured connection, not the
    // lifetime of the shared system-dialog parent service.
    std::lock_guard<std::mutex> guard(remoteMutex_);
    if (dialogFinished_) {
        return false;
    }
    auto request = dialogRequestParams_.find("deviceId");
    auto token = dialogRequestParams_.find("tokenId");
    auto port = dialogRequestParams_.find("portName");
    if (request == dialogRequestParams_.end() || request->second != requestId || token == dialogRequestParams_.end() ||
        token->second != tokenId || userId_ != userId || deviceId_.empty() || port == dialogRequestParams_.end()) {
        return false;
    }
    if (std::chrono::steady_clock::now() >= deadline_ || !SerialAttachmentMatches(port->second, deviceId_)) {
        dialogFinished_ = true;
        lastResultCode_ = SERIAL_RIGHT_FAILURE;
        dialogFinishedCondition_.notify_all();
        return false;
    }
    if (!SerialRightManager::GetInstance().AddDeviceRight(deviceId_, bundleName, tokenId, userId)) {
        return false;
    }
    lastResultCode_ = SERIAL_RIGHT_OK;
    dialogFinished_ = true;
    dialogFinishedCondition_.notify_all();
    return true;
}

void SerialRightManager::SerialAbilityConn::CancelDialog() {
    FinishDialog(SERIAL_RIGHT_FAILURE);
    CloseDialog();
}

void SerialRightManager::SerialAbilityConn::OnAbilityDisconnectDone(
    const AppExecFwk::ElementName& element, int32_t resultCode) {
    SERIAL_HILOGI(
        MODULE_SERVICE, "%{public}s:%{public}d disconnect done, resultCode=%{public}d", __func__, __LINE__, resultCode);
    FinishDialog(resultCode);
    std::lock_guard<std::mutex> guard(remoteMutex_);
    remoteObject_ = nullptr;
}

void SerialRightManager::SerialAbilityConn::CloseDialog() {
    sptr<IRemoteObject> remote;
    {
        std::lock_guard<std::mutex> guard(remoteMutex_);
        remote = remoteObject_;
    }
    if (remote == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d CloseDialog: disconnected", __func__, __LINE__);
        return;
    }

    // The system-dialog remote is shared. A late close must not close a newer
    // request; hold this lock through the bounded IPC to serialize a new show.
    std::lock_guard<std::mutex> requestGuard(dialogParamsMutex_);
    auto pending = dialogParams_.find("deviceId");
    auto expected = dialogRequestParams_.find("deviceId");
    if (pending != dialogParams_.end() &&
        (expected == dialogRequestParams_.end() || pending->second != expected->second)) {
        return;
    }
    MessageParcel data;
    MessageParcel reply;
    MessageOption option(MessageOption::TF_SYNC, SERIAL_DIALOG_IPC_TIMEOUT_SECONDS);
    const uint32_t cmdCode = CMD_CODE_CLOSE_DIALOG;

    int32_t ret = remote->SendRequest(cmdCode, data, reply, option);
    int32_t replyCode = SERIAL_RIGHT_FAILURE;
    bool success = false;
    if (ret == ERR_OK) {
        success = reply.ReadInt32(replyCode);
    }
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d CloseDialog: ret=%{public}d, %{public}d, %{public}d", __func__,
        __LINE__, ret, success, replyCode);
}

uint64_t SerialRightManager::GetCurrentTimestamp() {
    int64_t time =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<uint64_t>(time);
}

bool SerialRightManager::IsAllDigits(const std::string& str) {
    size_t len = str.length();
    for (size_t i = 0; i < len; i++) {
        if (!isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

sptr<IBundleMgr> SerialRightManager::GetBundleMgr() {
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

sptr<IBundleResource> SerialRightManager::GetBundleResMgr() {
    auto bundleMgr = GetBundleMgr();
    if (bundleMgr == nullptr) {
        return nullptr;
    }
    return bundleMgr->GetBundleResourceProxy();
}

bool SerialRightManager::GetAppName(const std::string& bundleName, std::string& appName, int32_t userId) {
    AccessTokenID callerTokenId = IPCSkeleton::GetCallingTokenID();
    HapTokenInfo callerTokenInfo;
    int32_t ret = AccessTokenKit::GetHapTokenInfo(callerTokenId, callerTokenInfo);
    if (ret != 0) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetAppName: GetHapTokenInfo failed: %{public}d", __func__,
            __LINE__, ret);
        appName = bundleName;
        return false;
    }

    appName = callerTokenInfo.bundleName;
    if (appName.empty()) {
        appName = bundleName;
    }

    auto resMgr = GetBundleResMgr();
    if (resMgr != nullptr) {
        BundleResourceInfo info;
        int32_t resRet = resMgr->GetBundleResourceInfo(
            bundleName, (uint32_t)ResourceFlag::GET_RESOURCE_INFO_WITH_LABEL, info, userId);
        if (resRet == ERR_OK && !info.label.empty()) {
            appName = info.label;
            SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetAppName from resMgr", __func__, __LINE__);
            return true;
        }
        resRet = resMgr->GetBundleResourceInfo(bundleName, (uint32_t)ResourceFlag::GET_RESOURCE_INFO_WITH_LABEL, info);
        if (resRet == ERR_OK && !info.label.empty()) {
            appName = info.label;
            SERIAL_HILOGI(
                MODULE_SERVICE, "%{public}s:%{public}d GetAppName from resMgr (no userId)", __func__, __LINE__);
            return true;
        }
        if (resRet != ERR_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetAppName: get res info failed: %{public}d", __func__,
                __LINE__, resRet);
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetAppName use bundleName", __func__, __LINE__);
    return true;
}

} // namespace Serial
} // namespace OHOS
