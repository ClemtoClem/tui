/* @file analysis/Elf.hpp
 *
 * Lecture des en-têtes ELF (exécutables, .so, objets et archives .a) sans
 * binutils : tailles text/data/bss (sémantique Berkeley de `size`),
 * machine, ABI, type, point d'entrée, sections allouées.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace elf {

struct Section {
    std::string name;
    uint64_t size = 0;
    uint64_t flags = 0;
};

struct Info {
    bool ok = false;
    bool elf = false;
    std::string note;
    std::string cls, endian, type, machine, abi;
    uint64_t entry = 0;
    uint64_t text = 0, data = 0, bss = 0;
    uint64_t file_size = 0;
    int members = 0;   ///< archives : nombre d'objets ELF
    std::vector<Section> sections;
    [[nodiscard]] uint64_t dec() const { return text + data + bss; }
};

uint16_t rd16(const std::string& b, size_t o, bool be);
uint32_t rd32(const std::string& b, size_t o, bool be);
uint64_t rd64(const std::string& b, size_t o, bool be);
const char* machine_name(uint16_t m);
const char* abi_name(unsigned char a);
bool parse_at(const std::string& buf, size_t off, Info& in);
std::string output_section(const std::string& n);
Info read(const fs::path& p);

} // namespace elf
