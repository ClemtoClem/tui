/* @file build/TestSupport.cpp */

#include "build/TestSupport.hpp"

#include "core/Util.hpp"

namespace build {

std::string_view test_header_source() {
    return R"TPL(#pragma once
// Framework de tests intégré à cpp-maker (généré, ne pas modifier).
//
//   TEST(nom)          { CHECK(x); CHECK_EQ(a, b); REQUIRE(p != nullptr); }
//   TEST(Suite, nom)   { EXPECT_TRUE(x); EXPECT_EQ(a, b); ASSERT_TRUE(p); }
//
// CHECK_* / EXPECT_* poursuivent le test après un échec ; REQUIRE_* /
// ASSERT_* l'interrompent. SKIP("raison") marque le test comme ignoré : si
// tous les tests exécutés sont ignorés, l'exécutable sort avec le code 77.
// Options de l'exécutable : --filter <sous-chaîne>, --list.
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cppmaker_test {

struct Case { std::string name; std::function<void()> fn; };

inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline bool& skipped() { static bool s = false; return s; }

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

template <class T, class = void> struct printable : std::false_type {};
template <class T>
struct printable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>> : std::true_type {};

template <class T> std::string show(const T& v) {
    if constexpr (printable<T>::value) { std::ostringstream s; s << v; return s.str(); }
    else return "?";
}

inline void report(const char* kind, const std::string& what, const char* file, int line) {
    std::fprintf(stderr, "\n    %s:%d: %s échoué : %s", file, line, kind, what.c_str());
    ++failures();
}

template <class A, class B, class Op>
bool compare(const char* kind, const A& a, const B& b, Op op, const char* ea, const char* eb,
             const char* sym, const char* file, int line) {
    if (op(a, b)) return true;
    report(kind, std::string(ea) + " " + sym + " " + eb + "  [" + show(a) + " vs " + show(b) + "]", file, line);
    return false;
}

} // namespace cppmaker_test

#define CPPMAKER_CAT_(a, b) a##b
#define CPPMAKER_CAT(a, b) CPPMAKER_CAT_(a, b)
#define CPPMAKER_TEST_1(name) \
    static void CPPMAKER_CAT(cppmaker_test_fn_, name)(); \
    static ::cppmaker_test::Registrar CPPMAKER_CAT(cppmaker_test_reg_, name)(#name, CPPMAKER_CAT(cppmaker_test_fn_, name)); \
    static void CPPMAKER_CAT(cppmaker_test_fn_, name)()
#define CPPMAKER_TEST_2(suite, name) \
    static void cppmaker_test_fn_##suite##_##name(); \
    static ::cppmaker_test::Registrar cppmaker_test_reg_##suite##_##name(#suite "." #name, cppmaker_test_fn_##suite##_##name); \
    static void cppmaker_test_fn_##suite##_##name()
#define CPPMAKER_PICK(_1, _2, M, ...) M
#define TEST(...) CPPMAKER_PICK(__VA_ARGS__, CPPMAKER_TEST_2, CPPMAKER_TEST_1, unused)(__VA_ARGS__)

#define CPPMAKER_CMP(kind, a, b, op, sym) \
    ::cppmaker_test::compare(kind, (a), (b), [](const auto& x, const auto& y) { return x op y; }, #a, #b, sym, __FILE__, __LINE__)

#define CHECK(c) do { if (!(c)) ::cppmaker_test::report("CHECK", #c, __FILE__, __LINE__); } while (0)
#define CHECK_FALSE(c) CHECK(!(c))
#define CHECK_EQ(a, b) do { CPPMAKER_CMP("CHECK_EQ", a, b, ==, "=="); } while (0)
#define CHECK_NE(a, b) do { CPPMAKER_CMP("CHECK_NE", a, b, !=, "!="); } while (0)
#define CHECK_LT(a, b) do { CPPMAKER_CMP("CHECK_LT", a, b, <, "<"); } while (0)
#define CHECK_LE(a, b) do { CPPMAKER_CMP("CHECK_LE", a, b, <=, "<="); } while (0)
#define CHECK_GT(a, b) do { CPPMAKER_CMP("CHECK_GT", a, b, >, ">"); } while (0)
#define CHECK_GE(a, b) do { CPPMAKER_CMP("CHECK_GE", a, b, >=, ">="); } while (0)
#define CHECK_NEAR(a, b, eps) do { \
    if (!(std::fabs(double(a) - double(b)) <= double(eps))) \
        ::cppmaker_test::report("CHECK_NEAR", #a " ~ " #b, __FILE__, __LINE__); } while (0)
#define CHECK_THROWS(expr) do { bool t_ = false; try { (void)(expr); } catch (...) { t_ = true; } \
    if (!t_) ::cppmaker_test::report("CHECK_THROWS", #expr, __FILE__, __LINE__); } while (0)
#define CHECK_NOTHROW(expr) do { try { (void)(expr); } catch (...) { \
    ::cppmaker_test::report("CHECK_NOTHROW", #expr, __FILE__, __LINE__); } } while (0)

#define REQUIRE(c) do { if (!(c)) { ::cppmaker_test::report("REQUIRE", #c, __FILE__, __LINE__); return; } } while (0)
#define REQUIRE_FALSE(c) REQUIRE(!(c))
#define REQUIRE_EQ(a, b) do { if (!CPPMAKER_CMP("REQUIRE_EQ", a, b, ==, "==")) return; } while (0)
#define REQUIRE_NE(a, b) do { if (!CPPMAKER_CMP("REQUIRE_NE", a, b, !=, "!=")) return; } while (0)

#define SKIP(reason) do { std::fprintf(stderr, "\n    ignoré : %s", reason); \
    ::cppmaker_test::skipped() = true; return; } while (0)

// Compatibilité GoogleTest (sous-ensemble).
#define EXPECT_TRUE(c) CHECK(c)
#define EXPECT_FALSE(c) CHECK_FALSE(c)
#define EXPECT_EQ(a, b) CHECK_EQ(a, b)
#define EXPECT_NE(a, b) CHECK_NE(a, b)
#define EXPECT_LT(a, b) CHECK_LT(a, b)
#define EXPECT_LE(a, b) CHECK_LE(a, b)
#define EXPECT_GT(a, b) CHECK_GT(a, b)
#define EXPECT_GE(a, b) CHECK_GE(a, b)
#define EXPECT_NEAR(a, b, e) CHECK_NEAR(a, b, e)
#define EXPECT_THROW(expr, type) CHECK_THROWS(expr)
#define EXPECT_NO_THROW(expr) CHECK_NOTHROW(expr)
#define ASSERT_TRUE(c) REQUIRE(c)
#define ASSERT_FALSE(c) REQUIRE_FALSE(c)
#define ASSERT_EQ(a, b) REQUIRE_EQ(a, b)
#define ASSERT_NE(a, b) REQUIRE_NE(a, b)
)TPL";
}

std::string_view test_main_source() {
    return R"TPL(// Point d'entrée des tests cpp-maker (généré, ne pas modifier).
#include <cpp-maker/test.hpp>

#include <cstring>

int main(int argc, char** argv) {
    std::string filter;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else if (std::strcmp(argv[i], "--list") == 0) list = true;
    }
    int failed = 0, skipped = 0, total = 0;
    for (auto& tc : ::cppmaker_test::registry()) {
        if (!filter.empty() && tc.name.find(filter) == std::string::npos) continue;
        if (list) { std::printf("%s\n", tc.name.c_str()); continue; }
        ++total;
        int before = ::cppmaker_test::failures();
        ::cppmaker_test::skipped() = false;
        try {
            tc.fn();
        } catch (const std::exception& e) {
            ::cppmaker_test::report("exception", e.what(), "?", 0);
        } catch (...) {
            ::cppmaker_test::report("exception", "inconnue", "?", 0);
        }
        std::fflush(stderr);
        if (::cppmaker_test::failures() > before) {
            std::printf("\n  [ FAIL ] %s\n", tc.name.c_str());
            ++failed;
        } else if (::cppmaker_test::skipped()) {
            std::printf("\n  [ SKIP ] %s\n", tc.name.c_str());
            ++skipped;
        } else {
            std::printf("  [ PASS ] %s\n", tc.name.c_str());
        }
        std::fflush(stdout);
    }
    if (list) return 0;
    std::printf("  %d/%d test(s) réussi(s)", total - failed - skipped, total);
    if (skipped) std::printf(", %d ignoré(s)", skipped);
    std::printf("\n");
    if (failed > 0) return 1;
    return (total > 0 && skipped == total) ? 77 : 0;
}
)TPL";
}

std::filesystem::path ensure_test_support(const std::filesystem::path& build_dir) {
    auto dir = build_dir / "support";
    util::write_file_if_changed(dir / "cpp-maker" / "test.hpp", std::string(test_header_source()));
    util::write_file_if_changed(dir / "cpp-maker" / "test_main.cpp", std::string(test_main_source()));
    return dir;
}

} // namespace build
