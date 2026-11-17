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
#ifndef SERIAL_AUTHORIZATION_REQUEST_H
#define SERIAL_AUTHORIZATION_REQUEST_H

#include <array>
#include <cerrno>
#include <cstdint>
#include <string>
#include <sys/random.h>

namespace OHOS {
namespace Serial {
constexpr size_t SERIAL_AUTHORIZATION_NONCE_BYTES = 16;
constexpr unsigned int SERIAL_AUTHORIZATION_NIBBLE_BITS = 4;
constexpr uint8_t SERIAL_AUTHORIZATION_NIBBLE_MASK = 15;

inline bool BuildSerialAuthorizationRequestId(const std::string& deviceId, std::string& requestId) {
    if (deviceId.empty()) {
        return false;
    }
    std::array<uint8_t, SERIAL_AUTHORIZATION_NONCE_BYTES> nonce {};
    size_t filled = 0;
    while (filled < nonce.size()) {
        ssize_t result = getrandom(nonce.data() + filled, nonce.size() - filled, GRND_NONBLOCK);
        if (result <= 0) {
            if (result < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        filled += static_cast<size_t>(result);
    }
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    std::string encoded = deviceId + "#request-";
    for (uint8_t byte : nonce) {
        encoded.push_back(HEX_DIGITS[byte >> SERIAL_AUTHORIZATION_NIBBLE_BITS]);
        encoded.push_back(HEX_DIGITS[byte & SERIAL_AUTHORIZATION_NIBBLE_MASK]);
    }
    requestId = encoded;
    return true;
}
} // namespace Serial
} // namespace OHOS
#endif
