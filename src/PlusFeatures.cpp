// SPDX-License-Identifier: MIT
#include "PlusFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "Engine/UTF8Helper.h"

namespace McBopomofo::Plus {
namespace {
class Calculator {
 public:
  explicit Calculator(const std::string& input) : input_(input) {}
  double run() {
    double result = sum(0);
    space();
    if (pos_ != input_.size() || !std::isfinite(result)) throw 0;
    return result;
  }
 private:
  void space() { while (pos_ < input_.size() && input_[pos_] == ' ') ++pos_; }
  bool take(char c) {
    space();
    if (pos_ < input_.size() && input_[pos_] == c) { ++pos_; return true; }
    return false;
  }
  double sum(int depth) {
    double v = product(depth);
    while (true) {
      if (take('+')) v += product(depth);
      else if (take('-')) v -= product(depth);
      else return v;
      if (!std::isfinite(v)) throw 0;
    }
  }
  double product(int depth) {
    double v = atom(depth);
    while (true) {
      if (take('*')) v *= atom(depth);
      else if (take('/')) {
        double divisor = atom(depth);
        if (divisor == 0) throw 0;
        v /= divisor;
      } else if (take('%')) {
        double divisor = atom(depth);
        if (divisor == 0) throw 0;
        v = std::fmod(v, divisor);
      } else return v;
      if (!std::isfinite(v)) throw 0;
    }
  }
  double atom(int depth) {
    if (depth > 32) throw 0;
    if (take('+')) return atom(depth + 1);
    if (take('-')) return -atom(depth + 1);
    if (take('(')) {
      double v = sum(depth + 1);
      if (!take(')')) throw 0;
      return v;
    }
    space();
    size_t start = pos_;
    bool digit = false;
    while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') {
      digit = true; ++pos_;
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') {
        digit = true; ++pos_;
      }
    }
    if (!digit) throw 0;
    std::istringstream stream(input_.substr(start, pos_ - start));
    stream.imbue(std::locale::classic());
    double v;
    if (!(stream >> v) || !std::isfinite(v)) throw 0;
    return v;
  }
  const std::string& input_;
  size_t pos_ = 0;
};
std::vector<std::string> Syllables(const std::string& reading) {
  std::vector<std::string> result;
  size_t start = 0;
  while (start < reading.size()) {
    size_t end = reading.find('-', start);
    result.push_back(reading.substr(start, end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return result;
}
// Decode a single, already split UTF-8 scalar. Input is engine-generated UTF-8.
uint32_t Scalar(const std::string& c) {
  if (c.empty()) return 0;
  auto b = static_cast<unsigned char>(c[0]);
  if (b < 128) return b;
  uint32_t v = b & (c.size() == 2 ? 31 : c.size() == 3 ? 15 : 7);
  for (size_t i = 1; i < c.size(); ++i) v = (v << 6) | (c[i] & 63);
  return v;
}
bool Han(const std::string& c) {
  uint32_t v = Scalar(c);
  return (v >= 0x3400 && v <= 0x9fff) || (v >= 0xf900 && v <= 0xfaff) ||
         (v >= 0x20000 && v <= 0x323af);
}
bool Ascii(const std::string& c, bool letters, bool digits) {
  if (c.size() != 1) return false;
  return (letters && ((c[0] >= 'a' && c[0] <= 'z') || (c[0] >= 'A' && c[0] <= 'Z'))) ||
         (digits && c[0] >= '0' && c[0] <= '9');
}
}  // namespace
std::optional<std::string> Calculate(const std::string& expression) {
  if (expression.empty() || expression.size() > 256) return std::nullopt;
  try {
    double result = Calculator(expression).run();
    if (result == 0) result = 0;  // No negative zero.
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(12) << result;
    return out.str();
  } catch (...) { return std::nullopt; }
}
std::string AutoSpace(const std::string& text, bool letters, bool digits) {
  const auto chars = Split(text);
  std::string result;
  for (size_t i = 0; i < chars.size(); ++i) {
    if (i && ((Han(chars[i - 1]) && Ascii(chars[i], letters, digits)) ||
              (Ascii(chars[i - 1], letters, digits) && Han(chars[i])))) result += ' ';
    result += chars[i];
  }
  return result;
}
std::vector<std::string> EmojiForText(const std::string& text) {
  // Small curated Traditional Chinese defaults; no private/proprietary data.
  static const std::vector<std::pair<std::string, std::string>> entries = {
      {"開心", "😊"}, {"高興", "😊"}, {"哈哈", "😂"}, {"笑死", "🤣"},
      {"難過", "😢"}, {"傷心", "😭"}, {"生氣", "😠"}, {"無言", "😑"},
      {"尷尬", "😅"}, {"害羞", "☺️"}, {"謝謝", "🙏"}, {"感謝", "🙏"},
      {"讚", "👍"}, {"加油", "💪"}, {"恭喜", "🎉"}, {"生日快樂", "🎂"},
      {"愛你", "❤️"}, {"早安", "☀️"}, {"晚安", "🌙"}, {"咖啡", "☕"},
      {"台灣", "🇹🇼"}, {"臺灣", "🇹🇼"}, {"火鍋", "🍲"}};
  std::vector<std::string> result;
  for (const auto& [word, emoji] : entries) {
    if (text.ends_with(word)) result.push_back(text + emoji);
  }
  return result;
}
bool CompletionIndex::load(const std::string& path) {
  buckets_.clear();
  std::ifstream input(path);
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '_') continue;
    std::istringstream stream(line);
    stream.imbue(std::locale::classic());
    Phrase p;
    if (!(stream >> p.reading >> p.value >> p.score) || !std::isfinite(p.score)) continue;
    if (p.value.starts_with("MACRO@")) continue;
    p.syllables = Syllables(p.reading);
    if (p.syllables.empty() || p.syllables.size() > 8) continue;
    const auto chars = Split(p.syllables[0]);
    if (chars.empty() || Scalar(chars[0]) < 0x3105 || Scalar(chars[0]) > 0x3129) continue;
    buckets_[chars[0]].push_back(std::move(p));
  }
  return true;
}
std::vector<Phrase> CompletionIndex::query(const std::string& reading, size_t limit) const {
  auto prefixes = Syllables(reading);
  if (prefixes.empty() || prefixes.size() > 8 || !limit) return {};
  auto chars = Split(prefixes[0]);
  if (chars.empty()) return {};
  auto it = buckets_.find(chars[0]);
  if (it == buckets_.end()) return {};
  std::vector<Phrase> matches;
  for (const auto& phrase : it->second) {
    if (phrase.syllables.size() != prefixes.size()) continue;
    bool match = true;
    for (size_t i = 0; i < prefixes.size(); ++i) {
      if (prefixes[i].empty() || !phrase.syllables[i].starts_with(prefixes[i])) { match = false; break; }
    }
    if (match) matches.push_back(phrase);
  }
  std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
  std::unordered_set<std::string> seen;
  std::vector<Phrase> result;
  for (const auto& p : matches) {
    if (seen.insert(p.value).second) result.push_back(p);
    if (result.size() == limit) break;
  }
  return result;
}
std::vector<std::string> TypoKeys(const std::string& keys) {
  if (keys.empty() || keys.size() > 8) return {};
  const std::vector<std::string> rows = {"1234567890", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./"};
  const std::vector<double> offsets = {0, 0.25, 0.5, 0.75};
  std::unordered_set<std::string> seen;
  std::vector<std::string> result;
  auto add = [&](std::string value) {
    if (!value.empty() && value != keys && seen.insert(value).second) result.push_back(std::move(value));
  };
  for (size_t i = 0; i < keys.size(); ++i) {
    auto v = keys; v.erase(i, 1); add(v);
    if (i + 1 < keys.size()) { v = keys; std::swap(v[i], v[i + 1]); add(v); }
    for (size_t r = 0; r < rows.size(); ++r) {
      auto c = rows[r].find(keys[i]);
      if (c == std::string::npos) continue;
      for (size_t s = 0; s < rows.size(); ++s) {
        if (std::abs(static_cast<int>(s) - static_cast<int>(r)) > 1) continue;
        for (size_t j = 0; j < rows[s].size(); ++j) {
          if (std::abs((j + offsets[s]) - (c + offsets[r])) > 1.05) continue;
          v = keys; v[i] = rows[s][j]; add(v);
        }
      }
    }
  }
  for (size_t i = 0; i <= keys.size(); ++i) {
    for (const auto& row : rows) for (char c : row) {
      auto v = keys; v.insert(i, 1, c); add(v);
    }
  }
  return result;
}
}  // namespace McBopomofo::Plus
