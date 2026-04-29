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
constexpr const char* DATABASE_DIR = "/data/service/el1/public/serial_manager/";
constexpr int DATABASE_DIR_MODE = 0755;
constexpr const char* CREATE_TABLE_SQL = "CREATE TABLE IF NOT EXISTS serial_right("
                                         "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                         "uid INTEGER, "
                                         "port_name TEXT, "
                                         "bundle_name TEXT, "
                                         "token_id TEXT, "
                                         "install_time INTEGER, "
                                         "update_time INTEGER, "
                                         "request_time INTEGER, "
                                         "valid_period INTEGER)";
} // namespace

std::shared_ptr<SerialRightDbHelper> SerialRightDbHelper::instance_ = nullptr;

static bool EnsureDatabaseDirExists()
{
    struct stat st;
    if (stat(DATABASE_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d database directory already exists: %{public}s", __func__,
            __LINE__, DATABASE_DIR);
        return true;
    }

    int ret = mkdir(DATABASE_DIR, DATABASE_DIR_MODE);
    if (ret != 0 && errno != EEXIST) {
        SERIAL_HILOGE(MODULE_SERVICE,
            "%{public}s:%{public}d failed to create database directory %{public}s, errno=%{public}d", __func__,
            __LINE__, DATABASE_DIR, errno);
        return false;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d database directory created: %{public}s", __func__, __LINE__,
        DATABASE_DIR);
    return true;
}

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
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d constructed", __func__, __LINE__);

    // Ensure database directory exists with retry mechanism
    if (!EnsureDatabaseDirExists()) {
        SERIAL_HILOGW(MODULE_SERVICE,
            "%{public}s:%{public}d EnsureDatabaseDirExists failed, retrying with direct mkdir", __func__, __LINE__);
        // Fallback: try direct mkdir if helper function failed (e.g. permission issue)
        int ret = mkdir(DATABASE_DIR, DATABASE_DIR_MODE);
        if (ret != 0 && errno != EEXIST) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d direct mkdir also failed, errno=%{public}d", __func__,
                __LINE__, errno);
            return;
        }
        SERIAL_HILOGI(
            MODULE_SERVICE, "%{public}s:%{public}d direct mkdir succeeded or directory exists", __func__, __LINE__);
    }

    RdbStoreConfig config(DATABASE_DIR + std::string(DATABASE_NAME));
    config.SetSecurityLevel(SecurityLevel::S1);

    SerialRightRdbOpenCallback callback;
    int32_t errCode = E_OK;
    rdbStore_ = RdbHelper::GetRdbStore(config, DATABASE_VERSION, callback, errCode);
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d GetRdbStore failed: %{public}d", __func__, __LINE__, errCode);
        return;
    }

    // Verify database is working by executing a simple query
    std::shared_ptr<ResultSet> resultSet = rdbStore_->QuerySql("SELECT name FROM sqlite_master WHERE type='table'");
    if (resultSet == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d QuerySql failed, database may be corrupted", __func__,
            __LINE__);
        rdbStore_ = nullptr;
        return;
    }
    resultSet->Close();
    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d database verified successfully", __func__, __LINE__);
}

bool SerialRightDbHelper::IsRecordExpired(int32_t uid, const std::string& portName, const std::string& bundleName,
    const std::string& tokenId, uint64_t expiredTime)
{
    std::vector<struct SerialRightAppInfo> infos;
    int32_t ret = QueryRightRecord(uid, portName, bundleName, tokenId, infos);
    if (ret <= 0) {
        SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d QueryRightRecord ret=%{public}d, record not found",
            __func__, __LINE__, ret);
        return true;
    }

    for (size_t i = 0; i < infos.size(); i++) {
        if (!IsRecordExpired(infos.at(i), expiredTime)) {
            SERIAL_HILOGD(
                MODULE_SERVICE, "%{public}s:%{public}d found valid record at index %{public}zu", __func__, __LINE__, i);
            return false;
        }
    }

    SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d all records expired", __func__, __LINE__);
    return true;
}

bool SerialRightDbHelper::IsRecordExpired(const struct SerialRightAppInfo& info, uint64_t expiredTime)
{
    if (info.validPeriod == SERIAL_RIGHT_VALID_PERIOD_MAX) {
        return false;
    }

    if (info.requestTime + info.validPeriod <= expiredTime) {
        SERIAL_HILOGD(MODULE_SERVICE,
            "%{public}s:%{public}d record expired: requestTime=%{public}" PRIu64 ", validPeriod=%{public}" PRIu64
            ", expiredTime=%{public}" PRIu64,
            __func__, __LINE__, info.requestTime, info.validPeriod, expiredTime);
        return true;
    }
    return false;
}

int32_t SerialRightDbHelper::AddRightRecord(const std::string& portName, const std::string& bundleName,
    const std::string& tokenId, struct SerialRightAppInfo& info)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    ValuesBucket values;
    values.PutInt("uid", info.uid);
    values.PutString("port_name", portName);
    values.PutString("bundle_name", bundleName);
    values.PutString("token_id", tokenId);
    values.PutLong("install_time", static_cast<int64_t>(info.installTime));
    values.PutLong("update_time", static_cast<int64_t>(info.updateTime));
    values.PutLong("request_time", static_cast<int64_t>(info.requestTime));
    values.PutLong("valid_period", static_cast<int64_t>(info.validPeriod));

    int64_t rowId = 0;
    int32_t ret = rdbStore_->Insert(rowId, TABLE_NAME, values);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Insert failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    info.primaryKeyId = static_cast<uint32_t>(rowId);
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d Insert success, rowId=%{public}" PRId64
        ", uid=%{public}d, port=%{public}s, bundle=%{public}s, token=%{public}s"
        ", request_time=%{public}" PRIu64 ", valid_period=%{public}" PRIu64,
        __func__, __LINE__, rowId, info.uid, portName.c_str(), bundleName.c_str(), tokenId.c_str(), info.requestTime,
        info.validPeriod);
    return E_OK;
}

int32_t SerialRightDbHelper::QueryUserRightRecord(int32_t uid, std::vector<struct SerialRightAppInfo>& infos)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid);
    std::vector<std::string> columns;
    return QueryAndGetResult(predicates, columns, infos);
}

int32_t SerialRightDbHelper::QueryPortRightRecord(
    int32_t uid, const std::string& portName, std::vector<struct SerialRightAppInfo>& infos)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid)->EqualTo("port_name", portName);
    std::vector<std::string> columns;
    return QueryAndGetResult(predicates, columns, infos);
}

int32_t SerialRightDbHelper::QueryAppRightRecord(
    int32_t uid, const std::string& bundleName, std::vector<struct SerialRightAppInfo>& infos)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid)->EqualTo("bundle_name", bundleName);
    std::vector<std::string> columns;
    return QueryAndGetResult(predicates, columns, infos);
}

int32_t SerialRightDbHelper::QueryRightRecord(int32_t uid, const std::string& portName, const std::string& bundleName,
    const std::string& tokenId, std::vector<struct SerialRightAppInfo>& infos)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid)
        ->EqualTo("port_name", portName)
        ->EqualTo("bundle_name", bundleName)
        ->EqualTo("token_id", tokenId);
    std::vector<std::string> columns;
    return QueryAndGetResult(predicates, columns, infos);
}

int32_t SerialRightDbHelper::QueryRightRecordUids(std::vector<std::string>& uids)
{
    RdbPredicates predicates(TABLE_NAME);
    std::vector<std::string> columns;
    columns.push_back("uid");
    return QueryAndGetResultColumnValues(predicates, columns, "uid", uids);
}

int32_t SerialRightDbHelper::QueryRightRecordApps(int32_t uid, std::vector<std::string>& apps)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid);
    std::vector<std::string> columns;
    columns.push_back("bundle_name");
    return QueryAndGetResultColumnValues(predicates, columns, "bundle_name", apps);
}

int32_t SerialRightDbHelper::UpdateRightRecord(
    int32_t uid, const std::string& portName, const std::string& bundleName, struct SerialRightAppInfo& info)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    ValuesBucket values;
    values.PutLong("install_time", static_cast<int64_t>(info.installTime));
    values.PutLong("update_time", static_cast<int64_t>(info.updateTime));
    values.PutLong("request_time", static_cast<int64_t>(info.requestTime));
    values.PutLong("valid_period", static_cast<int64_t>(info.validPeriod));

    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid)->EqualTo("port_name", portName)->EqualTo("bundle_name", bundleName);

    int32_t changedRows = 0;
    int32_t ret = rdbStore_->Update(changedRows, values, predicates);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Update failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Update success, changedRows=%{public}d", __func__, __LINE__,
        changedRows);
    return E_OK;
}

int32_t SerialRightDbHelper::AddOrUpdateRightRecord(int32_t uid, const std::string& portName,
    const std::string& bundleName, const std::string& tokenId, struct SerialRightAppInfo& info)
{
    bool isUpdate = false;
    int32_t ret = CheckIfNeedUpdateEx(isUpdate, uid, portName, bundleName, tokenId);
    if (ret != E_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d CheckIfNeedUpdateEx failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    return AddOrUpdateRightRecordEx(isUpdate, uid, portName, bundleName, tokenId, info);
}

int32_t SerialRightDbHelper::DeleteRightRecord(
    int32_t uid, const std::string& portName, const std::string& bundleName, const std::string& tokenId)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid)
        ->EqualTo("port_name", portName)
        ->EqualTo("bundle_name", bundleName)
        ->EqualTo("token_id", tokenId);
    return DeleteAndNoOtherOperation(predicates);
}

int32_t SerialRightDbHelper::DeletePortRightRecord(int32_t uid, const std::string& portName)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid)->EqualTo("port_name", portName);
    return DeleteAndNoOtherOperation(predicates);
}

int32_t SerialRightDbHelper::DeleteAppRightRecord(int32_t uid, const std::string& bundleName)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid)->EqualTo("bundle_name", bundleName);
    return DeleteAndNoOtherOperation(predicates);
}

int32_t SerialRightDbHelper::DeleteAppsRightRecord(int32_t uid, const std::vector<std::string>& bundleNames)
{
    if (bundleNames.empty()) {
        return E_OK;
    }

    std::string whereClause = "uid = ? AND bundle_name IN (";
    std::vector<std::string> whereArgs;
    whereArgs.push_back(std::to_string(uid));

    for (size_t i = 0; i < bundleNames.size(); i++) {
        if (i == 0) {
            whereClause += "?";
        } else {
            whereClause += ", ?";
        }
        whereArgs.push_back(bundleNames.at(i));
    }
    whereClause += ")";

    return DeleteAndNoOtherOperation(whereClause, whereArgs);
}

int32_t SerialRightDbHelper::DeleteUidRightRecord(int32_t uid)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("uid", uid);
    return DeleteAndNoOtherOperation(predicates);
}

int32_t SerialRightDbHelper::DeleteNormalExpiredRightRecord(int32_t uid, uint64_t expiredTime)
{
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d DeleteNormalExpiredRightRecord: uid=%{public}d, expiredTime=%{public}" PRIu64, __func__,
        __LINE__, uid, expiredTime);

    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    // Diagnostic: query all records for this uid before deletion
    std::vector<struct SerialRightAppInfo> allRecords;
    int32_t queryRet = QueryUserRightRecord(uid, allRecords);
    SERIAL_HILOGI(MODULE_SERVICE,
        "%{public}s:%{public}d QueryUserRightRecord returned %{public}d records for uid=%{public}d", __func__, __LINE__,
        queryRet, uid);
    for (size_t i = 0; i < allRecords.size(); i++) {
        const struct SerialRightAppInfo& info = allRecords.at(i);
        SERIAL_HILOGI(MODULE_SERVICE,
            "%{public}s:%{public}d Record[%{public}zu]: primaryKeyId=%{public}u"
            ", request_time=%{public}" PRIu64 ", valid_period=%{public}" PRIu64 ", expireCheck=%{public}" PRIu64
            " (request_time+valid_period)",
            __func__, __LINE__, i, info.primaryKeyId, info.requestTime, info.validPeriod,
            info.requestTime + info.validPeriod);
    }

    // Delete expired records where:
    // - uid matches
    // - valid_period > MIN (not uninitialized with value 0)
    // - valid_period != MAX (not permanent authorization)
    // - request_time + valid_period <= expiredTime (actually expired)
    std::string whereClause = "uid = ? AND valid_period > ? AND valid_period != ? AND request_time + valid_period <= ?";
    std::vector<std::string> whereArgs;
    whereArgs.push_back(std::to_string(uid));
    whereArgs.push_back(std::to_string(SERIAL_RIGHT_VALID_PERIOD_MIN));
    whereArgs.push_back(std::to_string(SERIAL_RIGHT_VALID_PERIOD_MAX));
    whereArgs.push_back(std::to_string(expiredTime));

    return DeleteAndNoOtherOperation(whereClause, whereArgs);
}

int32_t SerialRightDbHelper::DeleteValidPeriodRightRecord(long validPeriod, const std::string& portName)
{
    RdbPredicates predicates(TABLE_NAME);
    predicates.EqualTo("valid_period", static_cast<int64_t>(validPeriod))->EqualTo("port_name", portName);
    return DeleteAndNoOtherOperation(predicates);
}

int32_t SerialRightDbHelper::CheckIfNeedUpdateEx(
    bool& isUpdate, int32_t uid, const std::string& portName, const std::string& bundleName, const std::string& tokenId)
{
    std::vector<struct SerialRightAppInfo> infos;
    int32_t ret = QueryRightRecord(uid, portName, bundleName, tokenId, infos);
    if (ret > 0 && infos.size() > 0) {
        isUpdate = true;
        SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d record exists, need update", __func__, __LINE__);
    } else {
        isUpdate = false;
        SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d record not exists, need insert", __func__, __LINE__);
    }
    return E_OK;
}

int32_t SerialRightDbHelper::AddOrUpdateRightRecordEx(bool isUpdate, int32_t uid, const std::string& portName,
    const std::string& bundleName, const std::string& tokenId, struct SerialRightAppInfo& info)
{
    if (isUpdate) {
        return UpdateRightRecord(uid, portName, bundleName, info);
    } else {
        return AddRightRecord(portName, bundleName, tokenId, info);
    }
}

int32_t SerialRightDbHelper::GetResultSetTableInfo(
    const std::shared_ptr<ResultSet>& resultSet, struct SerialRightTableInfo& table)
{
    if (resultSet == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d resultSet is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    int32_t ret = resultSet->GetRowCount(table.rowCount);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetRowCount failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    ret = resultSet->GetColumnCount(table.columnCount);
    if (ret != E_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d GetColumnCount failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    resultSet->GetColumnIndex("id", table.primaryKeyIndex);
    resultSet->GetColumnIndex("uid", table.uidIndex);
    resultSet->GetColumnIndex("install_time", table.installTimeIndex);
    resultSet->GetColumnIndex("update_time", table.updateTimeIndex);
    resultSet->GetColumnIndex("request_time", table.requestTimeIndex);
    resultSet->GetColumnIndex("valid_period", table.validPeriodIndex);
    resultSet->GetColumnIndex("port_name", table.portNameIndex);
    resultSet->GetColumnIndex("bundle_name", table.bundleNameIndex);

    return E_OK;
}

int32_t SerialRightDbHelper::GetResultRightRecordEx(
    const std::shared_ptr<ResultSet>& resultSet, std::vector<struct SerialRightAppInfo>& infos)
{
    if (resultSet == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d resultSet is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    struct SerialRightTableInfo table;
    int32_t ret = GetResultSetTableInfo(resultSet, table);
    if (ret != E_OK) {
        return ret;
    }

    if (table.rowCount <= 0) {
        SERIAL_HILOGD(MODULE_SERVICE, "%{public}s:%{public}d no records found", __func__, __LINE__);
        return 0;
    }

    int32_t rowCount = 0;
    ret = resultSet->GetRowCount(rowCount);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetRowCount failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    for (int32_t i = 0; i < rowCount; i++) {
        ret = resultSet->GoToRow(i);
        if (ret != E_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GoToRow %{public}d failed: %{public}d", __func__,
                __LINE__, i, ret);
            continue;
        }

        struct SerialRightAppInfo info;

        int64_t value = 0;
        resultSet->GetLong(table.primaryKeyIndex, value);
        info.primaryKeyId = static_cast<uint32_t>(value);

        resultSet->GetLong(table.uidIndex, value);
        info.uid = static_cast<int32_t>(value);

        resultSet->GetLong(table.installTimeIndex, value);
        info.installTime = static_cast<uint64_t>(value);

        resultSet->GetLong(table.updateTimeIndex, value);
        info.updateTime = static_cast<uint64_t>(value);

        resultSet->GetLong(table.requestTimeIndex, value);
        info.requestTime = static_cast<uint64_t>(value);

        resultSet->GetLong(table.validPeriodIndex, value);
        info.validPeriod = static_cast<uint64_t>(value);

        infos.push_back(info);
    }

    return static_cast<int32_t>(infos.size());
}

int32_t SerialRightDbHelper::QueryAndGetResult(const RdbPredicates& rdbPredicates,
    const std::vector<std::string>& columns, std::vector<struct SerialRightAppInfo>& infos)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    std::shared_ptr<ResultSet> resultSet = rdbStore_->Query(rdbPredicates, columns);
    if (resultSet == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Query failed: resultSet is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    int32_t ret = GetResultRightRecordEx(resultSet, infos);
    resultSet->Close();
    return ret;
}

int32_t SerialRightDbHelper::QueryAndGetResultColumnValues(const RdbPredicates& rdbPredicates,
    const std::vector<std::string>& columns, const std::string& columnName, std::vector<std::string>& columnValues)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    std::shared_ptr<ResultSet> resultSet = rdbStore_->Query(rdbPredicates, columns);
    if (resultSet == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Query failed: resultSet is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    int32_t rowCount = 0;
    int32_t ret = resultSet->GetRowCount(rowCount);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GetRowCount failed: %{public}d", __func__, __LINE__, ret);
        resultSet->Close();
        return ret;
    }

    int32_t columnIndex = 0;
    ret = resultSet->GetColumnIndex(columnName, columnIndex);
    if (ret != E_OK) {
        SERIAL_HILOGE(
            MODULE_SERVICE, "%{public}s:%{public}d GetColumnIndex failed: %{public}d", __func__, __LINE__, ret);
        resultSet->Close();
        return ret;
    }

    for (int32_t i = 0; i < rowCount; i++) {
        ret = resultSet->GoToRow(i);
        if (ret != E_OK) {
            SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d GoToRow %{public}d failed: %{public}d", __func__,
                __LINE__, i, ret);
            continue;
        }

        std::string value;
        ret = resultSet->GetString(columnIndex, value);
        if (ret != E_OK) {
            SERIAL_HILOGE(
                MODULE_SERVICE, "%{public}s:%{public}d GetString failed: %{public}d", __func__, __LINE__, ret);
            continue;
        }

        columnValues.push_back(value);
    }

    resultSet->Close();
    return static_cast<int32_t>(columnValues.size());
}

int32_t SerialRightDbHelper::DeleteAndNoOtherOperation(
    const std::string& whereClause, const std::vector<std::string>& whereArgs)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    int32_t deletedRows = 0;
    int32_t ret = rdbStore_->Delete(deletedRows, TABLE_NAME, whereClause, whereArgs);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Delete failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Delete success, deletedRows=%{public}d", __func__, __LINE__,
        deletedRows);
    return E_OK;
}

int32_t SerialRightDbHelper::DeleteAndNoOtherOperation(const RdbPredicates& rdbPredicates)
{
    if (rdbStore_ == nullptr) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d rdbStore_ is nullptr", __func__, __LINE__);
        return E_ERROR;
    }

    int32_t deletedRows = 0;
    int32_t ret = rdbStore_->Delete(deletedRows, rdbPredicates);
    if (ret != E_OK) {
        SERIAL_HILOGE(MODULE_SERVICE, "%{public}s:%{public}d Delete failed: %{public}d", __func__, __LINE__, ret);
        return ret;
    }

    SERIAL_HILOGI(MODULE_SERVICE, "%{public}s:%{public}d Delete success, deletedRows=%{public}d", __func__, __LINE__,
        deletedRows);
    return E_OK;
}

} // namespace Serial
} // namespace OHOS