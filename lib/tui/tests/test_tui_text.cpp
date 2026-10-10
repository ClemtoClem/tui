#include <gtest/gtest.h>
#include <tui/core/Text.hpp>

using namespace tui;

TEST(Utf8, RoundTripAscii) {
    std::string s = "Hello, world!";
    auto decoded = TextHelper::decode_utf8(s);
    EXPECT_EQ(TextHelper::encode_utf8(decoded), s);
    EXPECT_EQ(decoded.size(), s.size());
}

TEST(Utf8, RoundTripAccents) {
    std::string s = "héllo wörld café";
    auto decoded = TextHelper::decode_utf8(s);
    EXPECT_EQ(TextHelper::encode_utf8(decoded), s);
}

TEST(Utf8, RoundTripCjk) {
    std::string s = "中文测试"; // 4 codepoints CJK, 3 bytes chacun en UTF-8
    auto decoded = TextHelper::decode_utf8(s);
    EXPECT_EQ(decoded.size(), 4u);
    EXPECT_EQ(TextHelper::encode_utf8(decoded), s);
}

TEST(Utf8, RoundTripFourByteEmoji) {
    std::string s = "hi \xF0\x9F\x98\x80 bye"; // U+1F600 (emoji, 4 octets UTF-8)
    auto decoded = TextHelper::decode_utf8(s);
    EXPECT_EQ(TextHelper::encode_utf8(decoded), s);
}

TEST(Utf8, InvalidContinuationReplaced) {
    std::string s = "\xC0\x80"; // overlong / invalide
    auto decoded = TextHelper::decode_utf8(s);
    ASSERT_FALSE(decoded.empty());
    EXPECT_EQ(decoded[0], char32_t(0xFFFD));
}

TEST(Utf8, EmptyString) {
    auto decoded = TextHelper::decode_utf8("");
    EXPECT_TRUE(decoded.empty());
    EXPECT_EQ(TextHelper::encode_utf8(decoded), "");
}

TEST(Width, AsciiIsOne) {
    EXPECT_EQ(TextHelper::codepoint_width(U'a'), 1);
    EXPECT_EQ(TextHelper::codepoint_width(U'Z'), 1);
    EXPECT_EQ(TextHelper::codepoint_width(U' '), 1);
}

TEST(Width, ControlAndNullAreZero) {
    EXPECT_EQ(TextHelper::codepoint_width(0), 0);
    EXPECT_EQ(TextHelper::codepoint_width(U'\t'), 0);
    EXPECT_EQ(TextHelper::codepoint_width(0x7F), 0);
}

TEST(Width, CombiningMarkIsZero) {
    EXPECT_EQ(TextHelper::codepoint_width(0x0301), 0); // combining acute accent
}

TEST(Width, CjkIsTwo) {
    EXPECT_EQ(TextHelper::codepoint_width(0x4E2D), 2); // 中
}

TEST(Width, DisplayWidthSumsCodepoints) {
    auto decoded = TextHelper::decode_utf8("ab中");
    EXPECT_EQ(TextHelper::display_width(decoded), 4); // 1+1+2
}

TEST(Truncate, NoTruncationNeeded) {
    auto out = TextHelper::truncate_to_width(U"short", 10);
    EXPECT_EQ(out, U"short");
}

TEST(Truncate, ExactFit) {
    auto out = TextHelper::truncate_to_width(U"exact", 5);
    EXPECT_EQ(out, U"exact");
}

TEST(Truncate, TruncatesWithEllipsis) {
    auto out = TextHelper::truncate_to_width(U"hello world", 8, U"...");
    EXPECT_LE(TextHelper::display_width(out), 8);
    EXPECT_EQ(out, U"hello...");
}

TEST(Truncate, EmptyString) {
    auto out = TextHelper::truncate_to_width(U"", 5);
    EXPECT_EQ(out, U"");
}

TEST(Truncate, ZeroWidthBudget) {
    auto out = TextHelper::truncate_to_width(U"anything", 0);
    EXPECT_EQ(out, U"");
}

TEST(Truncate, NeverSplitsWideGlyph) {
    // "中" (largeur 2) suivi d'un "a" ; budget de 2 colonnes ne doit
    // jamais inclure la moitié d'un glyphe large.
    std::u32string s;
    s.push_back(0x4E2D);
    s.push_back(U'a');
    auto out = TextHelper::truncate_to_width(s, 1);
    EXPECT_TRUE(out.empty()); // le glyphe large (2) ne tient pas dans 1 colonne
}

TEST(Wrap, ShortLineFitsOnOne) {
    auto lines = TextHelper::wrap_to_width(U"short line", 80);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], U"short line");
}

TEST(Wrap, GreedyWordWrap) {
    auto lines = TextHelper::wrap_to_width(U"the quick brown fox jumps", 10);
    for (auto& l : lines) EXPECT_LE(TextHelper::display_width(l), 10);
    // Reconstitue le texte (les espaces de coupure sont normalisés à un seul)
    std::u32string joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) joined.push_back(U' ');
        joined += lines[i];
    }
    EXPECT_EQ(joined, U"the quick brown fox jumps");
}

TEST(Wrap, SingleWordLongerThanWidthForceBreaks) {
    auto lines = TextHelper::wrap_to_width(U"supercalifragilistic", 5);
    for (auto& l : lines) EXPECT_LE(TextHelper::display_width(l), 5);
    EXPECT_GT(lines.size(), 1u);
}

TEST(Wrap, EmptyString) {
    auto lines = TextHelper::wrap_to_width(U"", 10);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(lines[0].empty());
}
