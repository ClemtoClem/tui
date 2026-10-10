#include <gtest/gtest.h>
#include <tui/core/SyntaxHighlight.hpp>

using namespace tui;

namespace {
const LanguageSpec& cpp_spec() { return language_specs().at("cpp"); }
} // namespace

TEST(SyntaxHighlight, DetectsLanguageByExtension) {
    EXPECT_EQ(detect_language("main.cpp"), "cpp");
    EXPECT_EQ(detect_language("script.sh"), "sh");
    EXPECT_EQ(detect_language("data.json"), "json");
    EXPECT_EQ(detect_language("config.yaml"), "yaml");
    EXPECT_EQ(detect_language("Makefile"), "make");
    EXPECT_EQ(detect_language("noext"), "");
}

TEST(SyntaxHighlight, TokenizesKeywordStringCommentAndNumber) {
    bool in_comment = false;
    auto tokens = tokenize_line(U"if (x == 42) { s = \"hi\"; } // note", cpp_spec(), in_comment);

    bool has_keyword = false, has_string = false, has_number = false, has_comment = false;
    for (const auto& t : tokens) {
        if (t.role == TokenRole::Keyword) has_keyword = true;
        if (t.role == TokenRole::String) has_string = true;
        if (t.role == TokenRole::Number) has_number = true;
        if (t.role == TokenRole::Comment) has_comment = true;
    }
    EXPECT_TRUE(has_keyword);
    EXPECT_TRUE(has_string);
    EXPECT_TRUE(has_number);
    EXPECT_TRUE(has_comment);
}

TEST(SyntaxHighlight, HashIsPreprocessorNotComment) {
    bool in_comment = false;
    auto tokens = tokenize_line(U"#include <foo>", cpp_spec(), in_comment);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].role, TokenRole::Preprocessor);
}

TEST(SyntaxHighlight, BlockCommentSpansMultipleLinesViaThreadedState) {
    bool in_comment = false;
    auto first = tokenize_line(U"/* start", cpp_spec(), in_comment);
    EXPECT_TRUE(in_comment);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].role, TokenRole::Comment);

    auto second = tokenize_line(U"still inside */ return x;", cpp_spec(), in_comment);
    EXPECT_FALSE(in_comment);
    bool has_keyword = false;
    for (const auto& t : second) if (t.role == TokenRole::Keyword) has_keyword = true;
    EXPECT_TRUE(has_keyword);
}

TEST(SyntaxHighlight, ShellVariableSigilProducesVariableToken) {
    bool in_comment = false;
    auto tokens = tokenize_line(U"echo $HOME ${PATH}", language_specs().at("sh"), in_comment);
    int variable_count = 0;
    for (const auto& t : tokens) if (t.role == TokenRole::Variable) ++variable_count;
    EXPECT_EQ(variable_count, 2);
}

TEST(SyntaxHighlight, UnknownLanguageIsAbsentFromTable) {
    EXPECT_EQ(language_specs().find("no-such-language"), language_specs().end());
}

TEST(SyntaxHighlight, SyntaxThemeReturnsConfiguredColor) {
    SyntaxTheme theme;
    theme.set_color(TokenRole::Keyword, Color{1, 2, 3});
    EXPECT_EQ(theme.color(TokenRole::Keyword), (Color{1, 2, 3}));
}
