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

#ifndef SERIAL_RIGHT_DB_HELPER_H
#define SERIAL_RIGHT_DB_HELPER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rdb_helper.h"
#include "rdb_open_callback.h"
#include "rdb_predicates.h"
#include "rdb_store.h"
#include "result_set.h"
#include "value_object.h"

namespace OHOS {
namespace Serial {

using namespace NativeRdb;

constexpr int64_t SERIAL_RIGHT_VALID_PERIOD_MIN = 0;
constexpr int64_t SERIAL_RIGHT_VALID_PERIOD_MAX = 0xFFFFFFFFL;
constexpr uint64_t SERIAL_RIGHT_VALID_PERIOD_SET = 0x0FFFFFFFL;

struct SerialRightAppInfo {
    uint32_t primaryKeyId; /* table primary key */
    int32_t uid;           /* app install user id */
    uint64_t installTime;  /* app install time */
    uint64_t updateTime;   /* app update time */
    uint64_t requestTime;  /* app request permission time */
    uint64_t validPeriod;  /* app permission valid period */
};

struct SerialRightFullRecord {
    uint32_t primaryKeyId;
    int32_t uid;
    std::string deviceId;
    std::string bundleName;
    std::string tokenId;
    uint64_t installTime;
    uint64_t updateTime;
    uint64_t requestTime;
    uint64_t validPeriod;
};

struct SerialRightTableInfo {
    int32_t rowCount;
    int32_t columnCount;
    int32_t primaryKeyIndex;
    int32_t uidIndex;
    int32_t installTimeIndex;
    int32_t updateTimeIndex;
    int32_t requestTimeIndex;
    int32_t validPeriodIndex;
    int32_t deviceIdIndex;
    int32_t bundleNameIndex;
};

class SerialRightDbHelper {
public:
    SerialRightDbHelper();
    SerialRightDbHelper(const SerialRightDbHelper&) = delete;
    SerialRightDbHelper& operator=(const SerialRightDbHelper&) = delete;

    static std::shared_ptr<SerialRightDbHelper> GetInstance();

    // Check if database is valid and ready for operations
    bool IsDatabaseValid() const
    {
        return rdbStore_ != nullptr;
    }

    // 延迟创建模式：OnStart 时尝试打开已有数据库（不创建）
    // OnStop 时创建数据库并保存缓存
    bool OpenDatabaseIfExists();
    bool CreateDatabaseAndOpen();
    void CloseDatabase(); // 关闭数据库连接

    int32_t AddRightRecord(const std::string& deviceId, const std::string& bundleName, const std::string& tokenId,
        SerialRightAppInfo& info);

    int32_t QueryUserRightRecord(int32_t uid, std::vector<SerialRightAppInfo>& infos);
    int32_t QueryPortRightRecord(int32_t uid, const std::string& deviceId, std::vector<SerialRightAppInfo>& infos);
    int32_t QueryAppRightRecord(int32_t uid, const std::string& bundleName, std::vector<SerialRightAppInfo>& infos);
    int32_t QueryRightRecord(int32_t uid, const std::string& deviceId, const std::string& bundleName,
        const std::string& tokenId, std::vector<SerialRightAppInfo>& infos);
    int32_t QueryRightRecordUids(std::vector<std::string>& uids);
    int32_t QueryRightRecordApps(int32_t uid, std::vector<std::string>& apps);
    int32_t QueryAllRightRecords(std::vector<SerialRightFullRecord>& records);

    // OnStop 保存时批量插入
    int32_t InsertAllRecords(const std::vector<SerialRightFullRecord>& records);

    // 数据库文件操作
    static std::string GetDatabasePath();
    bool DeleteDatabaseFile(); // 非静态方法，需要调用 CloseDatabase()

private:
    int32_t GetResultSetTableInfo(const std::shared_ptr<ResultSet>& resultSet, SerialRightTableInfo& table);
    int32_t GetResultRightRecordEx(const std::shared_ptr<ResultSet>& resultSet, std::vector<SerialRightAppInfo>& infos);
    int32_t QueryAndGetResult(const RdbPredicates& rdbPredicates, const std::vector<std::string>& columns,
        std::vector<SerialRightAppInfo>& infos);
    int32_t ReadFullRecordFromRow(
        const std::shared_ptr<ResultSet>& resultSet, int32_t rowIndex, SerialRightFullRecord& record);

    static std::shared_ptr<SerialRightDbHelper> instance_;
    std::mutex databaseMutex_;
    std::shared_ptr<RdbStore> rdbStore_;

    static constexpr const char* DATABASE_NAME = "SerialRight.db";
    static constexpr int32_t DATABASE_VERSION = 1;
    static constexpr const char* TABLE_NAME = "serial_right";
};

class SerialRightRdbOpenCallback : public RdbOpenCallback {
public:
    int32_t OnCreate(RdbStore& rdbStore) override;
    int32_t OnUpgrade(RdbStore& rdbStore, int32_t oldVersion, int32_t newVersion) override;
};

} // namespace Serial
} // namespace OHOS

#endif // SERIAL_RIGHT_DB_HELPER_H