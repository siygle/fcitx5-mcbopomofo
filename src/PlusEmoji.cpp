// SPDX-License-Identifier: MIT
#include "PlusEmoji.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace McBopomofo::Plus {
namespace {
constexpr size_t kMaxLine = 1024;
constexpr size_t kMaxEntries = 20000;
constexpr size_t kMaxCode = 40;
bool CodeChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
         c == '+' || c == '-';
}
bool ValidCode(const std::string& s) {
  return !s.empty() && s.size() <= kMaxCode &&
         std::all_of(s.begin(), s.end(), CodeChar);
}
}  // namespace

EmojiPrefix EmojiPrefix::Parse(const std::string& spec) {
  EmojiPrefix p;
  if (spec.size() != 1 && spec.size() != 2) return p;
  if (spec.size() == 2 && spec[0] != spec[1]) return p;
  char c = spec[0];
  bool punct = (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
               (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
  if (!punct) return p;
  p.key = c;
  p.doubleTap = spec.size() == 2;
  return p;
}

size_t EmojiShortcodes::load(const std::string& path, bool userFile) {
  std::ifstream in(path);
  if (!in) return 0;
  std::unordered_map<std::string, size_t> index;
  for (size_t i = 0; i < entries_.size(); ++i) index[entries_[i].code] = i;
  std::string line;
  size_t n = 0;
  while (entries_.size() < kMaxEntries && std::getline(in, line)) {
    if (line.size() > kMaxLine || line.empty() || line[0] == '#') continue;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // Accept TAB or whitespace separators for hand-edited user files.
    std::istringstream fields(line);
    Entry e;
    if (!(fields >> e.code >> e.emoji)) continue;
    std::transform(e.code.begin(), e.code.end(), e.code.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (!ValidCode(e.code) || e.emoji.size() > 64) continue;
    std::string tag;
    while (fields >> tag && e.tags.size() < 16) {
      if (ValidCode(tag)) e.tags.push_back(tag);
    }
    e.user = userFile;
    if (auto it = index.find(e.code); it != index.end()) {
      entries_[it->second] = std::move(e);
    } else {
      index[e.code] = entries_.size();
      entries_.push_back(std::move(e));
    }
    ++n;
  }
  return n;
}

std::vector<EmojiShortcodes::Match> EmojiShortcodes::search(
    const std::string& query, size_t limit) const {
  std::vector<Match> out;
  if (!limit) return out;
  if (query.empty()) {  // Quick access: the user's own shortcodes.
    for (const auto& e : entries_) {
      if (!e.user) continue;
      out.push_back({e.code, e.emoji});
      if (out.size() == limit) break;
    }
    return out;
  }
  if (!ValidCode(query)) return out;
  // tier: 0 user exact, 1 exact, 2 user prefix, 3 code prefix, 4 tag prefix
  std::vector<std::pair<int, const Entry*>> ranked;
  for (const auto& e : entries_) {
    int tier = -1;
    if (e.code == query) tier = e.user ? 0 : 1;
    else if (e.code.starts_with(query)) tier = e.user ? 2 : 3;
    else if (std::any_of(e.tags.begin(), e.tags.end(),
                         [&](const auto& t) { return t.starts_with(query); }))
      tier = 4;
    if (tier >= 0) ranked.emplace_back(tier, &e);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first < b.first;
    if (a.second->code.size() != b.second->code.size())
      return a.second->code.size() < b.second->code.size();
    return a.second->code < b.second->code;
  });
  std::unordered_set<std::string> seen;
  for (const auto& [tier, e] : ranked) {
    if (!seen.insert(e->emoji).second) continue;
    out.push_back({e->code, e->emoji});
    if (out.size() == limit) break;
  }
  return out;
}

}  // namespace McBopomofo::Plus
