#include "sysinfo/Sysinfo.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/statvfs.h>
#include <unistd.h>

namespace sysmon {

namespace {

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

uint64_t to_u64(const std::string& s) {
    try {
        return std::stoull(s);
    } catch (...) {
        return 0;
    }
}

// --- /proc/stat -------------------------------------------------------

bool parse_cpu_line(const std::string& line, CpuTimes& out) {
    auto fields = split_ws(line);
    if (fields.size() < 9 || fields[0].rfind("cpu", 0) != 0) return false;
    uint64_t user = to_u64(fields[1]);
    uint64_t nice = to_u64(fields[2]);
    uint64_t system = to_u64(fields[3]);
    uint64_t idle = to_u64(fields[4]);
    uint64_t iowait = to_u64(fields[5]);
    uint64_t irq = to_u64(fields[6]);
    uint64_t softirq = to_u64(fields[7]);
    uint64_t steal = fields.size() > 8 ? to_u64(fields[8]) : 0;
    out.idle = idle + iowait;
    out.total = user + nice + system + idle + iowait + irq + softirq + steal;
    return true;
}

double percent_from_delta(const CpuTimes& prev, const CpuTimes& cur) {
    if (cur.total <= prev.total) return 0.0;
    uint64_t total_delta = cur.total - prev.total;
    uint64_t idle_delta = cur.idle >= prev.idle ? cur.idle - prev.idle : 0;
    if (total_delta == 0) return 0.0;
    double pct = 100.0 * static_cast<double>(total_delta - idle_delta) / static_cast<double>(total_delta);
    return std::clamp(pct, 0.0, 100.0);
}

} // namespace

CpuSample read_cpu(CpuStatState& prev) {
    CpuSample sample;
    std::ifstream f("/proc/stat");
    if (!f) return sample;

    CpuTimes overall{};
    std::vector<CpuTimes> cores;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("cpu", 0) != 0) break;
        CpuTimes t{};
        if (!parse_cpu_line(line, t)) continue;
        if (line.rfind("cpu ", 0) == 0) {
            overall = t;
        } else {
            cores.push_back(t);
        }
    }

    if (prev.per_core.size() != cores.size()) {
        // Premier appel (ou changement du nombre de coeurs) : pas de delta possible.
        prev.overall = overall;
        prev.per_core = cores;
        sample.per_core_percent.assign(cores.size(), 0.0);
        return sample;
    }

    sample.overall_percent = percent_from_delta(prev.overall, overall);
    sample.per_core_percent.reserve(cores.size());
    for (size_t i = 0; i < cores.size(); ++i) {
        sample.per_core_percent.push_back(percent_from_delta(prev.per_core[i], cores[i]));
    }

    prev.overall = overall;
    prev.per_core = cores;
    return sample;
}

// --- /proc/meminfo ---------------------------------------------------

MemInfo read_mem() {
    MemInfo info;
    std::ifstream f("/proc/meminfo");
    if (!f) return info;

    uint64_t mem_total = 0, mem_free = 0, buffers = 0, cached = 0, sreclaimable = 0;
    uint64_t swap_total = 0, swap_free = 0;

    std::string line;
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        auto fields = split_ws(line.substr(colon + 1));
        if (fields.empty()) continue;
        uint64_t value_kb = to_u64(fields[0]);

        if (key == "MemTotal") mem_total = value_kb;
        else if (key == "MemFree") mem_free = value_kb;
        else if (key == "Buffers") buffers = value_kb;
        else if (key == "Cached") cached = value_kb;
        else if (key == "SReclaimable") sreclaimable = value_kb;
        else if (key == "SwapTotal") swap_total = value_kb;
        else if (key == "SwapFree") swap_free = value_kb;
    }

    uint64_t cache_kb = buffers + cached + sreclaimable;
    uint64_t used_kb = mem_total > mem_free + cache_kb ? mem_total - mem_free - cache_kb : 0;

    info.total = mem_total * 1024;
    info.free = mem_free * 1024;
    info.cache = cache_kb * 1024;
    info.used = used_kb * 1024;
    info.swap_total = swap_total * 1024;
    info.swap_used = swap_total >= swap_free ? (swap_total - swap_free) * 1024 : 0;
    return info;
}

// --- /proc/net/dev -----------------------------------------------------

NetSample read_net(NetState& prev, double dt_seconds) {
    NetSample sample;
    std::ifstream f("/proc/net/dev");
    if (!f) return sample;

    std::string line;
    std::getline(f, line); // ligne d'entete "Inter-|..."
    std::getline(f, line); // ligne d'entete "face |bytes..."

    uint64_t rx_total = 0, tx_total = 0;
    while (std::getline(f, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string iface = line.substr(0, colon);
        iface.erase(0, iface.find_first_not_of(" \t"));
        if (iface == "lo") continue;

        auto fields = split_ws(line.substr(colon + 1));
        if (fields.size() < 9) continue;
        rx_total += to_u64(fields[0]);
        tx_total += to_u64(fields[8]);
    }

    sample.rx_bytes_total = rx_total;
    sample.tx_bytes_total = tx_total;
    if (prev.has_prev && dt_seconds > 0.0) {
        sample.rx_rate = rx_total >= prev.rx_bytes ? static_cast<double>(rx_total - prev.rx_bytes) / dt_seconds : 0.0;
        sample.tx_rate = tx_total >= prev.tx_bytes ? static_cast<double>(tx_total - prev.tx_bytes) / dt_seconds : 0.0;
    }
    prev.rx_bytes = rx_total;
    prev.tx_bytes = tx_total;
    prev.has_prev = true;
    return sample;
}

// --- /proc/diskstats -----------------------------------------------------

namespace {

bool is_whole_disk(const std::string& name) {
    if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0 || name.rfind("sr", 0) == 0 ||
        name.rfind("dm-", 0) == 0) {
        return false;
    }
    if (name.rfind("nvme", 0) == 0 || name.rfind("mmcblk", 0) == 0) {
        // Partition si le nom contient un segment "pN" a la fin (ex: nvme0n1p1, mmcblk0p1).
        return name.find('p') == std::string::npos ||
               name.find_last_of('p') < name.find_first_of("0123456789");
    }
    // sdX, hdX, vdX, xvdX... : partition si le dernier caractere est un chiffre.
    return !std::isdigit(static_cast<unsigned char>(name.back()));
}

} // namespace

DiskIoSample read_disk(DiskState& prev, double dt_seconds) {
    DiskIoSample sample;
    std::ifstream f("/proc/diskstats");
    if (!f) return sample;

    uint64_t read_sectors = 0, write_sectors = 0;
    std::string line;
    while (std::getline(f, line)) {
        auto fields = split_ws(line);
        if (fields.size() < 10) continue;
        const std::string& name = fields[2];
        if (!is_whole_disk(name)) continue;
        read_sectors += to_u64(fields[5]);
        write_sectors += to_u64(fields[9]);
    }

    constexpr uint64_t kSectorBytes = 512;
    uint64_t read_bytes = read_sectors * kSectorBytes;
    uint64_t write_bytes = write_sectors * kSectorBytes;

    sample.read_bytes_total = read_bytes;
    sample.write_bytes_total = write_bytes;
    if (prev.has_prev && dt_seconds > 0.0) {
        sample.read_rate =
            read_bytes >= prev.read_bytes ? static_cast<double>(read_bytes - prev.read_bytes) / dt_seconds : 0.0;
        sample.write_rate =
            write_bytes >= prev.write_bytes ? static_cast<double>(write_bytes - prev.write_bytes) / dt_seconds : 0.0;
    }
    prev.read_bytes = read_bytes;
    prev.write_bytes = write_bytes;
    prev.has_prev = true;
    return sample;
}

// --- /proc/mounts --------------------------------------------------------

namespace {

const std::vector<std::string>& pseudo_fs_blacklist() {
    static const std::vector<std::string> kBlacklist = {
        "proc", "sysfs", "cgroup", "cgroup2", "devtmpfs", "devpts", "tmpfs", "squashfs", "overlay",
        "autofs", "mqueue", "debugfs", "tracefs", "securityfs", "pstore", "bpf", "configfs", "fusectl",
        "binfmt_misc", "hugetlbfs", "ramfs", "nsfs", "efivarfs", "fuse.portal", "fuse.gvfsd-fuse",
        "fuse.snapfuse", "rpc_pipefs",
    };
    return kBlacklist;
}

std::string unescape_octal(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
            int value = (s[i + 1] - '0') * 64 + (s[i + 2] - '0') * 8 + (s[i + 3] - '0');
            out.push_back(static_cast<char>(value));
            i += 3;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

} // namespace

std::vector<MountInfo> read_mounts() {
    std::vector<MountInfo> result;
    std::ifstream f("/proc/mounts");
    if (!f) return result;

    const auto& blacklist = pseudo_fs_blacklist();
    std::string line;
    while (std::getline(f, line)) {
        auto fields = split_ws(line);
        if (fields.size() < 3) continue;
        std::string device = unescape_octal(fields[0]);
        std::string mount_point = unescape_octal(fields[1]);
        std::string fs_type = fields[2];

        if (std::find(blacklist.begin(), blacklist.end(), fs_type) != blacklist.end()) continue;
        if (fs_type.rfind("fuse.", 0) == 0) continue;

        struct statvfs vfs {};
        if (statvfs(mount_point.c_str(), &vfs) != 0) continue;
        uint64_t total = static_cast<uint64_t>(vfs.f_frsize) * vfs.f_blocks;
        if (total == 0) continue;
        uint64_t free_blocks_bytes = static_cast<uint64_t>(vfs.f_frsize) * vfs.f_bfree;
        uint64_t avail_bytes = static_cast<uint64_t>(vfs.f_frsize) * vfs.f_bavail;
        uint64_t used = total > free_blocks_bytes ? total - free_blocks_bytes : 0;

        MountInfo info;
        info.device = device;
        info.mount_point = mount_point;
        info.fs_type = fs_type;
        info.total = total;
        info.available = avail_bytes;
        info.used_fraction = std::clamp(static_cast<double>(used) / static_cast<double>(total), 0.0, 1.0);
        result.push_back(std::move(info));
    }
    return result;
}

// --- Processes -------------------------------------------------------------

namespace {

std::string username_for_uid(uid_t uid) {
    static std::unordered_map<uid_t, std::string> cache;
    auto it = cache.find(uid);
    if (it != cache.end()) return it->second;
    std::string name;
    if (struct passwd* pw = getpwuid(uid)) {
        name = pw->pw_name;
    } else {
        name = std::to_string(uid);
    }
    cache.emplace(uid, name);
    return name;
}

bool read_stat_fields(int pid, std::string& comm, char& state, uint64_t& utime, uint64_t& stime, int& nice) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    if (!f) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto open_paren = content.find('(');
    auto close_paren = content.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos || close_paren < open_paren) {
        return false;
    }
    comm = content.substr(open_paren + 1, close_paren - open_paren - 1);

    auto rest = split_ws(content.substr(close_paren + 1));
    if (rest.size() < 17) return false;
    state = rest[0].empty() ? '?' : rest[0][0];
    utime = to_u64(rest[11]);
    stime = to_u64(rest[12]);
    nice = static_cast<int>(std::stol(rest[16]));
    return true;
}

bool read_status_fields(int pid, uid_t& uid, uint64_t& rss_kb) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/status");
    if (!f) return false;
    std::string line;
    bool got_uid = false, got_rss = false;
    while (std::getline(f, line) && !(got_uid && got_rss)) {
        if (line.rfind("Uid:", 0) == 0) {
            auto fields = split_ws(line.substr(4));
            if (!fields.empty()) { uid = static_cast<uid_t>(to_u64(fields[0])); got_uid = true; }
        } else if (line.rfind("VmRSS:", 0) == 0) {
            auto fields = split_ws(line.substr(6));
            if (!fields.empty()) { rss_kb = to_u64(fields[0]); got_rss = true; }
        }
    }
    return got_uid;
}

void read_io_fields(int pid, uint64_t& read_bytes, uint64_t& write_bytes) {
    read_bytes = write_bytes = 0;
    std::ifstream f("/proc/" + std::to_string(pid) + "/io");
    if (!f) return; // acces refuse pour un processus d'un autre utilisateur : on laisse 0.
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("read_bytes:", 0) == 0) {
            auto fields = split_ws(line.substr(11));
            if (!fields.empty()) read_bytes = to_u64(fields[0]);
        } else if (line.rfind("write_bytes:", 0) == 0) {
            auto fields = split_ws(line.substr(12));
            if (!fields.empty()) write_bytes = to_u64(fields[0]);
        }
    }
}

} // namespace

std::vector<ProcessInfo> read_processes(ProcState& prev, double dt_seconds) {
    std::vector<ProcessInfo> result;

    DIR* dir = opendir("/proc");
    if (!dir) return result;

    long clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0) clk_tck = 100;

    std::unordered_map<int, ProcPrevSample> next_prev;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        bool numeric = name[0] != '\0';
        for (const char* p = name; *p; ++p) {
            if (!std::isdigit(static_cast<unsigned char>(*p))) { numeric = false; break; }
        }
        if (!numeric) continue;
        int pid = std::atoi(name);

        std::string comm;
        char state = '?';
        uint64_t utime = 0, stime = 0;
        int nice = 0;
        if (!read_stat_fields(pid, comm, state, utime, stime, nice)) continue;

        uid_t uid = 0;
        uint64_t rss_kb = 0;
        if (!read_status_fields(pid, uid, rss_kb)) continue;

        uint64_t read_bytes = 0, write_bytes = 0;
        read_io_fields(pid, read_bytes, write_bytes);

        uint64_t ticks = utime + stime;
        double cpu_percent = 0.0;
        double read_rate = 0.0, write_rate = 0.0;
        auto it = prev.prev.find(pid);
        if (it != prev.prev.end() && dt_seconds > 0.0) {
            if (ticks >= it->second.cpu_ticks) {
                uint64_t delta = ticks - it->second.cpu_ticks;
                cpu_percent = 100.0 * static_cast<double>(delta) / (static_cast<double>(clk_tck) * dt_seconds);
            }
            if (read_bytes >= it->second.disk_read_bytes) {
                read_rate = static_cast<double>(read_bytes - it->second.disk_read_bytes) / dt_seconds;
            }
            if (write_bytes >= it->second.disk_write_bytes) {
                write_rate = static_cast<double>(write_bytes - it->second.disk_write_bytes) / dt_seconds;
            }
        }
        next_prev[pid] = ProcPrevSample{ticks, read_bytes, write_bytes};

        ProcessInfo info;
        info.pid = pid;
        info.name = comm;
        info.user = username_for_uid(uid);
        info.state = state;
        info.cpu_percent = std::max(cpu_percent, 0.0);
        info.rss_bytes = rss_kb * 1024;
        info.disk_read_total = read_bytes;
        info.disk_write_total = write_bytes;
        info.disk_read_rate = read_rate;
        info.disk_write_rate = write_rate;
        info.nice = nice;
        result.push_back(std::move(info));
    }
    closedir(dir);

    prev.prev = std::move(next_prev);
    return result;
}

// --- Formatting ------------------------------------------------------------

namespace {

std::string format_with_unit(double value, const char* unit_suffix) {
    static const char* kUnits[] = {"", "K", "M", "G", "T"};
    size_t unit_index = 0;
    double v = value;
    while (v >= 1024.0 && unit_index + 1 < std::size(kUnits)) {
        v /= 1024.0;
        ++unit_index;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f %s%s", v, kUnits[unit_index], unit_suffix);
    return buf;
}

} // namespace

std::string human_bytes(double bytes) { return format_with_unit(bytes, "o"); }

std::string human_rate(double bytes_per_second) { return format_with_unit(bytes_per_second, "o/s"); }

std::string human_go(uint64_t bytes) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f Go", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

} // namespace sysmon
