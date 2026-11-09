/* @file build/TestSupport.hpp
 *
 * Framework de tests intégré (tests.framework = "builtin") : cpp-maker écrit
 * <build>/support/cpp-maker/test.hpp et test_main.cpp, ajoute le dossier aux
 * chemins d'inclusion des tests et lie test_main à chaque exécutable de test.
 *
 *   #include <cpp-maker/test.hpp>
 *   TEST(addition) { CHECK_EQ(1 + 1, 2); }
 *   TEST(Suite, nom) { EXPECT_TRUE(x); ASSERT_EQ(a, b); }
 */
#pragma once

#include <filesystem>
#include <string_view>

namespace build {

std::string_view test_header_source();
std::string_view test_main_source();
/// Écrit les fichiers du framework (si modifiés) ; renvoie le dossier à inclure.
std::filesystem::path ensure_test_support(const std::filesystem::path& build_dir);

} // namespace build
