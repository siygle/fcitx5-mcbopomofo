// SPDX-License-Identifier: MIT
#include "PlusFeatures.h"
#include "gtest/gtest.h"
#include <algorithm>

namespace McBopomofo::Plus {
TEST(PlusCalculator, PrecedenceAndDecimals) {
  EXPECT_EQ(Calculate("1+2*3"), "7");
  EXPECT_EQ(Calculate("(1+2)*3"), "9");
  EXPECT_EQ(Calculate("-2 * -.5"), "1");
  EXPECT_EQ(Calculate("7%3"), "1");
  EXPECT_EQ(Calculate("-0"), "0");
}
TEST(PlusCalculator, RejectsUnsafeAndIncompleteInput) {
  for (auto input : {"", "1/0", "1%0", "1+", "(2", "1..2", "nan", "system(1)", "1;ls", "2**3"}) EXPECT_FALSE(Calculate(input));
  EXPECT_FALSE(Calculate(std::string(300, '1')));
  EXPECT_FALSE(Calculate(std::string(100, '(') + "1" + std::string(100, ')')));
}
TEST(PlusSpacing, ChineseBoundariesOnly) {
  EXPECT_EQ(AutoSpace("你好Linux第5章", true, true), "你好 Linux 第 5 章");
  EXPECT_EQ(AutoSpace("你好Linux第5章", true, false), "你好 Linux 第5章");
  EXPECT_EQ(AutoSpace("你好 Linux！🙂123abc", true, true), "你好 Linux！🙂123abc");
  EXPECT_EQ(AutoSpace("𠀀A", true, false), "𠀀 A");
  EXPECT_EQ(AutoSpace("你好Linux", false, false), "你好Linux");
}
TEST(PlusEmoji, TraditionalChineseSuffix) {
  EXPECT_EQ(EmojiForText("今天很開心"), std::vector<std::string>{"今天很開心😊"});
  EXPECT_TRUE(EmojiForText("一般文字").empty());
}
TEST(PlusCompletion, FullAndInitialPrefixes) {
  CompletionIndex index;
  ASSERT_TRUE(index.load("mcbopomofo-test-data.txt"));
  auto initial = index.query("ㄊ-ㄅ");
  EXPECT_TRUE(std::any_of(initial.begin(), initial.end(), [](const auto& p) { return p.value == "台北"; }));
  auto partial = index.query("ㄊㄞˊ-ㄅ");
  EXPECT_TRUE(std::any_of(partial.begin(), partial.end(), [](const auto& p) { return p.value == "台北"; }));
  EXPECT_TRUE(index.query("").empty());
  EXPECT_TRUE(index.query("ㄊ-ㄅ", 0).empty());
  EXPECT_LE(index.query("ㄊ-ㄅ", 2).size(), 2u);
  ASSERT_FALSE(index.load("/nonexistent/data.txt"));
  EXPECT_TRUE(index.query("ㄊ-ㄅ").empty());
}
TEST(PlusTypo, SingleEditsBoundedAndDeterministic) {
  auto keys = TypoKeys("su3");
  for (auto expected : {"us3", "s3", "su", "sju3", "su4"}) {
    EXPECT_NE(std::find(keys.begin(), keys.end(), expected), keys.end()) << expected;
  }
  EXPECT_EQ(keys, TypoKeys("su3"));
  EXPECT_TRUE(TypoKeys("").empty());
  EXPECT_TRUE(TypoKeys("123456789").empty());
}
}  // namespace McBopomofo::Plus
