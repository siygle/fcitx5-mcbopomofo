// SPDX-License-Identifier: MIT
#include "PlusEnglish.h"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace McBopomofo::Plus {
namespace {
constexpr size_t kMaxWord = 40;
constexpr size_t kMaxWords = 300000;

std::string Lower(const std::string& s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return r;
}

// Printable ASCII without spaces; this mode only commits what was typed.
bool ValidWord(const std::string& s) {
  if (s.empty() || s.size() > kMaxWord) return false;
  return std::all_of(s.begin(), s.end(),
                     [](unsigned char c) { return c > 0x20 && c < 0x7f; });
}

bool AllLower(const std::string& s) {
  return std::none_of(s.begin(), s.end(),
                      [](unsigned char c) { return std::isupper(c); });
}
}  // namespace

void EnglishLexicon::clear() {
  byLower_.clear();
  sortedKeys_.clear();
  words_.clear();
  sorted_ = true;
}

size_t EnglishLexicon::load(const std::string& path, bool userFile) {
  std::ifstream in(path);
  if (!in) return 0;
  std::string line;
  size_t n = 0;
  while (words_.size() < kMaxWords && std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // Trim surrounding whitespace for hand-edited user files.
    auto b = line.find_first_not_of(" \t");
    if (b == std::string::npos || line[b] == '#') continue;
    auto e = line.find_last_not_of(" \t");
    std::string w = line.substr(b, e - b + 1);
    if (!ValidWord(w)) continue;
    auto key = Lower(w);
    auto& casings = byLower_[key];
    if (casings.empty()) {
      sortedKeys_.push_back(key);
      sorted_ = false;
    }
    auto it = std::find_if(casings.begin(), casings.end(),
                           [&](const Word& x) { return x.text == w; });
    if (it != casings.end()) {
      if (userFile && !it->user) {  // Promote existing casing to user.
        Word promoted{w, true};
        casings.erase(it);
        casings.insert(casings.begin(), promoted);
      }
    } else if (userFile) {
      casings.insert(casings.begin(), Word{w, true});
    } else {
      casings.push_back(Word{w, false});
    }
    words_.push_back(w);
    ++n;
  }
  return n;
}

std::string EnglishLexicon::canonical(const std::string& typed) const {
  if (typed.empty()) return typed;
  auto it = byLower_.find(Lower(typed));
  if (it == byLower_.end()) return typed;
  const auto& casings = it->second;
  if (casings.front().user) return casings.front().text;
  // Only auto-correct when the user did not choose a casing themselves.
  if (!AllLower(typed)) return typed;
  // "apple"/"Apple" both exist: lowercase input is already correct.
  for (const auto& w : casings) {
    if (w.text == typed) return typed;
  }
  return casings.size() == 1 ? casings.front().text : typed;
}

std::vector<std::string> EnglishLexicon::candidates(const std::string& typed,
                                                    size_t limit) const {
  std::vector<std::string> out;
  if (typed.empty() || !limit) return out;
  std::unordered_set<std::string> seen;
  auto add = [&](const std::string& s) {
    if (out.size() < limit && seen.insert(s).second) out.push_back(s);
  };
  add(canonical(typed));
  add(typed);
  const auto key = Lower(typed);
  if (auto it = byLower_.find(key); it != byLower_.end()) {
    for (const auto& w : it->second) add(w.text);
  }
  if (out.size() >= limit) return out;

  if (!sorted_) {
    std::sort(sortedKeys_.begin(), sortedKeys_.end());
    sorted_ = true;
  }
  // Completions (longer words with this prefix), user words first.
  std::vector<const Word*> user, builtIn;
  for (auto it = std::lower_bound(sortedKeys_.begin(), sortedKeys_.end(), key);
       it != sortedKeys_.end() && it->starts_with(key); ++it) {
    if (*it == key) continue;
    const auto& w = byLower_.at(*it).front();
    (w.user ? user : builtIn).push_back(&w);
  }
  auto byLength = [](const Word* a, const Word* b) {
    if (a->text.size() != b->text.size()) return a->text.size() < b->text.size();
    return a->text < b->text;
  };
  std::stable_sort(user.begin(), user.end(), byLength);
  std::stable_sort(builtIn.begin(), builtIn.end(), byLength);
  for (auto* w : user) add(w->text);
  for (auto* w : builtIn) add(w->text);
  return out;
}

}  // namespace McBopomofo::Plus
