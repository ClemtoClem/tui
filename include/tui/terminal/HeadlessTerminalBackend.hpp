/**
 * @file HeadlessTerminalBackend.hpp
 * @brief Backend de test : aucun syscall, événements et taille pilotés par le test.
 */

#pragma once

#include "ITerminalBackend.hpp"
#include "../Buffer.hpp"

#include <deque>

namespace tui {

class HeadlessTerminalBackend : public ITerminalBackend {
public:
    explicit HeadlessTerminalBackend(int width = 80, int height = 24)
        : width_(width), height_(height), painted_(width, height) {}

    void init() override { initialized_ = true; }
    void shutdown() override { initialized_ = false; }

    [[nodiscard]] std::optional<Event> poll_event(std::chrono::milliseconds) override {
        if (pending_events_.empty()) return std::nullopt;
        Event ev = pending_events_.front();
        pending_events_.pop_front();
        return ev;
    }

    void draw_cell(int x, int y, const Cell& cell) override {
        painted_.set(x, y, cell);
        ++draw_cell_calls_;
    }

    void flush() override { ++flush_calls_; }

    [[nodiscard]] int get_width() const override { return width_; }
    [[nodiscard]] int get_height() const override { return height_; }

    void clear_screen() override { painted_.clear(); }

    void set_cursor_visible(bool visible) override { cursor_visible_ = visible; }
    void set_cursor_position(int x, int y) override { cursor_x_ = x; cursor_y_ = y; }
    void enable_mouse(bool enabled) override { mouse_enabled_ = enabled; }
    [[nodiscard]] bool supports_true_color() const override { return true; }

    // --- Contrôle depuis les tests ---

    /// Met en file un événement synthétique, consommé FIFO par poll_event().
    void push_event(Event ev) { pending_events_.push_back(std::move(ev)); }

    /// Simule un redimensionnement du terminal (ne pousse PAS de
    /// ResizeEvent automatiquement - à faire explicitement via push_event
    /// si le test veut aussi que l'App le détecte).
    void set_size(int width, int height) {
        width_ = width;
        height_ = height;
        painted_.resize(width, height);
    }

    [[nodiscard]] const Buffer& painted() const { return painted_; }
    [[nodiscard]] bool is_initialized() const { return initialized_; }
    [[nodiscard]] bool cursor_visible() const { return cursor_visible_; }
    [[nodiscard]] bool mouse_enabled() const { return mouse_enabled_; }
    [[nodiscard]] size_t draw_cell_calls() const { return draw_cell_calls_; }
    [[nodiscard]] size_t flush_calls() const { return flush_calls_; }
    void reset_call_counters() { draw_cell_calls_ = 0; flush_calls_ = 0; }

private:
    int width_;
    int height_;
    Buffer painted_;
    std::deque<Event> pending_events_;
    bool initialized_ = false;
    bool cursor_visible_ = true;
    bool mouse_enabled_ = false;
    int cursor_x_ = 0;
    int cursor_y_ = 0;
    size_t draw_cell_calls_ = 0;
    size_t flush_calls_ = 0;
};

} // namespace tui
