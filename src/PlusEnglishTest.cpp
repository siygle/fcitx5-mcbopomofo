// SPDX-License-Identifier: MIT
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "PlusEnglish.h"
#include "gtest/gtest.h"

namespace McBopomofo::Plus {

constexpr char kBuiltIn[] = "mcbopomofo-plus-english.txt";

static std::string WriteTemp(const std::string& content) {
  char path[] = "/tmp/plus-english-XXXXXX";
  int fd = mkstemp(path);
  EXPECT_GE(fd, 0);
  close(fd);
  std::ofstream(path) << content;
  return path;
}

TEST(PlusEnglish, CanonicalCasingFromBuiltIn) {
  EnglishLexicon lex;
  ASSERT_GT(lex.load(kBuiltIn), 50000u);
  EXPECT_EQ(lex.canonical("github"), "GitHub");
  EXPECT_EQ(lex.canonical("iphone"), "iPhone");
  EXPECT_EQ(lex.canonical("javascript"), "JavaScript");
  EXPECT_EQ(lex.canonical("linux"), "Linux");
  EXPECT_EQ(lex.canonical("hello"), "hello");
}

TEST(PlusEnglish, NeverOverridesExplicitOrAmbiguousCasing) {
  EnglishLexicon lex;
  lex.load(kBuiltIn);
  EXPECT_EQ(lex.canonical("GITHUB"), "GITHUB");  // User chose a casing.
  EXPECT_EQ(lex.canonical("Hello"), "Hello");
  EXPECT_EQ(lex.canonical("apple"), "apple");  // "apple" and "Apple" both exist.
  EXPECT_EQ(lex.canonical("xyzzyqq"), "xyzzyqq");  // Unknown: as typed.
}

TEST(PlusEnglish, CandidatesOrderAndCompletion) {
  EnglishLexicon lex;
  lex.load(kBuiltIn);
  auto c = lex.candidates("github");
  ASSERT_GE(c.size(), 2u);
  EXPECT_EQ(c[0], "GitHub");
  EXPECT_EQ(c[1], "github");  // As typed is always offered.
  c = lex.candidates("progr", 5);
  ASSERT_FALSE(c.empty());
  EXPECT_EQ(c[0], "progr");
  EXPECT_LE(c.size(), 5u);
  bool hasProgram = false;
  for (auto& w : c) hasProgram |= w == "program";
  EXPECT_TRUE(hasProgram);
  EXPECT_TRUE(lex.candidates("").empty());
}

TEST(PlusEnglish, UserWordsWinAndComeFirst) {
  EnglishLexicon lex;
  lex.load(kBuiltIn);
  auto user = WriteTemp("# mine\nOpenVanilla\n  McBopomofo  \nGITHUB\n\nbad word\n");
  EXPECT_EQ(lex.load(user, true), 3u);  // "bad word" has a space: rejected.
  std::remove(user.c_str());
  EXPECT_EQ(lex.canonical("openvanilla"), "OpenVanilla");
  EXPECT_EQ(lex.canonical("mcbopomofo"), "McBopomofo");
  EXPECT_EQ(lex.canonical("github"), "GITHUB");  // User casing overrides GitHub.
  auto c = lex.candidates("mcb");
  ASSERT_GE(c.size(), 2u);
  EXPECT_EQ(c[1], "McBopomofo");  // First completion is the user word.
}

TEST(PlusEnglish, BoundedAndMissingFiles) {
  EnglishLexicon lex;
  EXPECT_EQ(lex.load("/nonexistent/x"), 0u);
  auto path = WriteTemp(std::string(100, 'a') + "\nok\n");
  EXPECT_EQ(lex.load(path), 1u);
  std::remove(path.c_str());
  EXPECT_EQ(lex.candidates(std::string(41, 'a')).size(), 1u);
}

}  // namespace McBopomofo::Plus
