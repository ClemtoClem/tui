/* @file core/Util.cpp */

#include "core/Util.hpp"
#include "core/Term.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace util {

// --- Chaînes -----------------------------------------------------------------

std::string trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) r += sep;
        r += v[i];
    }
    return r;
}

std::vector<std::string> split(std::string_view s, char sep, bool keep_empty) {
    std::vector<std::string> r;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        std::string part(s.substr(start, end - start));
        if (keep_empty || !part.empty()) r.push_back(std::move(part));
        start = end + 1;
    }
    return r;
}

std::vector<std::string> split_list(std::string_view s, char sep) {
    std::vector<std::string> r;
    for (auto& p : split(s, sep, false)) {
        auto t = trim(p);
        if (!t.empty()) r.push_back(std::move(t));
    }
    return r;
}

std::vector<std::string> split_ws(std::string_view s) {
    std::vector<std::string> r;
    std::istringstream in{std::string(s)};
    std::string w;
    while (in >> w) r.push_back(w);
    return r;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

bool contains(const std::vector<std::string>& v, std::string_view x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool push_unique(std::vector<std::string>& v, const std::string& x) {
    if (contains(v, x)) return false;
    v.push_back(x);
    return true;
}

void push_unique(std::vector<std::string>& v, const std::vector<std::string>& xs) {
    for (auto& x : xs) push_unique(v, x);
}

std::string identifier(std::string_view name) {
    std::string r;
    for (char c : name) r += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    if (r.empty() || std::isdigit(static_cast<unsigned char>(r[0]))) r = "_" + r;
    return r;
}

bool is_safe_name(std::string_view s) {
    if (s.empty() || s == "." || s == "..") return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '+';
    });
}

size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::string suggest(std::string_view word, const std::vector<std::string>& candidates) {
    std::string best;
    size_t best_d = std::max<size_t>(2, word.size() / 3) + 1;
    for (auto& c : candidates) {
        if (c.starts_with(word) && !word.empty()) return c;
        size_t d = edit_distance(word, c);
        if (d < best_d) { best_d = d; best = c; }
    }
    return best;
}

int compare_versions(std::string_view a, std::string_view b) {
    auto parts = [](std::string_view s) {
        std::vector<long> r;
        for (auto& p : split(s, '.')) {
            long v = 0;
            for (char c : p) {
                if (!std::isdigit(static_cast<unsigned char>(c))) break;
                v = v * 10 + (c - '0');
            }
            r.push_back(v);
        }
        return r;
    };
    auto pa = parts(a), pb = parts(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// --- Échappements ------------------------------------------------------------

std::string shell_quote(const std::string& a) {
    if (!a.empty() && std::all_of(a.begin(), a.end(), [](unsigned char c) {
            return std::isalnum(c) || std::string_view("_-./=:,+@%").find(static_cast<char>(c)) != std::string_view::npos;
        }))
        return a;
    return "'" + replace_all(a, "'", "'\\''") + "'";
}

std::string shell_join(const std::vector<std::string>& v) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) r += ' ';
        r += shell_quote(v[i]);
    }
    return r;
}

std::string json_escape(std::string_view s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\t': r += "\\t"; break;
        case '\r': r += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) r += std::format("\\u{:04x}", c);
            else r += c;
        }
    }
    return r;
}

std::string xml_escape(std::string_view s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '&': r += "&amp;"; break;
        case '<': r += "&lt;"; break;
        case '>': r += "&gt;"; break;
        case '"': r += "&quot;"; break;
        case '\'': r += "&apos;"; break;
        default:
            // Les octets de contrôle (hors \t \n \r) sont interdits en XML 1.0.
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
            r += c;
        }
    }
    return r;
}

// --- Fichiers ----------------------------------------------------------------

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string read_file_or_throw(const fs::path& p) {
    std::string s;
    if (!read_file(p, s)) fail("lecture impossible : {}", p.string());
    return s;
}

void write_file(const fs::path& p, const std::string& content) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += std::format(".tmp{}", static_cast<int>(::getpid()));
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) fail("écriture impossible : {}", p.string());
        f << content;
        if (!f) fail("écriture impossible : {}", p.string());
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        fail("écriture impossible : {} ({})", p.string(), ec.message());
    }
}

bool write_file_if_changed(const fs::path& p, const std::string& content) {
    std::string old;
    if (read_file(p, old) && old == content) return false;
    write_file(p, content);
    return true;
}

fs::path under(const fs::path& dir, const fs::path& p) {
    if (p.is_absolute()) return p.lexically_normal();
    if (dir.empty() || dir == ".") return p.lexically_normal();
    return (dir / p).lexically_normal();
}

std::string display(const fs::path& p, const fs::path& base) {
    std::error_code ec;
    auto rel = fs::relative(p, base, ec);
    if (ec || rel.empty() || rel.native().starts_with("..")) return p.string();
    return rel.string();
}

std::optional<fs::path> find_in_path(const std::string& name) {
    if (name.empty()) return std::nullopt;
    std::error_code ec;
    if (name.find('/') != std::string::npos) {
        if (fs::is_regular_file(name, ec) && ::access(name.c_str(), X_OK) == 0) return fs::path(name);
        return std::nullopt;
    }
    const char* env = std::getenv("PATH");
    for (auto& dir : split_list(env ? env : "/usr/local/bin:/usr/bin:/bin", ':')) {
        fs::path p = fs::path(dir) / name;
        if (fs::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0) return p;
    }
    return std::nullopt;
}

fs::path self_exe() {
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p;
    return "cpp-maker";
}

// --- Divers ------------------------------------------------------------------

std::string elapsed(std::chrono::steady_clock::time_point t0) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    return std::format("{}.{:02}s", ms / 1000, (ms % 1000) / 10);
}

std::string human_size(uintmax_t n) {
    if (n < 1024) return std::format("{} o", n);
    if (n < 1024 * 1024) return std::format("{:.1f} Kio", static_cast<double>(n) / 1024.0);
    return std::format("{:.1f} Mio", static_cast<double>(n) / (1024.0 * 1024.0));
}

void Hasher::feed(std::string_view s) {
    for (char ch : s) {
        h ^= static_cast<unsigned char>(ch);
        h *= 1099511628211ULL;
    }
    h ^= 0xff;
    h *= 1099511628211ULL;
}

void Hasher::feed_u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v & 0xff);
        h *= 1099511628211ULL;
        v >>= 8;
    }
}

// --- FileLock ----------------------------------------------------------------

FileLock& FileLock::operator=(FileLock&& o) noexcept {
    if (this != &o) {
        release();
        fd_ = o.fd_;
        o.fd_ = -1;
    }
    return *this;
}

void FileLock::release() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
}

bool FileLock::acquire(const fs::path& p, std::string_view waiting_message) {
    release();
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    int fd = ::open(p.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno != EWOULDBLOCK) {
            ::close(fd);
            return false;
        }
        std::string holder;
        char buf[32] = {};
        if (::pread(fd, buf, sizeof buf - 1, 0) > 0) {
            int pid = std::atoi(buf);
            if (pid > 0) holder = std::format(" (PID {})", pid);
        }
        if (!waiting_message.empty()) term::warn("{}{}", waiting_message, holder);
        if (::flock(fd, LOCK_EX) != 0) {
            ::close(fd);
            return false;
        }
        term::info("verrou obtenu, poursuite.");
    }
    if (::ftruncate(fd, 0) == 0) {
        auto s = std::format("{}\n", static_cast<int>(::getpid()));
        if (::write(fd, s.data(), s.size()) < 0) { /* sans importance */ }
    }
    fd_ = fd;
    return true;
}

} // namespace util
