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

#ifndef SERIAL_TOKEN_ID_H
#define SERIAL_TOKEN_ID_H

#include <charconv>
#include <cstdint>
#include <string>

namespace OHOS {
namespace Serial {
inline bool ParseSerialTokenId(const std::string& text, uint32_t& tokenId) {
    constexpr int32_t DECIMAL_BASE = 10;
    uint32_t parsed = 0;
    if (text.empty() || text.front() < '1' || text.front() > '9') {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, DECIMAL_BASE);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || parsed == 0) {
        return false;
    }
    tokenId = parsed;
    return true;
}
} // namespace Serial
} // namespace OHOS

#endif // SERIAL_TOKEN_ID_H
