/**
 * @file PosixTerminalBackend.hpp
 * @brief Backend terminal réel : termios raw mode + sortie ANSI/VT + VTParser.
 *
 * Aucune dépendance à ncurses/notcurses : mode brut via termios, sortie
 * batchée (un seul write() par frame), entrée décodée par VTParser.
 * POSIX/Linux uniquement pour la Phase 1 (termios, poll(), SIGWINCH,
 * eventfd) - un futur backend Windows s'insérerait via ITerminalBackend
 * sans redesign.
 */

#pragma once

#include "ITerminalBackend.hpp"
#include "VTParser.hpp"

#include <deque>
#include <string>
#include <termios.h>

namespace tui {

class PosixTerminalBackend : public ITerminalBackend {
public:
    PosixTerminalBackend() = default;
    ~PosixTerminalBackend() override;

    PosixTerminalBackend(const PosixTerminalBackend&) = delete;
    PosixTerminalBackend& operator=(const PosixTerminalBackend&) = delete;

    void init() override;
    void shutdown() override;

    [[nodiscard]] std::optional<Event> poll_event(std::chrono::milliseconds timeout) override;

    void draw_cell(int x, int y, const Cell& cell) override;
    void flush() override;

    [[nodiscard]] int get_width() const override { return width_; }
    [[nodiscard]] int get_height() const override { return height_; }

    void clear_screen() override;
    void set_cursor_visible(bool visible) override;
    void set_cursor_position(int x, int y) override;
    void enable_mouse(bool enabled) override;
    [[nodiscard]] bool supports_true_color() const override { return true_color_; }

    /// Thread-safe : voir ITerminalBackend::wake_up().
    void wake_up() override;

private:
    void query_size();
    void write_raw(std::string_view s);
    [[nodiscard]] std::string sgr_sequence(const Cell& cell) const;

    bool initialized_ = false;
    struct termios original_termios_{};
    bool termios_saved_ = false;

    int width_ = 80;
    int height_ = 24;
    bool true_color_ = false;
    bool mouse_enabled_ = false;

    int wakeup_fd_ = -1;

    VTParser parser_;
    std::deque<Event> pending_events_;
    std::string output_buffer_;
};

} // namespace tui
