#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sysmon {

// --- CPU ---------------------------------------------------------------

struct CpuTimes {
    uint64_t total = 0;
    uint64_t idle = 0;
};

struct CpuStatState {
    CpuTimes overall;
    std::vector<CpuTimes> per_core;
};

struct CpuSample {
    double overall_percent = 0.0;
    std::vector<double> per_core_percent;
};

CpuSample read_cpu(CpuStatState& prev);

// --- Memory --------------------------------------------------------------

struct MemInfo {
    uint64_t total = 0;
    uint64_t used = 0;
    uint64_t cache = 0;
    uint64_t free = 0;
    uint64_t swap_total = 0;
    uint64_t swap_used = 0;
};

MemInfo read_mem();

// --- Network ---------------------------------------------------------------

struct NetState {
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
    bool has_prev = false;
};

struct NetSample {
    uint64_t rx_bytes_total = 0;
    uint64_t tx_bytes_total = 0;
    double rx_rate = 0.0; // bytes/s
    double tx_rate = 0.0; // bytes/s
};

NetSample read_net(NetState& prev, double dt_seconds);

// --- Disk I/O (aggregate) ---------------------------------------------------

struct DiskState {
    uint64_t read_bytes = 0;
    uint64_t write_bytes = 0;
    bool has_prev = false;
};

struct DiskIoSample {
    uint64_t read_bytes_total = 0;
    uint64_t write_bytes_total = 0;
    double read_rate = 0.0; // bytes/s
    double write_rate = 0.0; // bytes/s
};

DiskIoSample read_disk(DiskState& prev, double dt_seconds);

// --- File systems ------------------------------------------------------

struct MountInfo {
    std::string device;
    std::string mount_point;
    std::string fs_type;
    uint64_t total = 0;
    uint64_t available = 0;
    double used_fraction = 0.0;
};

std::vector<MountInfo> read_mounts();

// --- Processes -----------------------------------------------------------

struct ProcPrevSample {
    uint64_t cpu_ticks = 0;
    uint64_t disk_read_bytes = 0;
    uint64_t disk_write_bytes = 0;
};

struct ProcState {
    std::unordered_map<int, ProcPrevSample> prev;
};

struct ProcessInfo {
    int pid = 0;
    std::string name;
    std::string user;
    char state = '?';
    double cpu_percent = 0.0;
    uint64_t rss_bytes = 0;
    uint64_t disk_read_total = 0;
    uint64_t disk_write_total = 0;
    double disk_read_rate = 0.0;
    double disk_write_rate = 0.0;
    int nice = 0;
};

std::vector<ProcessInfo> read_processes(ProcState& prev, double dt_seconds);

// --- Formatting ------------------------------------------------------------

std::string human_bytes(double bytes);
std::string human_rate(double bytes_per_second);
std::string human_go(uint64_t bytes);

} // namespace sysmon
