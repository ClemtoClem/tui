#include <tui/widgets/Terminal.hpp>

#include <tui/core/Text.hpp>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>

#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tui {

namespace {

std::string encode_key(const KeyEvent& e) {
    if (e.key == Key::Char) {
        if (e.ctrl && e.codepoint >= U'a' && e.codepoint <= U'z') {
            return std::string(1, static_cast<char>(e.codepoint - U'a' + 1));
        }
        if (e.ctrl && e.codepoint >= U'A' && e.codepoint <= U'Z') {
            return std::string(1, static_cast<char>(e.codepoint - U'A' + 1));
        }
        return TextHelper::encode_utf8(std::u32string(1, e.codepoint));
    }
    switch (e.key) {
        case Key::Enter: return "\r";
        case Key::Backspace: return "\x7f";
        case Key::Tab: return "\t";
        case Key::BackTab: return "\x1b[Z";
        case Key::Escape: return "\x1b";
        case Key::Up: return "\x1b[A";
        case Key::Down: return "\x1b[B";
        case Key::Right: return "\x1b[C";
        case Key::Left: return "\x1b[D";
        case Key::Home: return "\x1b[H";
        case Key::End: return "\x1b[F";
        case Key::PageUp: return "\x1b[5~";
        case Key::PageDown: return "\x1b[6~";
        case Key::Delete: return "\x1b[3~";
        case Key::Insert: return "\x1b[2~";
        default: return "";
    }
}

} // namespace

Terminal::~Terminal() { stop(); }

bool Terminal::start(const std::string& shell) {
    if (running()) return true;

    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(std::max(bounds_.width, 1));
    ws.ws_row = static_cast<unsigned short>(std::max(bounds_.height, 1));

    pid_t pid = forkpty(&master_fd_, nullptr, nullptr, &ws);
    if (pid < 0) return false;

    if (pid == 0) {
        const char* sh = !shell.empty() ? shell.c_str() : std::getenv("SHELL");
        if (!sh || sh[0] == '\0') sh = "/bin/sh";
        setenv("TERM", "xterm-256color", 1);
        execlp(sh, sh, nullptr);
        _exit(127); // execlp a échoué (shell introuvable) : ne doit jamais revenir sinon
    }

    pid_ = pid;
    int flags = fcntl(master_fd_, F_GETFL, 0);
    if (flags >= 0) fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
    screen_.resize(std::max(bounds_.width, 1), std::max(bounds_.height, 1));
    need_repaint();
    return true;
}

void Terminal::stop() {
    if (pid_ > 0) {
        kill(pid_, SIGHUP);
        int status = 0;
        waitpid(pid_, &status, WNOHANG);
        pid_ = -1;
    }
    if (master_fd_ >= 0) {
        close(master_fd_);
        master_fd_ = -1;
    }
}

void Terminal::pump() {
    if (!running()) return;

    int status = 0;
    if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        need_repaint();
        return;
    }

    char buf[4096];
    bool any = false;
    while (true) {
        ssize_t n = read(master_fd_, buf, sizeof(buf));
        if (n > 0) {
            screen_.feed(std::string_view(buf, static_cast<size_t>(n)));
            any = true;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break; // EAGAIN/EWOULDBLOCK (rien de plus dispo) ou n==0
    }
    if (any) need_repaint();
}

void Terminal::arrange(Rect final_rect) {
    bounds_ = final_rect;
    if (bounds_.width <= 0 || bounds_.height <= 0) return;
    screen_.resize(bounds_.width, bounds_.height);
    sync_pty_size();
}

void Terminal::sync_pty_size() {
    if (!running()) return;
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(bounds_.width);
    ws.ws_row = static_cast<unsigned short>(bounds_.height);
    ioctl(master_fd_, TIOCSWINSZ, &ws);
}

void Terminal::paint(Buffer& buffer) const {
    ClipGuard clip(buffer, bounds_);
    const Buffer& grid = screen_.grid();
    for (int y = 0; y < grid.height() && y < bounds_.height; ++y) {
        for (int x = 0; x < grid.width() && x < bounds_.width; ++x) {
            Cell cell = grid.at(x, y);
            bool is_cursor = focused_ && running() && x == screen_.cursor_col() && y == screen_.cursor_row();
            if (is_cursor) cell.style = cell.style | TextStyle::Reverse;
            buffer.set(bounds_.x + x, bounds_.y + y, cell);
        }
    }
    if (!running()) {
        std::string msg = " [processus termine] ";
        auto decoded = TextHelper::decode_utf8(msg);
        int x = bounds_.x;
        for (char32_t ch : decoded) {
            if (x >= bounds_.x + bounds_.width) break;
            buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::Reverse});
            ++x;
        }
    }
}

bool Terminal::on_key(const KeyEvent& e) {
    if (!running()) return false;
    std::string bytes = encode_key(e);
    if (bytes.empty()) return false;
    write_to_child(bytes);
    return true;
}

bool Terminal::on_mouse(const MouseEvent&) {
    return false; // pas de report souris vers l'enfant (voir portée documentée dans AnsiScreen.hpp)
}

void Terminal::write_to_child(std::string_view bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = write(master_fd_, bytes.data() + off, bytes.size() - off);
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
}

} // namespace tui
