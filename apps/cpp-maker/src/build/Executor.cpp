/* @file build/Executor.cpp */

#include "build/Executor.hpp"

#include "core/Process.hpp"
#include "core/Term.hpp"
#include "core/Util.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace build {

std::vector<fs::path> parse_depfile(const fs::path& p) {
    std::vector<fs::path> deps;
    std::string all;
    if (!util::read_file(p, all)) return deps;
    std::string joined;
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == '\\' && i + 1 < all.size() && all[i + 1] == '\n') { ++i; continue; }
        if (all[i] == '\\' && i + 2 < all.size() && all[i + 1] == '\r' && all[i + 2] == '\n') { i += 2; continue; }
        joined += all[i];
    }
    // Seule la première règle compte (-MP ajoute des cibles vides ensuite).
    std::string first = joined.substr(0, joined.find('\n'));
    auto colon = first.find(": ");
    if (colon == std::string::npos) colon = first.find(':');
    if (colon == std::string::npos) return deps;
    std::string body = first.substr(colon + 1), tok;
    auto flush = [&] {
        if (!tok.empty()) { deps.emplace_back(tok); tok.clear(); }
    };
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '\\' && i + 1 < body.size()) { tok += body[++i]; continue; }
        if (c == '$' && i + 1 < body.size() && body[i + 1] == '$') { tok += '$'; ++i; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { flush(); continue; }
        tok += c;
    }
    flush();
    return deps;
}

std::string fingerprint(const Task& t) {
    util::Hasher h;
    h.feed(t.tool_version);
    for (auto& a : t.cmd) h.feed(a);
    std::vector<fs::path> files = t.inputs;
    if (!t.depfile.empty()) {
        auto d = parse_depfile(t.depfile);
        if (!d.empty()) files = std::move(d);
    }
    for (auto& f : files) {
        h.feed(f.string());
        std::error_code ec;
        auto sz = fs::file_size(f, ec);
        if (ec) { h.feed("<absent>"); continue; }
        h.feed_u64(static_cast<uint64_t>(sz));
        auto mt = fs::last_write_time(f, ec);
        if (!ec) h.feed_u64(static_cast<uint64_t>(mt.time_since_epoch().count()));
    }
    return h.hex();
}

bool up_to_date(const Task& t) {
    std::error_code ec;
    for (auto& o : t.outputs)
        if (!fs::exists(o, ec)) return false;
    std::string old;
    if (t.stamp.empty() || !util::read_file(t.stamp, old)) return false;
    return old == fingerprint(t);
}

std::vector<char> predict_dirty(const std::vector<Task>& tasks) {
    std::vector<char> dirty(tasks.size(), 0);
    // Les tâches sont créées dépendances d'abord : un seul passage suffit.
    for (size_t i = 0; i < tasks.size(); ++i) {
        bool d = !up_to_date(tasks[i]);
        for (auto dep : tasks[i].deps)
            if (dirty[dep]) d = true;
        dirty[i] = d;
    }
    return dirty;
}

int parallel_jobs(int requested) {
    unsigned hc = std::thread::hardware_concurrency();
    int ncpus = hc == 0 ? 1 : static_cast<int>(hc);
    int budget = std::max(1, ncpus - 1);
    if (requested > 0) return requested;
    return budget;
}

namespace {
std::string kind_label(const std::string& k) {
    std::string padded = std::format("{:<6}", k);
    if (k == "CC" || k == "CXX" || k == "AS") return term::yellow(padded);
    if (k == "AR") return term::cyan(padded);
    return term::magenta(padded);
}
} // namespace

ExecStats execute(const std::vector<Task>& tasks, const ExecOptions& o) {
    ExecStats st;
    const size_t n = tasks.size();
    if (n == 0) return st;

    auto dirty = predict_dirty(tasks);
    size_t total = static_cast<size_t>(std::count(dirty.begin(), dirty.end(), 1));

    std::vector<int> pending(n, 0);
    std::vector<std::vector<size_t>> rdeps(n);
    for (size_t i = 0; i < n; ++i) {
        pending[i] = static_cast<int>(tasks[i].deps.size());
        for (auto d : tasks[i].deps) rdeps[d].push_back(i);
    }
    std::deque<size_t> ready;
    for (size_t i = 0; i < n; ++i)
        if (pending[i] == 0) ready.push_back(i);

    std::vector<char> state(n, 0);   // 0 en attente, 1 ok, 2 échec/ignorée
    std::mutex mu, print_mu;
    std::condition_variable cv;
    size_t finished = 0, counter = 0;
    bool stop = false;
    int jobs = std::max(1, std::min<int>(parallel_jobs(o.jobs), static_cast<int>(n)));

    auto run_one = [&](const Task& t) -> bool {
        if (up_to_date(t)) {
            std::lock_guard lk(mu);
            ++st.uptodate;
            return true;
        }
        std::error_code ec;
        for (auto& out : t.outputs) {
            if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
            if (t.remove_outputs_first) fs::remove(out, ec);
        }
        if (!t.stamp.empty() && t.stamp.has_parent_path()) fs::create_directories(t.stamp.parent_path(), ec);
        {
            std::lock_guard lk(print_mu);
            size_t idx;
            {
                std::lock_guard lk2(mu);
                idx = ++counter;
            }
            term::outln("[{}/{}] {} {}", idx, std::max(idx, total), kind_label(t.kind), t.label);
            if (o.verbose) term::outln("      $ {}", util::shell_join(t.cmd));
            std::cout.flush();
        }
        auto r = proc::run(t.cmd, {.capture = true, .cwd = o.root.empty() ? std::nullopt : std::optional(o.root)});
        bool ok = r.exit_code == 0;
        for (auto& out : t.outputs)
            if (ok && !fs::exists(out, ec)) ok = false;
        {
            std::lock_guard lk(print_mu);
            if (!ok) {
                term::errln("{} {} {} (code {})", term::red("ÉCHEC :"), t.kind, t.label, r.exit_code);
                if (!o.verbose) term::errln("      $ {}", util::shell_join(t.cmd));
            }
            if (!r.output.empty()) {
                std::cout.flush();
                (ok ? std::cout : std::cerr) << r.output;
                if (r.output.back() != '\n') (ok ? std::cout : std::cerr) << '\n';
            }
        }
        if (!ok) {
            fs::remove(t.stamp, ec);
            return false;
        }
        if (!t.stamp.empty()) util::write_file(t.stamp, fingerprint(t));
        std::lock_guard lk(mu);
        ++st.ran;
        return true;
    };

    auto worker = [&] {
        std::unique_lock lk(mu);
        for (;;) {
            cv.wait(lk, [&] { return !ready.empty() || finished == n; });
            if (ready.empty()) return;
            size_t i = ready.front();
            ready.pop_front();
            bool dep_failed = std::any_of(tasks[i].deps.begin(), tasks[i].deps.end(),
                                          [&](size_t d) { return state[d] == 2; });
            bool abort = stop;
            lk.unlock();
            bool ok = false;
            if (!dep_failed && !abort) {
                try {
                    ok = run_one(tasks[i]);
                } catch (const std::exception& e) {
                    std::lock_guard pl(print_mu);
                    term::error("{} : {}", tasks[i].label, e.what());
                    ok = false;
                }
            }
            lk.lock();
            if (dep_failed || abort) ++st.skipped;
            else if (!ok) {
                ++st.failed;
                if (!o.keep_going) stop = true;
            }
            state[i] = ok ? 1 : 2;
            ++finished;
            for (auto r : rdeps[i])
                if (--pending[r] == 0) ready.push_back(r);
            cv.notify_all();
        }
    };

    std::vector<std::thread> pool;
    for (int i = 0; i < jobs; ++i) pool.emplace_back(worker);
    for (auto& th : pool) th.join();
    return st;
}

} // namespace build
