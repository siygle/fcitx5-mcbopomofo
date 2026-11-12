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

struct Phrase {
  std::string reading;
  std::string value;
  double score;
  std::vector<std::string> syllables;
};
class CompletionIndex {
 public:
  bool load(const std::string& path);
  // Hyphen-delimited syllable prefixes; tones, if supplied, are significant.
  std::vector<Phrase> query(const std::string& reading, size_t limit = 30) const;
 private:
  std::unordered_map<std::string, std::vector<Phrase>> buckets_;
};
// Single-edit QWERTY substitutions, transpositions, insertions and deletions.
// Callers must parse using the active Bopomofo layout and validate against LM.
std::vector<std::string> TypoKeys(const std::string& keys);
}  // namespace McBopomofo::Plus
#endif
