/* @file core/Redirect.cpp */

#include "core/Redirect.hpp"

#include "core/Util.hpp"
#include "io/toml.hpp"
#include "model/Model.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>

#include <fcntl.h>
#include <unistd.h>

namespace redirect {

Request extract(std::vector<std::string>& argv, std::string& error) {
    Request r;
    std::vector<std::string> kept;
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        if (a == "--") {
            kept.insert(kept.end(), argv.begin() + static_cast<long>(i), argv.end());
            break;
        }
        std::vector<std::string>* dest = nullptr;
        std::string name;
        for (auto [opt, d] : {std::pair<const char*, std::vector<std::string>*>{"--output", &r.output},
                              {"--stdout", &r.out}, {"--stderr", &r.err}}) {
            std::string o = opt;
            if (a == o || a.starts_with(o + "=")) {
                dest = d;
                name = o;
            }
        }
        if (!dest) {
            kept.push_back(a);
            continue;
        }
        if (a.size() > name.size()) {
            dest->push_back(a.substr(name.size() + 1));
        } else if (i + 1 < argv.size()) {
            dest->push_back(argv[++i]);
        } else {
            error = name + " : nom de fichier manquant";
            return r;
        }
        if (dest->back().empty()) {
            error = name + " : nom de fichier vide";
            return r;
        }
    }
    argv = std::move(kept);
    return r;
}

std::filesystem::path out_dir(const std::filesystem::path& cwd) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::absolute(cwd, ec);
    for (;;) {
        if (fs::is_regular_file(p / model::kProjectFile, ec)) {
            std::string dir = ".out";
            try {
                auto root = toml::parse_file(p / model::kProjectFile);
                if (auto* v = root.at_path({"paths", "out"})) {
                    bool ok = false;
                    auto list = v->as_strings(&ok);
                    if (ok && !list.empty()) dir = list.front();
                }
            } catch (const std::exception&) {
                // Fichier invalide : la commande le signalera ; on garde le défaut.
            }
            return p / dir;
        }
        if (!p.has_parent_path() || p.parent_path() == p) return cwd;
        p = p.parent_path();
    }
}

Session::~Session() { stop(); }

std::string Session::start(const Request& r, const std::filesystem::path& cwd) {
    namespace fs = std::filesystem;
    if (!r.any()) return {};
    fs::path base = out_dir(cwd);
    std::map<fs::path, int> by_path;   // un même fichier n'est ouvert qu'une fois
    auto open_file = [&](const std::string& name, int& fd) -> std::string {
        fs::path p = fs::path(name).has_parent_path() ? util::under(cwd, name) : base / name;
        p = p.lexically_normal();
        if (auto it = by_path.find(p); it != by_path.end()) {
            fd = it->second;
            return {};
        }
        std::error_code ec;
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) return "impossible d'ouvrir " + p.string() + " : " + std::strerror(errno);
        by_path[p] = fd;
        opened_.push_back(fd);
        return {};
    };
    std::vector<int> dests[2];
    for (auto* list : {&r.output, &r.out, &r.err}) {
        for (auto& name : *list) {
            int fd = -1;
            if (auto e = open_file(name, fd); !e.empty()) {
                stop();
                return e;
            }
            if (list != &r.err) dests[0].push_back(fd);
            if (list != &r.out) dests[1].push_back(fd);
        }
    }
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    for (int s = 0; s < 2; ++s) {
        auto& st = streams_[s];
        st.fd = s + 1;
        std::vector<int> uniq;
        for (int fd : dests[s])
            if (std::find(uniq.begin(), uniq.end(), fd) == uniq.end()) uniq.push_back(fd);
        if (uniq.empty()) continue;
        st.saved = ::dup(st.fd);
        if (uniq.size() == 1) {
            ::dup2(uniq[0], st.fd);
            continue;
        }
        // Plusieurs destinations : tube + fil qui recopie vers chaque fichier.
        int p[2];
        if (::pipe2(p, O_CLOEXEC) != 0) {
            stop();
            return "pipe() a échoué";
        }
        ::dup2(p[1], st.fd);
        ::close(p[1]);
        st.pipe_read = p[0];
        st.files = uniq;
        st.pump = std::thread([&st] {
            char buf[8192];
            for (;;) {
                ssize_t n = ::read(st.pipe_read, buf, sizeof buf);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                for (int fd : st.files) {
                    ssize_t off = 0;
                    while (off < n) {
                        ssize_t w = ::write(fd, buf + off, static_cast<size_t>(n - off));
                        if (w <= 0) break;
                        off += w;
                    }
                }
            }
        });
    }
    active_ = true;
    return {};
}

void Session::stop() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    for (auto& st : streams_) {
        if (st.saved >= 0) {
            // Restaurer le descripteur ferme l'extrémité d'écriture du tube :
            // le fil de recopie voit la fin de flux et se termine.
            ::dup2(st.saved, st.fd);
            ::close(st.saved);
            st.saved = -1;
        }
        if (st.pump.joinable()) st.pump.join();
        if (st.pipe_read >= 0) {
            ::close(st.pipe_read);
            st.pipe_read = -1;
        }
    }
    for (int fd : opened_) ::close(fd);
    opened_.clear();
    active_ = false;
}

} // namespace redirect
