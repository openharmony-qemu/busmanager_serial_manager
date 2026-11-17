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

#ifndef SERIAL_DEVICE_IDENTITY_H
#define SERIAL_DEVICE_IDENTITY_H

#include <string>
#include <sys/stat.h>

namespace OHOS {
namespace Serial {
constexpr const char* SERIAL_ATTACHMENT_SEPARATOR = "#attachment-";
constexpr const char* SERIAL_USB_PREFIX = "ttyUSB";
constexpr const char* SERIAL_ACM_PREFIX = "ttyACM";

inline bool IsSerialUsbAttachment(const std::string& portName) {
    for (const char* prefix : {SERIAL_USB_PREFIX, SERIAL_ACM_PREFIX}) {
        std::string start(prefix);
        if (portName.compare(0, start.size(), start) == 0 && portName.size() > start.size() &&
            portName.find_first_not_of("0123456789", start.size()) == std::string::npos) {
            return true;
        }
    }
    return false;
}

inline std::string SerialStatIdentity(const struct stat& metadata) {
    return std::to_string(metadata.st_dev) + ":" + std::to_string(metadata.st_ino) + ":" +
        std::to_string(metadata.st_rdev) + ":" + std::to_string(metadata.st_ctim.tv_sec) + ":" +
        std::to_string(metadata.st_ctim.tv_nsec);
}

// An attachment lifetime discriminator, not a hardware authenticity claim.
// Both nodes must exist: the devnode can briefly survive physical unplug.
inline bool ReadSerialAttachmentIdentity(const std::string& portName, std::string& identity) {
    if (!IsSerialUsbAttachment(portName)) {
        return false;
    }
    struct stat device = {};
    struct stat sysfs = {};
    std::string devicePath = "/dev/" + portName;
    std::string sysfsPath = "/sys/class/tty/" + portName + "/device";
    if (stat(devicePath.c_str(), &device) != 0 || !S_ISCHR(device.st_mode) || stat(sysfsPath.c_str(), &sysfs) != 0 ||
        !S_ISDIR(sysfs.st_mode)) {
        return false;
    }
    identity = SerialStatIdentity(device) + ":" + SerialStatIdentity(sysfs);
    return true;
}

inline bool SerialAttachmentMatches(const std::string& portName, const std::string& deviceId) {
    if (!IsSerialUsbAttachment(portName)) {
        return !deviceId.empty();
    }
    std::string identity;
    auto separator = deviceId.rfind(SERIAL_ATTACHMENT_SEPARATOR);
    return separator != std::string::npos && ReadSerialAttachmentIdentity(portName, identity) &&
        deviceId.substr(separator + std::string(SERIAL_ATTACHMENT_SEPARATOR).size()) == identity;
}
} // namespace Serial
} // namespace OHOS
#endif // SERIAL_DEVICE_IDENTITY_H
