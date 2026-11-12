// SPDX-License-Identifier: MIT
// Emoji shortcode lookup (":smile" style) for McBopomofo Plus.
#ifndef SRC_PLUSEMOJI_H_
#define SRC_PLUSEMOJI_H_

#include <string>
#include <utility>
#include <vector>

namespace McBopomofo::Plus {

// Prefix that opens shortcode input. Either one key ("`"), or the same key
// pressed twice within the double-tap window ("``").
struct EmojiPrefix {
  char key = 0;
  bool doubleTap = false;
  // Parses ";", ";;", "`", "``", ":" etc. Only printable ASCII punctuation is
  // accepted: letters/digits are Bopomofo keys and would break normal typing.
  static EmojiPrefix Parse(const std::string& spec);
  bool valid() const { return key != 0; }
};

class EmojiShortcodes {
 public:
  // File format: "code<TAB>emoji[<TAB>tags]"; '#' comments. Later loads
  // override earlier ones, so load built-in first, then the user file.
  // Returns the number of entries read.
  size_t load(const std::string& path, bool userFile = false);
  void clear() { entries_.clear(); }
  size_t size() const { return entries_.size(); }

  struct Match {
    std::string code;
    std::string emoji;
  };
  // Empty query returns the user's own entries (quick access).
  // Ranking: user entries first, then exact match, then code prefix, then
  // tag prefix. Deterministic; deduplicated by emoji.
  std::vector<Match> search(const std::string& query, size_t limit = 9) const;

 private:
  struct Entry {
    std::string code;
    std::string emoji;
    std::vector<std::string> tags;
    bool user;
  };
  std::vector<Entry> entries_;
};

}  // namespace McBopomofo::Plus
#endif
