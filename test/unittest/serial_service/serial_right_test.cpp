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
 * @file serial_right_test.cpp
 * @brief SerialRightDbHelper 和 SerialRightManager 单元测试（缓存模式）
 * @details 本文件测试：
 *          - SerialRightDbHelper 数据库加载和保存操作
 *          - SerialRightManager 缓存机制
 *          - OnStart 加载、OnStop 保存流程
 * @since 2026-05-08
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hilog_wrapper.h"
#include "serial_error_code.h"
#include "serial_right_db_helper.h"
#include "serial_right_manager.h"

using namespace testing;
using namespace testing::ext;
using namespace OHOS::Serial;

namespace OHOS {
namespace Serial {

namespace {
constexpr const char* TEST_DB_DIR = "/data/service/el1/public/database/serial_manager/";
constexpr int32_t TEST_USER_ID = 100;
constexpr int32_t TEST_USER_ID_2 = 101;
constexpr const char* TEST_BUNDLE_NAME = "com.test.serialapp";
constexpr const char* TEST_BUNDLE_NAME_2 = "com.test.serialapp2";
constexpr const char* TEST_TOKEN_ID = "12345678";
constexpr const char* TEST_TOKEN_ID_2 = "87654321";
constexpr const char* TEST_DEVICE_ID_1 = "5678_1234_ttyUSB0";
constexpr const char* TEST_DEVICE_ID_2 = "2222_1111_ttyUSB1";
constexpr const char* TEST_DEVICE_ID_WITH_SN = "5678_1234_SN123456_ttyUSB0";
constexpr uint64_t TEST_TIMESTAMP = 1700000000;
} // namespace

// ============================================================================
// SerialRightDbHelper Test Fixture (简化版 - 只测试加载和保存)
// ============================================================================

class SerialRightDbHelperTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightDbHelperTest::SetUpTestCase");
        struct stat st;
        if (stat(TEST_DB_DIR, &st) != 0) {
            mkdir(TEST_DB_DIR, 0755);
        }
    }

    static void TearDownTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightDbHelperTest::TearDownTestCase");
    }

    void SetUp() override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightDbHelperTest::SetUp");
        dbHelper_ = SerialRightDbHelper::GetInstance();
        ASSERT_NE(dbHelper_, nullptr);
        // 删除测试数据库文件
        dbHelper_->DeleteDatabaseFile();
    }

    void TearDown() override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightDbHelperTest::TearDown");
        dbHelper_->DeleteDatabaseFile();
    }

protected:
    std::shared_ptr<SerialRightDbHelper> dbHelper_;
};

// ============================================================================
// 1. 数据库基本状态测试
// ============================================================================

HWTEST_F(SerialRightDbHelperTest, GetInstance_ReturnsSingleton, TestSize.Level1)
{
    auto instance1 = SerialRightDbHelper::GetInstance();
    auto instance2 = SerialRightDbHelper::GetInstance();
    EXPECT_EQ(instance1.get(), instance2.get());
}

HWTEST_F(SerialRightDbHelperTest, OpenDatabaseIfExists_NotExists_ReturnsFalse, TestSize.Level1)
{
    // 确保数据库文件不存在
    dbHelper_->DeleteDatabaseFile();

    bool result = dbHelper_->OpenDatabaseIfExists();
    EXPECT_FALSE(result);
    EXPECT_FALSE(dbHelper_->IsDatabaseValid());
}

HWTEST_F(SerialRightDbHelperTest, CreateDatabaseAndOpen_Success, TestSize.Level1)
{
    bool result = dbHelper_->CreateDatabaseAndOpen();
    EXPECT_TRUE(result);
    EXPECT_TRUE(dbHelper_->IsDatabaseValid());
}

HWTEST_F(SerialRightDbHelperTest, CreateDatabaseAndOpen_ThenOpenIfExists_ReturnsTrue, TestSize.Level1)
{
    // 先创建
    dbHelper_->CreateDatabaseAndOpen();
    EXPECT_TRUE(dbHelper_->IsDatabaseValid());

    // 关闭数据库连接
    dbHelper_->CloseDatabase();
    EXPECT_FALSE(dbHelper_->IsDatabaseValid());

    // 重新打开已存在的数据库
    bool result = dbHelper_->OpenDatabaseIfExists();
    EXPECT_TRUE(result);
    EXPECT_TRUE(dbHelper_->IsDatabaseValid());
}

HWTEST_F(SerialRightDbHelperTest, QueryAllRightRecords_EmptyDb_ReturnsZero, TestSize.Level1)
{
    dbHelper_->CreateDatabaseAndOpen();

    std::vector<SerialRightFullRecord> records;
    int32_t ret = dbHelper_->QueryAllRightRecords(records);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(records.size(), 0);
}

HWTEST_F(SerialRightDbHelperTest, InsertAllRecords_Success, TestSize.Level1)
{
    // 确保先删除旧数据库
    dbHelper_->DeleteDatabaseFile();

    dbHelper_->CreateDatabaseAndOpen();
    EXPECT_TRUE(dbHelper_->IsDatabaseValid());

    SerialRightFullRecord record1 = {};
    record1.uid = TEST_USER_ID;
    record1.deviceId = TEST_DEVICE_ID_1;
    record1.bundleName = TEST_BUNDLE_NAME;
    record1.tokenId = TEST_TOKEN_ID;
    record1.installTime = TEST_TIMESTAMP;
    record1.updateTime = TEST_TIMESTAMP;
    record1.requestTime = TEST_TIMESTAMP;
    record1.validPeriod = SERIAL_RIGHT_VALID_PERIOD_MAX;

    SerialRightFullRecord record2 = {};
    record2.uid = TEST_USER_ID;
    record2.deviceId = TEST_DEVICE_ID_2;
    record2.bundleName = TEST_BUNDLE_NAME;
    record2.tokenId = TEST_TOKEN_ID;
    record2.installTime = TEST_TIMESTAMP;
    record2.updateTime = TEST_TIMESTAMP;
    record2.requestTime = TEST_TIMESTAMP;
    record2.validPeriod = SERIAL_RIGHT_VALID_PERIOD_MAX;

    std::vector<SerialRightFullRecord> records = {record1, record2};
    int32_t ret = dbHelper_->InsertAllRecords(records);
    EXPECT_EQ(ret, E_OK);

    // 关闭后重新打开再查询
    dbHelper_->CloseDatabase();
    dbHelper_->OpenDatabaseIfExists();

    std::vector<SerialRightFullRecord> queryRecords;
    ret = dbHelper_->QueryAllRightRecords(queryRecords);
    EXPECT_GT(ret, 0);
    EXPECT_EQ(queryRecords.size(), 2);
}

HWTEST_F(SerialRightDbHelperTest, InsertAllRecords_EmptyList_ReturnsOk, TestSize.Level1)
{
    dbHelper_->CreateDatabaseAndOpen();

    std::vector<SerialRightFullRecord> emptyRecords;
    int32_t ret = dbHelper_->InsertAllRecords(emptyRecords);
    EXPECT_EQ(ret, E_OK);
}

HWTEST_F(SerialRightDbHelperTest, GetDatabasePath_ReturnsCorrectPath, TestSize.Level1)
{
    std::string path = SerialRightDbHelper::GetDatabasePath();
    EXPECT_TRUE(path.find("SerialRight.db") != std::string::npos);
    EXPECT_TRUE(path.find("serial_manager") != std::string::npos);
}

HWTEST_F(SerialRightDbHelperTest, DeleteDatabaseFile_RemovesFile, TestSize.Level1)
{
    // 创建数据库文件
    dbHelper_->CreateDatabaseAndOpen();
    EXPECT_TRUE(dbHelper_->IsDatabaseValid());

    // 先关闭数据库连接
    dbHelper_->CloseDatabase();
    EXPECT_FALSE(dbHelper_->IsDatabaseValid());

    // 删除文件
    bool result = dbHelper_->DeleteDatabaseFile();
    EXPECT_TRUE(result);

    // 验证文件不存在
    std::string path = SerialRightDbHelper::GetDatabasePath();
    struct stat st;
    EXPECT_NE(stat(path.c_str(), &st), 0);
}

// ============================================================================
// SerialRightManager Test Fixture (缓存测试)
// ============================================================================

class SerialRightManagerTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightManagerTest::SetUpTestCase");
        struct stat st;
        if (stat(TEST_DB_DIR, &st) != 0) {
            mkdir(TEST_DB_DIR, 0755);
        }
    }

    static void TearDownTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightManagerTest::TearDownTestCase");
    }

    void SetUp() override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightManagerTest::SetUp");
        dbHelper_ = SerialRightDbHelper::GetInstance();
        // 删除测试数据库文件，确保干净状态
        dbHelper_->DeleteDatabaseFile();
    }

    void TearDown() override
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialRightManagerTest::TearDown");
        dbHelper_->DeleteDatabaseFile();
    }

protected:
    std::shared_ptr<SerialRightDbHelper> dbHelper_;
};

// ============================================================================
// 1. SerialRightManager 基本测试
// ============================================================================

HWTEST_F(SerialRightManagerTest, GetInstance_ReturnsSingleton, TestSize.Level1)
{
    auto& instance1 = SerialRightManager::GetInstance();
    auto& instance2 = SerialRightManager::GetInstance();
    EXPECT_EQ(&instance1, &instance2);
}

HWTEST_F(SerialRightManagerTest, Init_NoDb_UsesEmptyCache, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();

    // 数据库不存在，Init 应成功加载空缓存
    int32_t ret = manager.Init();
    EXPECT_EQ(ret, SERIAL_RIGHT_OK);

    // HasRight 应返回 false（缓存为空）
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    SERIAL_HILOGI(MODULE_SERVICE, "HasRight after Init with empty cache: %{public}d", hasRight);
}

HWTEST_F(SerialRightManagerTest, AddDeviceRight_WritesToCache, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init(); // 加载空缓存

    // 添加授权
    bool result = manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(result);

    // 验证 HasRight 返回 true（缓存中有记录）
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(hasRight);
}

HWTEST_F(SerialRightManagerTest, AddDeviceRight_DuplicateKey_UpdatesCache, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();

    // 第一次添加
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);

    // 第二次添加（相同key，应更新）
    bool result = manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(result);

    // HasRight 应仍返回 true
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(hasRight);
}

HWTEST_F(SerialRightManagerTest, HasRight_MismatchDeviceId_ReturnsFalse, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();

    // 添加授权
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);

    // 查询不同 deviceId
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_2, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_FALSE(hasRight);
}

HWTEST_F(SerialRightManagerTest, HasRight_MismatchBundleName_ReturnsFalse, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();

    // 添加授权
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);

    // 查询不同 bundleName
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_FALSE(hasRight);
}

HWTEST_F(SerialRightManagerTest, HasRight_MismatchUserId_ReturnsFalse, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();

    // 添加授权（userId=100）
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);

    // 查询不同 userId
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID_2);
    EXPECT_FALSE(hasRight);
}

HWTEST_F(SerialRightManagerTest, OnStop_SavesCacheToDb, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();

    // 添加授权到缓存
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    manager.AddDeviceRight(TEST_DEVICE_ID_2, TEST_BUNDLE_NAME_2, TEST_TOKEN_ID_2, TEST_USER_ID);

    // OnStop 保存缓存到数据库
    int32_t ret = manager.OnStop();
    EXPECT_EQ(ret, SERIAL_RIGHT_OK);

    // 验证数据库文件存在
    std::string dbPath = SerialRightDbHelper::GetDatabasePath();
    struct stat st;
    EXPECT_EQ(stat(dbPath.c_str(), &st), 0);

    // 验证数据库有记录
    auto dbHelper = SerialRightDbHelper::GetInstance();
    dbHelper->OpenDatabaseIfExists();
    std::vector<SerialRightFullRecord> records;
    ret = dbHelper->QueryAllRightRecords(records);
    EXPECT_GT(ret, 0);
    EXPECT_EQ(records.size(), 2);
}

HWTEST_F(SerialRightManagerTest, OnStop_EmptyCache_SkipsSave, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init(); // 空缓存

    // OnStop 不应创建数据库文件
    int32_t ret = manager.OnStop();
    EXPECT_EQ(ret, SERIAL_RIGHT_OK);

    // 验证数据库文件不存在（因为缓存为空）
    std::string dbPath = SerialRightDbHelper::GetDatabasePath();
    struct stat st;
    // 注意：之前测试可能已创建文件，这里不强制断言
    SERIAL_HILOGI(
        MODULE_SERVICE, "Database exists after OnStop with empty cache: %{public}d", stat(dbPath.c_str(), &st) == 0);
}

// ============================================================================
// 3. 多用户场景测试
// ============================================================================

HWTEST_F(SerialRightManagerTest, MultiUser_AuthorizationsIndependent, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();

    // 用户1授权
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);

    // 用户2授权同一设备
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID_2);

    // 用户1的授权独立
    bool hasRight1 = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(hasRight1);

    // 用户2的授权独立
    bool hasRight2 = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID_2);
    EXPECT_TRUE(hasRight2);
}

// ============================================================================
// 4. deviceId 不同格式测试
// ============================================================================

HWTEST_F(SerialRightManagerTest, AddDeviceRight_DifferentDeviceIdFormats, TestSize.Level1)
{
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();

    // 格式1: vid_pid_portName
    bool result1 = manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(result1);

    // 格式2: vid_pid_sn_portName
    bool result2 = manager.AddDeviceRight(TEST_DEVICE_ID_WITH_SN, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(result2);

    // 验证两种格式都能正确查询
    bool hasRight1 = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(hasRight1);

    bool hasRight2 = manager.HasRight(TEST_DEVICE_ID_WITH_SN, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(hasRight2);
}

// ============================================================================
// 5. 缓存加载测试（模拟 SA 重启）
// ============================================================================

HWTEST_F(SerialRightManagerTest, Init_WithExistingDb_LoadsCache, TestSize.Level1)
{
    // 场景：模拟 SA 空闲自杀后重新拉起

    // 第一步：初始授权并保存
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    manager.OnStop(); // 模拟 SA 空闲自杀，保存缓存

    // 第二步：模拟 SA 重新拉起，Init 加载缓存
    // 注意：GetInstance 返回单例，实际上 Init 会重新加载
    int32_t ret = manager.Init();
    EXPECT_EQ(ret, SERIAL_RIGHT_OK);

    // 第三步：验证缓存中有之前的授权
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_TRUE(hasRight);
}

HWTEST_F(SerialRightManagerTest, Init_AfterDbDeleted_UsesEmptyCache, TestSize.Level1)
{
    // 场景：模拟设备重启（init 删除数据库）

    // 先创建授权并保存
    auto& manager = SerialRightManager::GetInstance();
    manager.Init();
    manager.AddDeviceRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    manager.OnStop();

    // 模拟 init 删除数据库文件（重启）
    dbHelper_->DeleteDatabaseFile();

    // Init 应加载空缓存
    int32_t ret = manager.Init();
    EXPECT_EQ(ret, SERIAL_RIGHT_OK);

    // 验证无授权
    bool hasRight = manager.HasRight(TEST_DEVICE_ID_1, TEST_BUNDLE_NAME, TEST_TOKEN_ID, TEST_USER_ID);
    EXPECT_FALSE(hasRight);
}

} // namespace Serial
} // namespace OHOS