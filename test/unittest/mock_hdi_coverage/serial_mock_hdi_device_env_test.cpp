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
 * @file serial_mock_hdi_device_env_test.cpp
 * @brief Mock HDI 设备枚举与环境场景单元测试
 * @details 覆盖无需真实设备的设备枚举生命周期场景（对应覆盖表
 *          develop/lifecycle 变体的轮次 + errcodetest 企业策略轮次 + 配置转换）：
 *          - 设备列表正常 / VID 变化 / 设备减少 / 空列表 / 恢复
 *          - "/dev/" 前缀剥离
 *          - 企业策略禁用 RS-232 时仅保留 USB 串口
 *          - SerialConfig → HDI SerialConfig 字段转换
 *          全部用例基于 MockSerials 运行，可在无真实设备环境执行；
 *          无真实 HDI 服务时按需求默认返回 success。
 * @since 2026-05-12
 */

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hilog_wrapper.h"
#include "mock_hdi_env.h"
#include "mock_serial_hdi.h"
#include "serial_error_code.h"
#include "serial_service.h"
#include "system_ability_definition.h"

using namespace testing;
using namespace testing::ext;
using namespace OHOS::Serial;
using namespace OHOS::HDI::Serial::V1_0;

namespace OHOS {
namespace Serial {

namespace {
constexpr int32_t TEST_BAUD_RATE = 115200;
constexpr int32_t TEST_DATA_BITS = 8;
constexpr int32_t TEST_STOP_BITS = 1;
constexpr int32_t TEST_PARITY_NONE = 0;
constexpr int32_t VID_A = 1;
constexpr int32_t PID_A = 11;
constexpr int32_t VID_B = 2;
constexpr int32_t PID_B = 22;
constexpr int32_t VID_CHANGED = 99;
const char *PORT_S0 = "ttyS0";
const char *PORT_S1 = "ttyS1";
const char *PORT_USB = "ttyUSB0";
const char *PORT_ACM_DEV = "/dev/ttyACM0";
const char *PORT_ACM = "ttyACM0";
const char *MFR_A = "MfgA";
const char *MFR_B = "MfgB";
const char *MFR_USB = "USBMfg";
} // namespace

/**
 * @class SerialMockHdiDeviceEnvTest
 * @brief Mock HDI 设备枚举场景测试夹具
 */
class SerialMockHdiDeviceEnvTest : public testing::Test {
public:
    static void SetUpTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiDeviceEnvTest::SetUpTestCase");
    }

    static void TearDownTestCase()
    {
        SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiDeviceEnvTest::TearDownTestCase");
    }

    void SetUp() override;
    void TearDown() override;

protected:
    sptr<MockSerials> mockHdi_ = nullptr;
    sptr<SerialService> service_ = nullptr;

    static SerialDeviceInfo MakeHdiDevice(const std::string &portName, int32_t pid, int32_t vid,
        const std::string &manufacturer);
    void SetMockDeviceList(const std::vector<SerialDeviceInfo> &devices);
};

void SerialMockHdiDeviceEnvTest::SetUp()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiDeviceEnvTest::SetUp");
    // NiceMock：抑制未预期调用的 GMOCK WARNING（如 OpenSerial 内部隐式调用 QueryDevices），
    // 未显式声明预期的调用仍执行 ON_CALL 默认动作，行为不变；EXPECT_CALL 设定的预期照常严格校验
    mockHdi_ = new NiceMock<MockSerials>();
    ASSERT_NE(mockHdi_, nullptr);

    service_ = new SerialService(SERIAL_SYSTEM_ABILITY_ID, false);
    ASSERT_NE(service_, nullptr);
    service_->serialsProxy_ = mockHdi_;
}

void SerialMockHdiDeviceEnvTest::TearDown()
{
    SERIAL_HILOGI(MODULE_SERVICE, "SerialMockHdiDeviceEnvTest::TearDown");
    // 企业策略（isRs232Disabled_）为实例级状态，随本用例的 SerialService 实例销毁自动复位
    mockHdi_ = nullptr;
    service_ = nullptr;
}

SerialDeviceInfo SerialMockHdiDeviceEnvTest::MakeHdiDevice(const std::string &portName, int32_t pid, int32_t vid,
    const std::string &manufacturer)
{
    SerialDeviceInfo info;
    info.portName = portName;
    info.productId = pid;
    info.vendorId = vid;
    info.manufacturer = manufacturer;
    return info;
}

void SerialMockHdiDeviceEnvTest::SetMockDeviceList(const std::vector<SerialDeviceInfo> &devices)
{
    mockHdi_->SetMockDevices(devices);
}

// ============================================================================
// 1. 设备枚举生命周期轮次（Mock HDI 模拟热插拔/变化）
// ============================================================================

/**
 * @brief 正常轮次：两台设备字段完整映射（portName/vid/pid/manufacturer）
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_RoundNormal_TwoDevicesMapped, TestSize.Level1)
{
    SetMockDeviceList({
        MakeHdiDevice(PORT_S0, PID_A, VID_A, MFR_A),
        MakeHdiDevice(PORT_S1, PID_B, VID_B, MFR_B),
    });

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_OK);
    ASSERT_EQ(devices.size(), 2u);
    EXPECT_EQ(devices[0].portName, PORT_S0);
    EXPECT_EQ(devices[0].productId, PID_A);
    EXPECT_EQ(devices[0].vendorId, VID_A);
    EXPECT_EQ(devices[0].manufacturer, MFR_A);
    EXPECT_EQ(devices[1].portName, PORT_S1);
    EXPECT_EQ(devices[1].productId, PID_B);
    EXPECT_EQ(devices[1].vendorId, VID_B);
    EXPECT_EQ(devices[1].manufacturer, MFR_B);
}

/**
 * @brief VID 变化轮次：设备 VID 更新后枚举结果同步更新
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_RoundVidChanged_InfoUpdated, TestSize.Level1)
{
    SetMockDeviceList({MakeHdiDevice(PORT_S0, PID_A, VID_CHANGED, MFR_A)});

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_OK);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].portName, PORT_S0);
    EXPECT_EQ(devices[0].vendorId, VID_CHANGED);
    EXPECT_EQ(devices[0].productId, PID_A);
}

/**
 * @brief 设备减少轮次：拔出 ttyS0 后仅剩 ttyS1
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_RoundShrunk_ReturnsRemaining, TestSize.Level1)
{
    SetMockDeviceList({MakeHdiDevice(PORT_S1, PID_B, VID_B, MFR_B)});

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_OK);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].portName, PORT_S1);
}

/**
 * @brief 空列表轮次：全部设备离线时返回空列表且不报错
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_RoundEmpty_ReturnsEmptyList, TestSize.Level1)
{
    SetMockDeviceList({});

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_EQ(devices.size(), 0u);
}

/**
 * @brief 恢复轮次：设备重新插入后枚举列表恢复
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_RoundRestored_ListRecovered, TestSize.Level1)
{
    SetMockDeviceList({
        MakeHdiDevice(PORT_S0, PID_A, VID_A, MFR_A),
        MakeHdiDevice(PORT_S1, PID_B, VID_B, MFR_B),
    });

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_OK);
    EXPECT_EQ(devices.size(), 2u);
}

/**
 * @brief HDI 返回带 "/dev/" 前缀的端口名时，服务层枚举结果剥离前缀
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_DevPrefix_StrippedInServiceList, TestSize.Level1)
{
    SetMockDeviceList({MakeHdiDevice(PORT_ACM_DEV, PID_A, VID_A, MFR_A)});

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_OK);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].portName, PORT_ACM);
}

// ============================================================================
// 2. 企业策略（对应 errcodetest 轮次的 Mock 覆盖）
// ============================================================================

/**
 * @brief 企业策略禁用 RS-232 时，枚举结果仅保留 USB 串口设备
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_EnterpriseDisable_KeepsUsbOnly, TestSize.Level1)
{
    service_->SetSerialManagePolicy(true);
    SetMockDeviceList({
        MakeHdiDevice(PORT_S0, PID_A, VID_A, MFR_A),
        MakeHdiDevice(PORT_USB, PID_B, VID_B, MFR_USB),
    });

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_OK);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].portName, PORT_USB);
}

/**
 * @brief 企业策略禁用 RS-232 且全部为 RS-232 设备时返回策略错误码
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, QueryDevices_EnterpriseDisable_AllRs232_ReturnsPolicyError, TestSize.Level1)
{
    service_->SetSerialManagePolicy(true);
    SetMockDeviceList({MakeHdiDevice(PORT_S0, PID_A, VID_A, MFR_A)});

    std::vector<SerialPortInfo> devices;
    int32_t ret = service_->QuerySerialDevices(devices);

    EXPECT_EQ(ret, SERIAL_ERR_ENTERPRISE_POLICY);
    EXPECT_EQ(devices.size(), 0u);
}

// ============================================================================
// 3. 配置转换（SerialConfig → HDI SerialConfig）
// ============================================================================

/**
 * @brief ConvertConfig 将服务层配置的 8 个字段逐一映射到 HDI 配置
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, ConvertConfig_MapsAllFields, TestSize.Level1)
{
    SerialConfig serviceConfig;
    serviceConfig.baudRate = TEST_BAUD_RATE;
    serviceConfig.dataBits = TEST_DATA_BITS;
    serviceConfig.stopBits = TEST_STOP_BITS;
    serviceConfig.parity = TEST_PARITY_NONE;
    serviceConfig.rtscts = false;
    serviceConfig.xon = true;
    serviceConfig.xoff = false;
    serviceConfig.xany = true;

    // 与生产代码一致（serial_service.cpp:542）：ConvertConfig 返回 HDI 类型，
    // 显式限定为 OHOS::HDI::Serial::V1_0::SerialConfig，避免被 using 解析为 IDL 的
    // OHOS::Serial::SerialConfig（两者为不同类型且无隐式转换）
    OHOS::HDI::Serial::V1_0::SerialConfig hdiConfig = service_->ConvertConfig(serviceConfig);

    EXPECT_EQ(hdiConfig.baudRate, TEST_BAUD_RATE);
    EXPECT_EQ(hdiConfig.dataBits, TEST_DATA_BITS);
    EXPECT_EQ(hdiConfig.stopBits, TEST_STOP_BITS);
    EXPECT_EQ(hdiConfig.parity, TEST_PARITY_NONE);
    EXPECT_EQ(hdiConfig.rtscts, false);
    EXPECT_EQ(hdiConfig.xon, true);
    EXPECT_EQ(hdiConfig.xoff, false);
    EXPECT_EQ(hdiConfig.xany, true);
}

// ============================================================================
// 4. 真实设备环境探测（无真实设备时默认返回 success）
// ============================================================================

/**
 * @brief 无真实串口设备/HDI 服务时，本用例默认返回 success；有则仅探测代理存在
 * @details 按需求约定：不执行任何真实设备读写，仅验证探测接口行为
 */
HWTEST_F(SerialMockHdiDeviceEnvTest, RealDeviceProbe_NoDevice_DefaultSuccess, TestSize.Level1)
{
    if (!MockHdiEnv::HasRealHdiEnvironment()) {
        GTEST_SUCCEED() << "[ENV] no real serial device/HDI service, default SUCCESS";
        return;
    }

    sptr<ISerials> proxy = MockHdiEnv::TryGetRealHdiProxy();
    EXPECT_NE(proxy, nullptr);
    EXPECT_TRUE(proxy->IsProxy());
    GTEST_SUCCEED() << "[ENV] real HDI proxy present, probe passed (no device I/O performed)";
}

} // namespace Serial
} // namespace OHOS
