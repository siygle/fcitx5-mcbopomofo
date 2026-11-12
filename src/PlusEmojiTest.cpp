// SPDX-License-Identifier: MIT
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "PlusEmoji.h"
#include "gtest/gtest.h"

namespace McBopomofo::Plus {

constexpr char kBuiltIn[] = "mcbopomofo-plus-emoji-shortcodes.txt";

static std::string WriteTemp(const std::string& content) {
  char path[] = "/tmp/plus-emoji-XXXXXX";
  int fd = mkstemp(path);
  EXPECT_GE(fd, 0);
  close(fd);
  std::ofstream(path) << content;
  return path;
}

TEST(PlusEmojiPrefix, ParsesSingleAndDoubleTap) {
  auto p = EmojiPrefix::Parse(";");
  EXPECT_TRUE(p.valid());
  EXPECT_EQ(p.key, ';');
  EXPECT_FALSE(p.doubleTap);
  p = EmojiPrefix::Parse("``");
  EXPECT_EQ(p.key, '`');
  EXPECT_TRUE(p.doubleTap);
}

TEST(PlusEmojiPrefix, RejectsBopomofoKeysAndBadSpecs) {
  // Letters and digits are Bopomofo keys on every layout; never allow them.
  for (auto s : {"", "a", "aa", "1", "11", ";:", ";;;", " ", "\t", "é"}) {
    EXPECT_FALSE(EmojiPrefix::Parse(s).valid()) << s;
  }
}

TEST(PlusEmojiShortcodes, BuiltInExactAndPrefix) {
  EmojiShortcodes db;
  ASSERT_GT(db.load(kBuiltIn), 1000u);
  auto r = db.search("thumbsup");
  ASSERT_FALSE(r.empty());
  EXPECT_EQ(r[0].emoji, "👍");
  r = db.search("smi");
  ASSERT_FALSE(r.empty());
  EXPECT_EQ(r[0].code, "smile");  // Shortest prefix match first.
  EXPECT_LE(db.search("s", 5).size(), 5u);
}

TEST(PlusEmojiShortcodes, TagsMatchAfterCodes) {
  EmojiShortcodes db;
  ASSERT_GT(db.load(kBuiltIn), 0u);
  auto r = db.search("launch");  // Tag of 🚀 "rocket".
  bool found = false;
  for (auto& m : r) found |= m.emoji == "🚀";
  EXPECT_TRUE(found);
}

TEST(PlusEmojiShortcodes, UserFileOverridesAndRanksFirst) {
  EmojiShortcodes db;
  db.load(kBuiltIn);
  auto user = WriteTemp(
      "# my shortcodes\n"
      "ok 👌\n"
      "smile\t🙂\n"     // Overrides built-in 😄.
      "tw 🇹🇼 taiwan\n"
      "BAD CODE\n"      // Invalid: not lowercase-able into a code? 'bad' ok, 'CODE' emoji.
      "has space 😀\n"
      "x\n");           // Missing emoji.
  db.load(user, true);
  std::remove(user.c_str());
  EXPECT_EQ(db.search("smile")[0].emoji, "🙂");
  EXPECT_EQ(db.search("tw")[0].emoji, "🇹🇼");
  EXPECT_EQ(db.search("taiwan")[0].emoji, "🇹🇼");
  // Empty query lists the user's own entries for quick access.
  auto mine = db.search("");
  ASSERT_GE(mine.size(), 3u);
  EXPECT_EQ(mine[0].code, "ok");
}

TEST(PlusEmojiShortcodes, RejectsUnsafeQueriesAndMissingFiles) {
  EmojiShortcodes db;
  EXPECT_EQ(db.load("/nonexistent/x.txt"), 0u);
  db.load(kBuiltIn);
  for (auto q : {"SMILE", "a b", "../x", ":", std::string(41, 'a').c_str()}) {
    EXPECT_TRUE(db.search(q).empty()) << q;
  }
  EXPECT_TRUE(db.search("zzzzzz-no-such").empty());
  EXPECT_TRUE(db.search("smile", 0).empty());
}

TEST(PlusEmojiShortcodes, BoundedLineLength) {
  EmojiShortcodes db;
  auto path = WriteTemp(std::string(5000, 'a') + " 😀\nok 👌\n");
  EXPECT_EQ(db.load(path, true), 1u);
  std::remove(path.c_str());
}

}  // namespace McBopomofo::Plus
