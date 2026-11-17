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

#include "ipc_object_stub.h"
#include "serial_authorization_request.h"
#include "serial_right_manager.h"
#include "serial_token_id.h"
#include <future>
#include <gtest/gtest.h>
#include <limits>

using namespace testing::ext;

namespace OHOS {
namespace Serial {
namespace {
constexpr int32_t TEST_USER = 100;
constexpr int32_t OTHER_USER = 101;
constexpr const char* TEST_TOKEN = "2147483648";
constexpr const char* OTHER_TOKEN = "2147483649";
constexpr const char* TEST_PORT = "ttyUSB0";
constexpr const char* OTHER_PORT = "ttyUSB1";
constexpr const char* TEST_BUNDLE = "com.example.serial.boundary";
constexpr const char* TEST_REQUEST = "ttyUSB0#request-first";
constexpr const char* NEXT_REQUEST = "ttyUSB0#request-second";
constexpr const char* SYNTHETIC_PORT = "ttyTest0";
constexpr size_t EXPECTED_REMAINING_RECORDS = 2;
constexpr int32_t WAITER_TIMEOUT_MILLISECONDS = 2000;
} // namespace

class SerialAuthorizationBoundaryTest : public testing::Test {
public:
    void SetUp() override {
        auto& manager = SerialRightManager::GetInstance();
        manager.ClearDialogRequest();
        manager.rightCache_.clear();
    }

    void TearDown() override {
        SetUp();
    }

    void SetPendingRequest(const std::string& requestId = TEST_REQUEST) {
        auto& manager = SerialRightManager::GetInstance();
        manager.pendingUserId_ = TEST_USER;
        manager.pendingDeviceId_ = TEST_PORT;
        manager.dialogParams_["deviceId"] = requestId;
        manager.dialogParams_["tokenId"] = TEST_TOKEN;
        // Cache/request unit fixture, not a simulated real USB attachment.
        manager.dialogParams_["portName"] = SYNTHETIC_PORT;
        manager.pendingConnection_ =
            new SerialRightManager::SerialAbilityConn(manager.dialogParams_, TEST_PORT, TEST_USER);
    }
};

HWTEST_F(SerialAuthorizationBoundaryTest, TokenUint32Boundaries, TestSize.Level1) {
    uint32_t tokenId = 0;
    EXPECT_TRUE(ParseSerialTokenId(TEST_TOKEN, tokenId));
    EXPECT_EQ(tokenId, static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) + 1);
    EXPECT_TRUE(ParseSerialTokenId("4294967295", tokenId));
    EXPECT_EQ(tokenId, std::numeric_limits<uint32_t>::max());
    for (const char* invalid : {"", "0", "0001", "-1", "+1", " 1", "1x", "4294967296", "18446744073709551616"}) {
        EXPECT_FALSE(ParseSerialTokenId(invalid, tokenId));
    }
}

HWTEST_F(SerialAuthorizationBoundaryTest, RejectGrantOutsideRequest, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    SetPendingRequest();
    EXPECT_FALSE(manager.AuthorizePendingRequest(OTHER_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, OTHER_TOKEN, TEST_USER));
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, OTHER_USER));
    EXPECT_TRUE(manager.rightCache_.empty());
}

HWTEST_F(SerialAuthorizationBoundaryTest, GrantOnlyUntilRequestCleared, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    EXPECT_TRUE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.HasRight(TEST_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    manager.ClearDialogRequest();
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
}

HWTEST_F(SerialAuthorizationBoundaryTest, OldGrantCannotAuthorizeRepeatedPortRequest, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    SerialRightManager::SerialAbilityConn oldConnection(manager.dialogParams_);
    manager.ClearDialogRequest();
    SetPendingRequest(NEXT_REQUEST);
    EXPECT_EQ(oldConnection.dialogRequestParams_.at("deviceId"), TEST_REQUEST);
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.rightCache_.empty());
    EXPECT_TRUE(manager.AuthorizePendingRequest(NEXT_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.HasRight(TEST_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
}

HWTEST_F(SerialAuthorizationBoundaryTest, FailedConnectionWakesWaiter, TestSize.Level1) {
    SerialRightManager::SerialAbilityConn connection;
    connection.OnAbilityConnectDone(AppExecFwk::ElementName(), nullptr, SERIAL_RIGHT_OK);
    EXPECT_FALSE(connection.WaitForDisconnect(std::chrono::milliseconds(0)));
    EXPECT_TRUE(connection.dialogFinished_);
}

HWTEST_F(SerialAuthorizationBoundaryTest, LateDisconnectCannotCompleteNextRequest, TestSize.Level1) {
    SerialRightManager::SerialAbilityConn expired;
    SerialRightManager::SerialAbilityConn current;
    EXPECT_FALSE(expired.WaitForDisconnect(std::chrono::milliseconds(0)));
    expired.OnAbilityDisconnectDone(AppExecFwk::ElementName(), SERIAL_RIGHT_OK);
    EXPECT_FALSE(current.dialogFinished_);
    EXPECT_FALSE(expired.WaitForDisconnect(std::chrono::milliseconds(0)));
    current.OnAbilityDisconnectDone(AppExecFwk::ElementName(), SERIAL_RIGHT_OK);
    EXPECT_TRUE(current.WaitForDisconnect(std::chrono::milliseconds(0)));
}

HWTEST_F(SerialAuthorizationBoundaryTest, OfflineRevokesOnlyMatchingAttachmentAllUsers, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    manager.AddDeviceRight(TEST_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER);
    manager.AddDeviceRight(TEST_PORT, TEST_BUNDLE, OTHER_TOKEN, OTHER_USER);
    manager.AddDeviceRight(OTHER_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER);
    manager.AddDeviceRight(OTHER_PORT, TEST_BUNDLE, OTHER_TOKEN, OTHER_USER);
    manager.RevokeDeviceRights(TEST_PORT);
    EXPECT_FALSE(manager.HasRight(TEST_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_FALSE(manager.HasRight(TEST_PORT, TEST_BUNDLE, OTHER_TOKEN, OTHER_USER));
    EXPECT_TRUE(manager.HasRight(OTHER_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.HasRight(OTHER_PORT, TEST_BUNDLE, OTHER_TOKEN, OTHER_USER));
    manager.RevokeDeviceRights(TEST_PORT);
    EXPECT_EQ(manager.rightCache_.size(), EXPECTED_REMAINING_RECORDS);
}

HWTEST_F(SerialAuthorizationBoundaryTest, OfflineCancelsWaitAndRejectsOldReply, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    sptr<SerialRightManager::SerialAbilityConn> old = new SerialRightManager::SerialAbilityConn(manager.dialogParams_);
    manager.pendingConnection_ = old;
    manager.RevokeDeviceRights(TEST_PORT);
    EXPECT_FALSE(old->WaitForDisconnect(std::chrono::milliseconds(0)));
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    SetPendingRequest(NEXT_REQUEST);
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.AuthorizePendingRequest(NEXT_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
}

HWTEST_F(SerialAuthorizationBoundaryTest, OtherAttachmentOfflinePreservesPendingRequest, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    sptr<SerialRightManager::SerialAbilityConn> current = manager.pendingConnection_;
    manager.pendingConnection_ = current;
    manager.RevokeDeviceRights(OTHER_PORT);
    EXPECT_FALSE(current->dialogFinished_);
    EXPECT_EQ(manager.pendingUserId_, TEST_USER);
    EXPECT_TRUE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
}

HWTEST_F(SerialAuthorizationBoundaryTest, MissingRealAttachmentCannotApproveOrContinueWaiting, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    manager.dialogParams_["portName"] = TEST_PORT;
    // No fabricated attachment metadata: an unbound key must be rejected,
    // even if the isolated unit environment happens to have ttyUSB0.
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    SerialRightManager::SerialAbilityConn connection(manager.dialogParams_, TEST_PORT);
    EXPECT_FALSE(connection.WaitForDisconnect(std::chrono::seconds(SERIAL_DIALOG_TIMEOUT_SECONDS)));
    EXPECT_TRUE(manager.rightCache_.empty());
}

HWTEST_F(SerialAuthorizationBoundaryTest, GrantCompletesWithoutParentDisconnect, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    auto connection = manager.pendingConnection_;
    // A live local Binder endpoint is a unit-only transport fixture, not a
    // substituted SystemUI service or simulated device/permission decision.
    sptr<IRemoteObject> parent = new IPCObjectStub();
    connection->remoteObject_ = parent;
    auto waiter = std::async(std::launch::async, [connection]() {
        return connection->WaitForDisconnect(std::chrono::milliseconds(WAITER_TIMEOUT_MILLISECONDS));
    });
    ASSERT_TRUE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(waiter.get());
    EXPECT_EQ(connection->remoteObject_, parent);
    EXPECT_TRUE(manager.HasRight(TEST_PORT, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    // A late shared-parent failure cannot overwrite an accepted result.
    connection->OnAbilityDisconnectDone(AppExecFwk::ElementName(), SERIAL_RIGHT_FAILURE);
    EXPECT_TRUE(connection->WaitForDisconnect(std::chrono::milliseconds(0)));
}

HWTEST_F(SerialAuthorizationBoundaryTest, MissingCapturedConnectionCannotGrant, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    manager.pendingConnection_ = nullptr;
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.rightCache_.empty());
}

HWTEST_F(SerialAuthorizationBoundaryTest, CancelledCapturedConnectionCannotGrant, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    manager.pendingConnection_->CancelDialog();
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.rightCache_.empty());
}

HWTEST_F(SerialAuthorizationBoundaryTest, TimedOutCapturedConnectionCannotGrant, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    EXPECT_FALSE(manager.pendingConnection_->WaitForDisconnect(std::chrono::milliseconds(0)));
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.rightCache_.empty());
}

HWTEST_F(SerialAuthorizationBoundaryTest, ExpiredBeforeWaitCannotGrant, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    manager.pendingConnection_->deadline_ = std::chrono::steady_clock::now();
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.pendingConnection_->dialogFinished_);
    EXPECT_TRUE(manager.rightCache_.empty());
}

HWTEST_F(SerialAuthorizationBoundaryTest, DisconnectedCapturedConnectionCannotGrant, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    manager.pendingConnection_->OnAbilityDisconnectDone(AppExecFwk::ElementName(), SERIAL_RIGHT_OK);
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    EXPECT_TRUE(manager.rightCache_.empty());
}

HWTEST_F(SerialAuthorizationBoundaryTest, ImmutableConnectionRejectsRetargetedPendingTuple, TestSize.Level1) {
    auto& manager = SerialRightManager::GetInstance();
    SetPendingRequest();
    manager.dialogParams_["deviceId"] = NEXT_REQUEST;
    EXPECT_FALSE(manager.AuthorizePendingRequest(NEXT_REQUEST, TEST_BUNDLE, TEST_TOKEN, TEST_USER));
    manager.dialogParams_["deviceId"] = TEST_REQUEST;
    manager.dialogParams_["tokenId"] = OTHER_TOKEN;
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, OTHER_TOKEN, TEST_USER));
    manager.dialogParams_["tokenId"] = TEST_TOKEN;
    manager.pendingUserId_ = OTHER_USER;
    EXPECT_FALSE(manager.AuthorizePendingRequest(TEST_REQUEST, TEST_BUNDLE, TEST_TOKEN, OTHER_USER));
    EXPECT_FALSE(manager.pendingConnection_->dialogFinished_);
    EXPECT_TRUE(manager.rightCache_.empty());
}
} // namespace Serial
} // namespace OHOS
