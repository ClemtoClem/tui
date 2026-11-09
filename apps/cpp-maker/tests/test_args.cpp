/* @file tests/test_args.cpp — analyse des arguments. */

#include "io/args.hpp"

#include <gtest/gtest.h>

namespace {
args::Arguments make() {
    args::Arguments a;
    a.add("--jobs").set_value(0).flags("-j");
    a.add("--verbose").set_value(false).flags("-v");
    a.add("--keep-going").set_value(false).flags("-k");
    a.add("--mode").set_value(std::string("debug"));
    a.add("--sanitize").set_value(std::vector<std::string>{});
    return a;
}
} // namespace

TEST(Args, PositionnelsPartoutEtGrappes) {
    auto a = make();
    a.parse({"build", "-vkj8", "release", "--mode=asan", "core.log"});
    a.set_dispatch_depth(1);
    EXPECT_EQ(a.get<int>("--jobs"), 8);
    EXPECT_TRUE(a.get<bool>("--verbose"));
    EXPECT_TRUE(a.get<bool>("--keep-going"));
    EXPECT_EQ(a.get<std::string>("--mode"), "asan");
    EXPECT_EQ(a.positionals(), (std::vector<std::string>{"release", "core.log"}));
}

TEST(Args, ListesRepetablesEtDoubleTiret) {
    auto a = make();
    a.parse({"run", "--sanitize", "address,undefined", "--sanitize=leak", "--", "-x", "--port", "80"});
    EXPECT_EQ(a.get<std::vector<std::string>>("--sanitize"), (std::vector<std::string>{"address", "undefined", "leak"}));
    EXPECT_TRUE(a.has_double_dash());
    EXPECT_EQ(a.trailing(), (std::vector<std::string>{"-x", "--port", "80"}));
}

TEST(Args, Erreurs) {
    {
        auto a = make();
        try {
            a.parse({"--jbos", "2"});
            FAIL();
        } catch (const args::Exception& e) {
            EXPECT_EQ(e.code(), args::ErrorCode::UnknownOption);
            EXPECT_NE(std::string(e.what()).find("--jobs"), std::string::npos);   // suggestion
        }
    }
    {
        auto a = make();
        EXPECT_THROW(a.parse({"-j", "abc"}), args::Exception);
    }
    {
        auto a = make();
        EXPECT_THROW(a.parse({"--mode"}), args::Exception);
    }
    {
        auto a = make();
        EXPECT_THROW(a.parse({"--mode", "a", "--mode", "b"}), args::Exception);
    }
}

TEST(Args, AideGeneree) {
    auto a = make();
    auto h = a.help();
    EXPECT_NE(h.find("-j, --jobs"), std::string::npos);
    EXPECT_NE(h.find("--sanitize"), std::string::npos);
}
