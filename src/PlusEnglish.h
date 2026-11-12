// SPDX-License-Identifier: MIT
// English buffer mode lexicon for McBopomofo Plus.
#ifndef SRC_PLUSENGLISH_H_
#define SRC_PLUSENGLISH_H_

#include <string>
#include <unordered_map>
#include <vector>

namespace McBopomofo::Plus {

// Case-insensitive word lookup with canonical casing (github -> GitHub).
// Built-in words load first; the user file loads second and wins.
class EnglishLexicon {
 public:
  // One word per line, '#' comments. Returns the number of words read.
  size_t load(const std::string& path, bool userFile = false);
  void clear();
  size_t size() const { return words_.size(); }

  // Candidates for what the user typed, in order:
  //  1. user-defined casing of the exact word,
  //  2. the text exactly as typed,
  //  3. other dictionary casings of the exact word (e.g. GitHub, Apple),
  //  4. completions: user words first, then built-in, shortest first.
  // Always non-empty for non-empty input; never contains duplicates.
  std::vector<std::string> candidates(const std::string& typed,
                                      size_t limit = 9) const;

  // The canonical form to commit with Space: user casing, else the single
  // dictionary casing when the user typed all-lowercase, else as typed.
  std::string canonical(const std::string& typed) const;

 private:
  struct Word {
    std::string text;
    bool user;
  };
  // lowercase -> casings in load order (user entries moved first).
  std::unordered_map<std::string, std::vector<Word>> byLower_;
  // All distinct lowercase keys, sorted, for prefix completion.
  mutable std::vector<std::string> sortedKeys_;
  mutable bool sorted_ = true;
  std::vector<std::string> words_;
};

}  // namespace McBopomofo::Plus
#endif
