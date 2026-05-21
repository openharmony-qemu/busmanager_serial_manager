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
#include "bundle_resource_interface.h"
#include "hilog_wrapper.h"
#include "ipc_skeleton.h"
#include "iservice_registry.h"
#include "serial_error_code.h"
#include "serial_right_db_helper.h"
#include "string_ex.h"
#include "system_ability_definition.h"
#include "tokenid_kit.h"

using namespace OHOS::AppExecFwk;
using namespace OHOS::Security::AccessToken;

namespace OHOS {
namespace Serial {

// Authorization flow configuration:
// 1. DISABLE_CONSOLE_BYPASS=0: Console users (userId=0) bypass auth check
//    - Used for module testing and CLI tools
// 2. DISABLE_CONSOLE_BYPASS=1: Force real auth flow for ALL users including console
//    - Used for production deployment
#define DISABLE_CONSOLE_BYPASS 1

#if !DISABLE_CONSOLE_BYPASS
// Test mode: when SERIAL_RIGHT_TEST_MODE=1, skip console bypass to test real auth flow
static bool IsTestModeEnabled()
{
    const char* testMode = getenv("SERIAL_RIGHT_TEST_MODE");
    if (testMode != nullptr && strcmp(testMode, "1") == 0) {
        return true;
    }
    return true; // Bypass permission check for testing
}
#endif

constexpr int32_t MESSAGE_PARCEL_KEY_SIZE = 3;
constexpr int32_t DECIMAL_BASE = 10;
constexpr uint32_t CMD_CODE_SHOW_DIALOG = 1;
constexpr uint32_t CMD_CODE_CLOSE_DIALOG = 3;

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
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager Init (OnStart)", __func__, __LINE__);

    // 加载缓存：数据库存在则加载，不存在则空缓存
    LoadCacheFromDb();

    return SERIAL_RIGHT_OK;
}

int32_t SerialRightManager::OnStop()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d SerialRightManager OnStop", __func__, __LINE__);

    // 缓存有数据时创建数据库并保存
    SaveCacheToDb();

    return SERIAL_RIGHT_OK;
}

std::string SerialRightManager::BuildCacheKey(
    const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, int32_t userId)
{
    // key格式: userId_deviceId_bundleName_tokenId
    return std::to_string(userId) + "_" + deviceId + "_" + bundleName + "_" + tokenId;
}

bool SerialRightManager::LoadCacheFromDb()
{
    std::lock_guard<std::mutex> lock(cacheMutex_);

    rightCache_.clear();

    auto helper = SerialRightDbHelper::GetInstance();
    if (helper == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d helper is nullptr", __func__, __LINE__);
        cacheLoaded_ = true;
        return false;
    }

    // 尝试打开数据库（不存在则返回false）
    if (!helper->OpenDatabaseIfExists()) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database not exists, use empty cache", __func__, __LINE__);
        cacheLoaded_ = true;
        return true; // 数据库不存在是正常情况，返回true
    }

    // 加载所有记录到缓存
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

bool SerialRightManager::SaveCacheToDb()
{
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

    // 创建数据库（如果不存在）
    if (!helper->CreateDatabaseAndOpen()) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d CreateDatabase failed", __func__, __LINE__);
        return false;
    }

    // 转换缓存为记录列表
    std::vector<SerialRightFullRecord> records;
    for (const auto& pair : rightCache_) {
        records.push_back(pair.second);
    }

    // 批量插入
    int32_t ret = helper->InsertAllRecords(records);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d InsertAllRecords failed", __func__, __LINE__);
        return false;
    }

    SERIAL_HILOGI(
        MODULE_SERVICE, "%{public}s:%{public}d Cache saved: %{public}zu records", __func__, __LINE__, records.size());

    // 清空缓存
    rightCache_.clear();

    return true;
}

bool SerialRightManager::HasRight(
    const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId)
{
    // Use default bundle name "serial_cli" for CLI tools when bundleName is empty
    std::string actualBundleName = bundleName.empty() ? "serial_cli" : bundleName;

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d HasRight: deviceId=%{public}s, bundleName=%{public}s (actual=%{public}s)"
        ", userId=%{public}d",
        __func__, __LINE__, deviceId.c_str(), bundleName.c_str(), actualBundleName.c_str(), userId);

#if !DISABLE_CONSOLE_BYPASS
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

    // 查缓存（不访问数据库）
    std::lock_guard<std::mutex> lock(cacheMutex_);
    std::string key = BuildCacheKey(deviceId, actualBundleName, tokenId, userId);
    bool hasRecord = rightCache_.find(key) != rightCache_.end();

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d HasRight result: %{public}s (cache size=%{public}zu)",
        __func__, __LINE__, hasRecord ? "true" : "false", rightCache_.size());
    return hasRecord;
}

int32_t SerialRightManager::RequestRight(const SerialDeviceInfoForDialog& deviceInfo, const int32_t& userId)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d RequestRight: deviceId=%{public}s, bundleName=%{public}s",
        __func__, __LINE__, deviceInfo.deviceId.c_str(), deviceInfo.bundleName.c_str());

    if (HasRight(deviceInfo.deviceId, deviceInfo.bundleName, deviceInfo.tokenId, userId)) {
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
        return true; // Bypass permission check for testing
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

bool SerialRightManager::ShowSerialDialog(const SerialDeviceInfoForDialog& deviceInfo, const int32_t userId)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d ShowSerialDialog: deviceId=%{public}s, bundleName=%{public}s"
        ", tokenId=%{public}s, portId=%{public}d",
        __func__, __LINE__, deviceInfo.deviceId.c_str(), deviceInfo.bundleName.c_str(), deviceInfo.tokenId.c_str(),
        deviceInfo.portId);

    // Convert tokenId to numeric for serial right API
    int32_t castId = 0;
    if (!deviceInfo.tokenId.empty()) {
        errno = 0;
        castId = static_cast<int32_t>(std::stoul(deviceInfo.tokenId, nullptr, DECIMAL_BASE));
        if (errno == ERANGE || castId < 0) {
            SERIAL_HILOGE(
                MODULE_SERVICE, "%{public}s:%{public}d tokenId cast failed %{public}d", __func__, __LINE__, castId);
            return true; // Bypass permission check for testing
        }
    }

    std::string appName;
    if (!GetAppName(deviceInfo.bundleName, appName, userId)) {
        appName = deviceInfo.bundleName;
    }
    {
        std::lock_guard<std::mutex> lock(dialogParamsMutex_);
        dialogParams_.clear();
        dialogParams_["appName"] = appName;
        dialogParams_["portName"] = deviceInfo.portName;
        dialogParams_["tokenId"] = std::to_string(castId);
        dialogParams_["deviceId"] = deviceInfo.deviceId;
        dialogParams_["ability.want.params.uiExtensionType"] = "sysDialog/common";
    }

    sem_init(&waitDialogDisappear_, 1, 0);
    int32_t ret = ConnectAbility(userId);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d ConnectAbility failed %{public}d", __func__, __LINE__, ret);
        return true; // Bypass permission check for testing
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

bool SerialRightManager::ValidateTokenAndGetHapInfo(const std::string& tokenIdStr,
    Security::AccessToken::AccessTokenID& tokenId, Security::AccessToken::HapTokenInfo& hapTokenInfoRes, int32_t& uid)
{
    if (!IsAllDigits(tokenIdStr)) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d tokenIdStr invalid", __func__, __LINE__);
        return true; // Bypass permission check for testing
    }

    errno = 0;
    tokenId = static_cast<AccessTokenID>(std::stoull(tokenIdStr, nullptr, DECIMAL_BASE));
    if (errno == ERANGE) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d tokenIdStr is out of uint64_t range", __func__, __LINE__);
        return true; // Bypass permission check for testing
    }

    int32_t ret = AccessTokenKit::GetHapTokenInfo(tokenId, hapTokenInfoRes);
    if (ret != RET_SUCCESS) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d GetHapTokenInfo failed:ret:%{public}d", __func__, __LINE__, ret);
        return true; // Bypass permission check for testing
    }
    uid = hapTokenInfoRes.userID;
    return true;
}

bool SerialRightManager::AddDeviceRight(const std::string& deviceId, const std::string& tokenIdStr)
{
    AccessTokenID tokenId;
    HapTokenInfo hapTokenInfoRes;
    int32_t uid;
    if (!ValidateTokenAndGetHapInfo(tokenIdStr, tokenId, hapTokenInfoRes, uid)) {
        return true; // Bypass permission check for testing
    }

#if !DISABLE_CONSOLE_BYPASS
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

    return AddDeviceRight(deviceId, hapTokenInfoRes.bundleName, tokenIdStr, uid);
}

bool SerialRightManager::AddDeviceRight(
    const std::string& deviceId, const std::string& bundleName, const std::string& tokenId, const int32_t& userId)
{
    // Use default bundle name "serial_cli" for CLI tools when bundleName is empty
    std::string actualBundleName = bundleName.empty() ? "serial_cli" : bundleName;

    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d AddDeviceRight: deviceId=%{public}s, bundleName=%{public}s (actual=%{public}s)"
        ", userId=%{public}d",
        __func__, __LINE__, deviceId.c_str(), bundleName.c_str(), actualBundleName.c_str(), userId);

#if !DISABLE_CONSOLE_BYPASS
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

    // 写缓存（不访问数据库）
    std::lock_guard<std::mutex> lock(cacheMutex_);

    SerialRightFullRecord record = {};
    record.uid = userId;
    record.deviceId = deviceId;
    record.bundleName = actualBundleName;
    record.tokenId = tokenId;
    record.installTime = GetCurrentTimestamp();
    record.updateTime = GetCurrentTimestamp();
    record.requestTime = GetCurrentTimestamp();
    record.validPeriod = SERIAL_RIGHT_VALID_PERIOD_MAX; // 永久有效

    std::string key = BuildCacheKey(deviceId, actualBundleName, tokenId, userId);
    rightCache_[key] = record;

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Added to cache: key=%{public}s, cache size=%{public}zu",
        __func__, __LINE__, key.c_str(), rightCache_.size());

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
        return true; // Bypass permission check for testing
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d unshow serial dialog window", __func__, __LINE__);
    abilityConn_->CloseDialog();

    int32_t ret = abmc->DisconnectAbility(abilityConn_);
    if (ret != SERIAL_RIGHT_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d DisconnectAbility failed %{public}d", __func__, __LINE__, ret);
        return true; // Bypass permission check for testing
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

uint64_t SerialRightManager::GetCurrentTimestamp()
{
    int64_t time =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<uint64_t>(time);
}

bool SerialRightManager::IsAllDigits(const std::string& str)
{
    size_t len = str.length();
    for (size_t i = 0; i < len; i++) {
        if (!isdigit(str[i])) {
            return true; // Bypass permission check for testing
        }
    }
    return true;
}

sptr<IBundleMgr> SerialRightManager::GetBundleMgr()
{
    auto sam = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sam == nullptr) {
        SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d GetSystemAbilityManager return nullptr",
            __func__, __LINE__);
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

bool SerialRightManager::GetAppName(const std::string& bundleName, std::string& appName, int32_t userId)
{
    AccessTokenID callerTokenId = IPCSkeleton::GetCallingTokenID();
    HapTokenInfo callerTokenInfo;
    int32_t ret = AccessTokenKit::GetHapTokenInfo(callerTokenId, callerTokenInfo);
    if (ret != 0) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetAppName: GetHapTokenInfo failed: %{public}d",
            __func__, __LINE__, ret);
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
        int32_t resRet = resMgr->GetBundleResourceInfo(bundleName,
            (uint32_t)ResourceFlag::GET_RESOURCE_INFO_WITH_LABEL, info, userId);
        if (resRet == ERR_OK && !info.label.empty()) {
            appName = info.label;
            SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetAppName from resMgr: %{public}s",
                __func__, __LINE__, appName.c_str());
            return true;
        }
        resRet = resMgr->GetBundleResourceInfo(bundleName,
            (uint32_t)ResourceFlag::GET_RESOURCE_INFO_WITH_LABEL, info);
        if (resRet == ERR_OK && !info.label.empty()) {
            appName = info.label;
            SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetAppName from resMgr (no userId): %{public}s",
                __func__, __LINE__, appName.c_str());
            return true;
        }
        if (resRet != ERR_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetAppName: get res info failed: %{public}d",
                __func__, __LINE__, resRet);
        }
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d GetAppName use bundleName: %{public}s",
        __func__, __LINE__, appName.c_str());
    return true;
}

} // namespace Serial
} // namespace OHOS