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

#include "serial_right_db_helper.h"

#include <cerrno>
#include <ctime>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "hilog_wrapper.h"
#include "rdb_errno.h"
#include "string_ex.h"

namespace OHOS {
namespace Serial {

using namespace NativeRdb;

namespace {
constexpr const char* DATABASE_DIR = "/data/service/el1/public/database/serial_manager/";
constexpr const char* CREATE_TABLE_SQL = "CREATE TABLE IF NOT EXISTS serial_right("
                                         "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                         "uid INTEGER, "
                                         "device_id TEXT, "
                                         "bundle_name TEXT, "
                                         "token_id TEXT, "
                                         "install_time INTEGER, "
                                         "update_time INTEGER, "
                                         "request_time INTEGER, "
                                         "valid_period INTEGER)";
} // namespace

std::shared_ptr<SerialRightDbHelper> SerialRightDbHelper::instance_ = nullptr;

int32_t SerialRightRdbOpenCallback::OnCreate(RdbStore& rdbStore)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnCreate", __func__, __LINE__);
    int32_t ret = rdbStore.ExecuteSql(CREATE_TABLE_SQL);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d ExecuteSql failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }
    return E_OK;
}

int32_t SerialRightRdbOpenCallback::OnUpgrade(RdbStore& rdbStore, int32_t oldVersion, int32_t newVersion)
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d OnUpgrade: oldVersion=%{public}d, newVersion=%{public}d",
        __func__, __LINE__, oldVersion, newVersion);
    return E_OK;
}

std::shared_ptr<SerialRightDbHelper> SerialRightDbHelper::GetInstance()
{
    static std::mutex instanceMutex;
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance_ == nullptr) {
        instance_ = std::make_shared<SerialRightDbHelper>();
    }
    return instance_;
}

SerialRightDbHelper::SerialRightDbHelper()
{
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d constructed (lazy init mode)", __func__, __LINE__);
    // Do not create database in constructor, defer to OnStart/OnStop
    rdbStore_ = nullptr;
}

std::string SerialRightDbHelper::GetDatabasePath()
{
    return std::string(DATABASE_DIR) + std::string(DATABASE_NAME);
}

void SerialRightDbHelper::CloseDatabase()
{
    std::lock_guard<std::mutex> lock(databaseMutex_);
    if (rdbStore_ != nullptr) {
        rdbStore_ = nullptr;
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database closed", __func__, __LINE__);
    }
}

bool SerialRightDbHelper::DeleteDatabaseFile()
{
    // Close database connection first
    CloseDatabase();

    std::string dbPath = GetDatabasePath();
    if (unlink(dbPath.c_str()) == 0) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database file deleted: %{public}s", __func__, __LINE__,
            dbPath.c_str());
        return true;
    }
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database file not found or delete failed: %{public}s",
        __func__, __LINE__, dbPath.c_str());
    return false;
}

bool SerialRightDbHelper::OpenDatabaseIfExists() {
    std::lock_guard<std::mutex> lock(databaseMutex_);

    // Clean up old connection first
    rdbStore_ = nullptr;

    std::string dbPath = GetDatabasePath();
    struct stat st;
    if (stat(dbPath.c_str(), &st) != 0) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database file not exists: %{public}s", __func__, __LINE__,
            dbPath.c_str());
        return false;
    }

    RdbStoreConfig config(dbPath);
    config.SetSecurityLevel(SecurityLevel::S1);
    config.SetCreateNecessary(false);

    SerialRightRdbOpenCallback callback;
    int32_t errCode = E_OK;
    rdbStore_ = RdbHelper::GetRdbStore(config, DATABASE_VERSION, callback, errCode);
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d GetRdbStore failed: %{public}d", __func__, __LINE__, errCode);
        return false;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database opened successfully", __func__, __LINE__);
    return true;
}

int32_t SerialRightDbHelper::DeleteDeviceRightRecords(const std::string& deviceId) {
    if (deviceId.empty()) {
        return E_ERROR;
    }
    std::lock_guard<std::mutex> lock(databaseMutex_);
    if (rdbStore_ == nullptr) {
        std::string path = GetDatabasePath();
        struct stat metadata = {};
        if (stat(path.c_str(), &metadata) != 0) {
            return errno == ENOENT ? E_OK : E_ERROR;
        }
        RdbStoreConfig config(path);
        config.SetSecurityLevel(SecurityLevel::S1);
        config.SetCreateNecessary(false);
        SerialRightRdbOpenCallback callback;
        int32_t error = E_OK;
        rdbStore_ = RdbHelper::GetRdbStore(config, DATABASE_VERSION, callback, error);
        if (rdbStore_ == nullptr) {
            return error == E_OK ? E_ERROR : error;
        }
    }
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("device_id", deviceId);
    int32_t deletedRows = 0;
    return rdbStore_->Delete(deletedRows, predicates);
}

bool SerialRightDbHelper::CreateDatabaseAndOpen()
{
    std::lock_guard<std::mutex> lock(databaseMutex_);

    // Ensure directory exists
    struct stat st;
    if (stat(DATABASE_DIR, &st) != 0) {
        if (mkdir(DATABASE_DIR, 0770) != 0) {
            SERIAL_HILOGE(
                MODULE_SERVICE, "%{public}s:%{public}d mkdir failed: %{public}s", __func__, __LINE__, DATABASE_DIR);
            return false;
        }
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d Directory created: %{public}s", __func__, __LINE__, DATABASE_DIR);
    }

    std::string dbPath = GetDatabasePath();
    RdbStoreConfig config(dbPath);
    config.SetSecurityLevel(SecurityLevel::S1);

    SerialRightRdbOpenCallback callback;
    int32_t errCode = E_OK;
    rdbStore_ = RdbHelper::GetRdbStore(config, DATABASE_VERSION, callback, errCode);
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d CreateRdbStore failed: %{public}d", __func__, __LINE__, errCode);
        return false;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Database created/opened successfully: %{public}s", __func__,
        __LINE__, dbPath.c_str());
    return true;
}

int32_t SerialRightDbHelper::QueryAllRightRecords(std::vector<SerialRightFullRecord>& records)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    std::vector<std::string> columns;
    RdbPredicates predicates(TABLE_NAME);
    std::shared_ptr<ResultSet> resultSet = rdbStore_->Query(predicates, columns);
    if (resultSet == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Query failed", __func__, __LINE__);
        return E_ERROR;
    }

    int32_t rowCount = 0;
    int32_t ret = resultSet->GetRowCount(rowCount);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetRowCount failed: %{public}d", __func__, __LINE__, ret);
        resultSet->Close();
        return ret;
    }

    if (rowCount <= 0) {
        SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d no records found", __func__, __LINE__);
        resultSet->Close();
        return 0;
    }

    for (int32_t i = 0; i < rowCount; i++) {
        SerialRightFullRecord record;
        ret = ReadFullRecordFromRow(resultSet, i, record);
        if (ret == E_OK) {
            records.push_back(record);
        }
    }

    resultSet->Close();
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d QueryAllRightRecords found %{public}zu records", __func__,
        __LINE__, records.size());
    return static_cast<int32_t>(records.size());
}

int32_t SerialRightDbHelper::InsertAllRecords(const std::vector<SerialRightFullRecord>& records)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    if (records.empty()) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d no records to insert", __func__, __LINE__);
        return E_OK;
    }

    int32_t successCount = 0;
    for (const auto& record : records) {
        ValuesBucket values;
        values.PutInt("uid", record.uid);
        values.PutString("device_id", record.deviceId);
        values.PutString("bundle_name", record.bundleName);
        values.PutString("token_id", record.tokenId);
        values.PutLong("install_time", static_cast<int64_t>(record.installTime));
        values.PutLong("update_time", static_cast<int64_t>(record.updateTime));
        values.PutLong("request_time", static_cast<int64_t>(record.requestTime));
        values.PutLong("valid_period", static_cast<int64_t>(record.validPeriod));

        int64_t rowId = 0;
        int32_t ret = rdbStore_->Insert(rowId, TABLE_NAME, values);
        if (ret != E_OK) {
            SERIAL_HILOGW(MODULE_SERVICE, "%{public}s:%{public}d Insert failed for record", __func__, __LINE__);
            continue;
        }
        successCount++;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d InsertAllRecords: %{public}d/%{public}zu records inserted",
        __func__, __LINE__, successCount, records.size());
    return (successCount > 0) ? E_OK : E_ERROR;
}

int32_t SerialRightDbHelper::ReadFullRecordFromRow(
    const std::shared_ptr<ResultSet>& resultSet, int32_t rowIndex, SerialRightFullRecord& record)
{
    int32_t ret = resultSet->GoToRow(rowIndex);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GoToRow %{public}d failed: %{public}d", __func__, __LINE__,
            rowIndex, ret);
        return ret;
    }

    int32_t primaryKeyIndex = 0;
    int32_t uidIndex = 0;
    int32_t deviceIdIndex = 0;
    int32_t bundleNameIndex = 0;
    int32_t tokenIdIndex = 0;
    int32_t installTimeIndex = 0;
    int32_t updateTimeIndex = 0;
    int32_t requestTimeIndex = 0;
    int32_t validPeriodIndex = 0;

    resultSet->GetColumnIndex("id", primaryKeyIndex);
    resultSet->GetColumnIndex("uid", uidIndex);
    resultSet->GetColumnIndex("device_id", deviceIdIndex);
    resultSet->GetColumnIndex("bundle_name", bundleNameIndex);
    resultSet->GetColumnIndex("token_id", tokenIdIndex);
    resultSet->GetColumnIndex("install_time", installTimeIndex);
    resultSet->GetColumnIndex("update_time", updateTimeIndex);
    resultSet->GetColumnIndex("request_time", requestTimeIndex);
    resultSet->GetColumnIndex("valid_period", validPeriodIndex);

    int64_t value = 0;
    resultSet->GetLong(primaryKeyIndex, value);
    record.primaryKeyId = static_cast<uint32_t>(value);

    resultSet->GetLong(uidIndex, value);
    record.uid = static_cast<int32_t>(value);

    resultSet->GetString(deviceIdIndex, record.deviceId);
    resultSet->GetString(bundleNameIndex, record.bundleName);
    resultSet->GetString(tokenIdIndex, record.tokenId);

    resultSet->GetLong(installTimeIndex, value);
    record.installTime = static_cast<uint64_t>(value);

    resultSet->GetLong(updateTimeIndex, value);
    record.updateTime = static_cast<uint64_t>(value);

    resultSet->GetLong(requestTimeIndex, value);
    record.requestTime = static_cast<uint64_t>(value);

    resultSet->GetLong(validPeriodIndex, value);
    record.validPeriod = static_cast<uint64_t>(value);

    return E_OK;
}

} // namespace Serial
} // namespace OHOS
