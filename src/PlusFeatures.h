// SPDX-License-Identifier: MIT
#ifndef SRC_PLUSFEATURES_H_
#define SRC_PLUSFEATURES_H_

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace McBopomofo::Plus {
// No shell, interpreter, network, or locale-dependent parsing.
std::optional<std::string> Calculate(const std::string& expression);
std::string AutoSpace(const std::string& text, bool letters, bool digits);
std::vector<std::string> EmojiForText(const std::string& text);

}  // namespace McBopomofo::Plus
#endif
