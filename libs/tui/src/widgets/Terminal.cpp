#include <tui/widgets/Terminal.hpp>

#include <tui/core/Clipboard.hpp>
#include <tui/core/Text.hpp>
#include <tui/widget/Scrollbar.hpp>

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
        return TextHelper::encode_utf8(e.codepoint);
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
    int cols = logical_cols_ > 0 ? logical_cols_ : std::max(bounds_.width, 1);
    screen_.resize(std::max(cols, 1), std::max(bounds_.height, 1));
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
    size_t sb_before = screen_.scrollback_size();
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
    if (any) {
        // « offset depuis le haut » : rester ancré sur le même texte quand on
        // est reculé, suivre la sortie quand on était en bas.
        if (scroll_offset_ >= static_cast<int>(sb_before)) {
            scroll_offset_ = static_cast<int>(screen_.scrollback_size());
        }
        need_repaint();
    }
}

void Terminal::arrange(Rect final_rect) {
    bounds_ = final_rect;
    if (bounds_.width <= 0 || bounds_.height <= 0) return;
    int cols = logical_cols_ > 0 ? logical_cols_ : bounds_.width;
    // arrange() est appelé à chaque rendu : ne redimensionner (et prévenir le
    // processus enfant) que si la taille a réellement changé.
    if (cols == screen_.cols() && bounds_.height == screen_.rows()) return;
    screen_.resize(cols, bounds_.height);
    scroll_offset_ = static_cast<int>(screen_.scrollback_size()); // en bas (suivi)
    scroll_col_ = 0;
    clear_selection();
    sync_pty_size();
}

void Terminal::sync_pty_size() {
    if (!running()) return;
    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(screen_.cols());
    ws.ws_row = static_cast<unsigned short>(screen_.rows());
    ioctl(master_fd_, TIOCSWINSZ, &ws);
}

void Terminal::paint(Buffer& buffer) const {
    ClipGuard clip(buffer, bounds_);
    size_t sb = screen_.scrollback_size();
    int w = content_width();

    // Sélection (rectangle d'écran), hors drag en cours.
    bool has_sel = !selecting_ && (sel_anchor_row_ != sel_row_ || sel_anchor_col_ != sel_col_);
    int r0 = std::min(sel_anchor_row_, sel_row_);
    int r1 = std::max(sel_anchor_row_, sel_row_);
    int c0 = std::min(sel_anchor_col_, sel_col_);
    int c1 = std::max(sel_anchor_col_, sel_col_);
    auto in_sel = [&](int row, int col) {
        if (!has_sel) return false;
        if (row < r0 || row > r1) return false;
        if (r0 == r1) return col >= c0 && col < c1;
        if (row == r0) return col >= c0;
        if (row == r1) return col < c1;
        return true;
    };

    bool at_bottom = scroll_offset_ >= static_cast<int>(sb);
    for (int y = 0; y < bounds_.height; ++y) {
        for (int x = 0; x < w; ++x) {
            Cell cell = cell_at(y, x);
            if (in_sel(y, x)) cell.style = cell.style | TextStyle::Reverse;
            if (focused_ && running() && at_bottom &&
                x == screen_.cursor_col() - scroll_col_ && y == screen_.cursor_row()) {
                cell.style = cell.style | TextStyle::Reverse;
            }
            buffer.set(bounds_.x + x, bounds_.y + y, cell);
        }
    }
    if (!running()) {
        std::string msg = " [processus termine] ";
        auto decoded = TextHelper::decode_utf8(msg);
        int x = bounds_.x;
        for (char32_t ch : decoded) {
            if (x >= bounds_.x + w) break;
            buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::Reverse});
            ++x;
        }
    }
}

Size Terminal::content_size() const {
    return {screen_.cols(), static_cast<int>(screen_.scrollback_size()) + screen_.rows()};
}

Point Terminal::scroll_offset() const { return {scroll_col_, scroll_offset_}; }

void Terminal::set_scroll_offset(int x, int y) {
    scroll_col_ = std::clamp(x, 0, max_scroll_col());
    scroll_offset_ = std::clamp(y, 0, static_cast<int>(screen_.scrollback_size()));
    need_repaint();
}

int Terminal::max_scroll_col() const { return std::max(screen_.cols() - content_width(), 0); }

Cell Terminal::cell_at(int row, int col) const {
    size_t sb = screen_.scrollback_size();
    size_t L = static_cast<size_t>(std::max(scroll_offset_, 0)) + static_cast<size_t>(row);
    int gc = col + scroll_col_; // colonne dans la grille (décalage horizontal)
    if (L < sb) {
        const auto& line = screen_.scrollback_line(L);
        if (gc >= 0 && static_cast<size_t>(gc) < line.size()) return line[static_cast<size_t>(gc)];
        return Cell::blank();
    }
    int grid_row = static_cast<int>(L - sb);
    const Buffer& g = screen_.grid();
    if (gc >= 0 && gc < g.width() && grid_row >= 0 && grid_row < g.height()) return g.at(gc, grid_row);
    return Cell::blank();
}

std::string Terminal::selected_text() const {
    if (selecting_) return "";
    if (sel_anchor_row_ == sel_row_ && sel_anchor_col_ == sel_col_) return "";
    int r0 = std::min(sel_anchor_row_, sel_row_);
    int r1 = std::max(sel_anchor_row_, sel_row_);
    int c0 = std::min(sel_anchor_col_, sel_col_);
    int c1 = std::max(sel_anchor_col_, sel_col_);
    std::string out;
    for (int row = r0; row <= r1; ++row) {
        int from = row == r0 ? c0 : 0;
        int to = row == r1 ? c1 : content_width();
        std::string line;
        for (int x = from; x < to; ++x) {
            Cell c = cell_at(row, x);
            if (c.width == 0) continue; // continuation d'un glyphe large
            line += TextHelper::encode_utf8(c.codepoint);
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (row > r0) out += '\n';
        out += line;
    }
    return out;
}

void Terminal::clear_selection() {
    selecting_ = false;
    sel_anchor_row_ = sel_anchor_col_ = sel_row_ = sel_col_ = 0;
    need_repaint();
}

void Terminal::scroll_to(int offset) {
    int max_s = static_cast<int>(screen_.scrollback_size());
    scroll_offset_ = std::clamp(offset, 0, max_s);
    need_repaint();
}

void Terminal::scroll_by(int delta) { scroll_to(scroll_offset_ + delta); }

bool Terminal::on_key(const KeyEvent& e) {
    // Ctrl+C avec une sélection active : copie (sinon transmis au processus).
    bool ctrl_c = e.ctrl && e.key == Key::Char && (e.codepoint == U'c' || e.codepoint == U'C');
    bool has_sel = sel_anchor_row_ != sel_row_ || sel_anchor_col_ != sel_col_;
    if (ctrl_c && has_sel) {
        std::string sel = selected_text();
        if (!sel.empty()) Clipboard::instance().set(std::move(sel));
        clear_selection();
        return true;
    }
    // Défilement du scrollback : consommé, pas transmis à l'enfant.
    if (e.key == Key::PageUp) { scroll_by(-std::max(bounds_.height - 1, 1)); return true; }
    if (e.key == Key::PageDown) { scroll_by(std::max(bounds_.height - 1, 1)); return true; }
    if (e.key == Key::Home) { scroll_to(0); return true; }
    if (e.key == Key::End) { scroll_to(static_cast<int>(screen_.scrollback_size())); return true; }
    if (e.key == Key::Escape && has_sel) { clear_selection(); return true; }

    if (!running()) return false;
    std::string bytes = encode_key(e);
    if (bytes.empty()) return false;
    write_to_child(bytes);
    return true;
}

bool Terminal::on_mouse(const MouseEvent& e) {
    if (handle_h_scroll_wheel(e)) return true;
    if (e.button == MouseEvent::Button::WheelUp) { scroll_by(-3); clear_selection(); return true; }
    if (e.button == MouseEvent::Button::WheelDown) { scroll_by(3); clear_selection(); return true; }
    if (handle_scrollbar_mouse(e)) return true;

    if (e.button != MouseEvent::Button::Left) return false;
    int row = std::clamp(e.y - bounds_.y, 0, std::max(bounds_.height - 1, 0));
    int col = std::clamp(e.x - bounds_.x, 0, std::max(content_width() - 1, 0));
    switch (e.action) {
        case MouseEvent::Action::Press:
            selecting_ = true;
            sel_anchor_row_ = sel_row_ = row;
            sel_anchor_col_ = sel_col_ = col;
            need_repaint();
            return true;
        case MouseEvent::Action::Drag:
            if (selecting_) {
                sel_row_ = row;
                sel_col_ = col;
                need_repaint();
                return true;
            }
            return false;
        case MouseEvent::Action::Release:
            if (selecting_) {
                selecting_ = false;
                sel_row_ = row;
                sel_col_ = col;
                need_repaint();
                return true;
            }
            return false;
        default:
            return false;
    }
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
