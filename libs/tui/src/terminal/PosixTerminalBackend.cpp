#include <tui/terminal/PosixTerminalBackend.hpp>
#include <tui/core/Text.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iterator>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace tui {

namespace {

// Un gestionnaire de signal ne peut pas être une méthode d'instance ni
// capturer d'état ; ce drapeau global (même schéma que cpp-tui) est le
// seul canal async-signal-safe entre le handler SIGWINCH et poll_event().
// Un seul terminal réel étant attaché au process à la fois en pratique,
// un flag de portée namespace est suffisant.
volatile std::sig_atomic_t g_resize_pending = 0;

void handle_winch(int) { g_resize_pending = 1; }

/// Quantifie une couleur RGB vers la palette 256 couleurs (cube 6x6x6 +
/// niveaux de gris), pour les terminaux sans support true-color.
int quantize_to_256(Color c) {
    auto to6 = [](uint8_t v) { return static_cast<int>(v) * 5 / 255; };
    if (c.r == c.g && c.g == c.b) {
        if (c.r < 8) return 16;
        if (c.r > 248) return 231;
        return 232 + ((static_cast<int>(c.r) - 8) * 24 / 240);
    }
    int r = to6(c.r), g = to6(c.g), b = to6(c.b);
    return 16 + 36 * r + 6 * g + b;
}

} // namespace

PosixTerminalBackend::~PosixTerminalBackend() {
    if (initialized_) shutdown();
}

void PosixTerminalBackend::init() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        throw TerminalError("stdin/stdout must be a real TTY");
    }

    if (tcgetattr(STDIN_FILENO, &original_termios_) != 0) {
        throw TerminalError(std::string("tcgetattr failed: ") + std::strerror(errno));
    }
    termios_saved_ = true;

    struct termios raw = original_termios_;
    raw.c_iflag &= ~static_cast<tcflag_t>(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
    raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~static_cast<tcflag_t>(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        throw TerminalError(std::string("tcsetattr failed: ") + std::strerror(errno));
    }

    wakeup_fd_ = eventfd(0, EFD_NONBLOCK);
    if (wakeup_fd_ < 0) {
        throw TerminalError(std::string("eventfd failed: ") + std::strerror(errno));
    }

    struct sigaction sa{};
    sa.sa_handler = handle_winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    const char* colorterm = std::getenv("COLORTERM");
    true_color_ = colorterm != nullptr && (std::string_view(colorterm) == "truecolor" || std::string_view(colorterm) == "24bit");

    query_size();

    // Écran alternatif, curseur caché, collage entre crochets.
    write_raw("\x1b[?1049h\x1b[?25l\x1b[?2004h");
    clear_screen();

    initialized_ = true;
}

void PosixTerminalBackend::shutdown() {
    if (mouse_enabled_) enable_mouse(false);
    write_raw("\x1b[?2004l\x1b[?25h\x1b[?1049l");
    flush();

    if (termios_saved_) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios_);
    }
    if (wakeup_fd_ >= 0) {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
    initialized_ = false;
}

void PosixTerminalBackend::query_size() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        width_ = ws.ws_col;
        height_ = ws.ws_row;
    }
}

void PosixTerminalBackend::wake_up() {
    if (wakeup_fd_ < 0) return;
    uint64_t one = 1;
    // Best effort : si le buffer eventfd est plein, une notification
    // suffisait déjà à réveiller le poll() en attente.
    [[maybe_unused]] ssize_t ignored = write(wakeup_fd_, &one, sizeof(one));
}

std::optional<Event> PosixTerminalBackend::poll_event(std::chrono::milliseconds timeout) {
    if (g_resize_pending) {
        g_resize_pending = 0;
        query_size();
        return Event{ResizeEvent{width_, height_}};
    }

    if (!pending_events_.empty()) {
        Event ev = pending_events_.front();
        pending_events_.pop_front();
        return ev;
    }

    constexpr auto kEscapeFlushDelay = std::chrono::milliseconds(25);
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            if (parser_.has_pending_escape()) return parser_.flush_pending_escape();
            return std::nullopt;
        }

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        auto wait = parser_.has_pending_escape() ? std::min(remaining, kEscapeFlushDelay) : remaining;

        std::array<struct pollfd, 2> fds{{
            {STDIN_FILENO, POLLIN, 0},
            {wakeup_fd_, POLLIN, 0},
        }};
        int rc = poll(fds.data(), fds.size(), static_cast<int>(wait.count()));

        if (g_resize_pending) {
            g_resize_pending = 0;
            query_size();
            return Event{ResizeEvent{width_, height_}};
        }

        if (rc < 0) {
            if (errno == EINTR) continue;
            return std::nullopt;
        }

        if (rc == 0) {
            // Timeout de cette itération : soit on flush un ESC en
            // attente, soit on boucle (délai global pas encore atteint).
            if (parser_.has_pending_escape() && wait == kEscapeFlushDelay &&
                std::chrono::steady_clock::now() < deadline) {
                return parser_.flush_pending_escape();
            }
            continue;
        }

        if (fds[1].revents & POLLIN) {
            uint64_t drain = 0;
            [[maybe_unused]] ssize_t ignored = read(wakeup_fd_, &drain, sizeof(drain));
            return std::nullopt; // réveil pour messages postés, pas d'event terminal
        }

        if (fds[0].revents & POLLIN) {
            std::array<char, 4096> buf{};
            ssize_t n = read(STDIN_FILENO, buf.data(), buf.size());
            if (n > 0) {
                parser_.feed(std::string_view(buf.data(), static_cast<size_t>(n)));
                auto events = parser_.pump();
                if (!events.empty()) {
                    for (auto& e : events) pending_events_.push_back(std::move(e));
                    Event ev = pending_events_.front();
                    pending_events_.pop_front();
                    return ev;
                }
                // Séquence encore incomplète : reboucle.
                continue;
            }
        }
    }
}

void PosixTerminalBackend::append_cell_sequence(const Cell& cell, int x, int y) {
    // Écrit la séquence d'échappement complète d'une cellule directement
    // dans output_buffer_, sans chaîne temporaire ni std::to_string : c'est
    // le chemin le plus chaud du rendu (une cellule par cellule redessinée,
    // tout l'écran à chaque défilement du terminal intégré).
    std::format_to(std::back_inserter(output_buffer_), "\x1b[{};{}H\x1b[0", y + 1, x + 1);

    if (has_style(cell.style, TextStyle::Bold)) output_buffer_ += ";1";
    if (has_style(cell.style, TextStyle::Dim)) output_buffer_ += ";2";
    if (has_style(cell.style, TextStyle::Italic)) output_buffer_ += ";3";
    if (has_style(cell.style, TextStyle::Underline)) output_buffer_ += ";4";
    if (has_style(cell.style, TextStyle::Blink)) output_buffer_ += ";5";
    if (has_style(cell.style, TextStyle::Reverse)) output_buffer_ += ";7";
    if (has_style(cell.style, TextStyle::Strikethrough)) output_buffer_ += ";9";

    if (!cell.fg.is_default) {
        if (true_color_)
            std::format_to(std::back_inserter(output_buffer_), ";38;2;{};{};{}", cell.fg.r, cell.fg.g, cell.fg.b);
        else
            std::format_to(std::back_inserter(output_buffer_), ";38;5;{}", quantize_to_256(cell.fg));
    }
    if (!cell.bg.is_default) {
        if (true_color_)
            std::format_to(std::back_inserter(output_buffer_), ";48;2;{};{};{}", cell.bg.r, cell.bg.g, cell.bg.b);
        else
            std::format_to(std::back_inserter(output_buffer_), ";48;5;{}", quantize_to_256(cell.bg));
    }
    output_buffer_ += 'm';
    TextHelper::append_utf8(output_buffer_, cell.codepoint);
}

void PosixTerminalBackend::draw_cell(int x, int y, const Cell& cell) {
    if (cell.width == 0) return; // cellule de continuation d'un glyphe large : rien à dessiner
    append_cell_sequence(cell, x, y);
}

void PosixTerminalBackend::flush() {
    if (output_buffer_.empty()) return;
    write_raw(output_buffer_);
    output_buffer_.clear();
}

void PosixTerminalBackend::write_raw(std::string_view s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = write(STDOUT_FILENO, s.data() + off, s.size() - off);
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
}

void PosixTerminalBackend::clear_screen() {
    write_raw("\x1b[2J\x1b[H");
}

void PosixTerminalBackend::write_clipboard(std::string_view text) {
    // OSC 52 : copie vers le presse-papiers du terminal (donc du bureau).
    // Ignoré silencieusement par les terminaux qui ne le supportent pas.
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(4 + ((text.size() + 2) / 3) * 4 + 2);
    out += "\x1b]52;c;";
    size_t i = 0;
    while (i + 2 < text.size()) {
        uint32_t v = (static_cast<uint32_t>(static_cast<unsigned char>(text[i])) << 16) |
                     (static_cast<uint32_t>(static_cast<unsigned char>(text[i + 1])) << 8) |
                     static_cast<uint32_t>(static_cast<unsigned char>(text[i + 2]));
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += kAlphabet[v & 0x3F];
        i += 3;
    }
    if (i + 1 == text.size()) {
        uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(text[i])) << 16;
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += "==";
    } else if (i + 2 == text.size()) {
        uint32_t v = (static_cast<uint32_t>(static_cast<unsigned char>(text[i])) << 16) |
                     (static_cast<uint32_t>(static_cast<unsigned char>(text[i + 1])) << 8);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += '=';
    }
    out += "\x1b\\";
    write_raw(out);
    flush();
}

void PosixTerminalBackend::set_cursor_visible(bool visible) {
    write_raw(visible ? "\x1b[?25h" : "\x1b[?25l");
}

void PosixTerminalBackend::set_cursor_position(int x, int y) {
    write_raw("\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H");
}

void PosixTerminalBackend::enable_mouse(bool enabled) {
    mouse_enabled_ = enabled;
    write_raw(enabled ? "\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h" : "\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[?1006l");
}

} // namespace tui
