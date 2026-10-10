/**
 * @file Buffer.hpp
 * @brief Grille de cellules double-bufferisée avec suivi des cellules modifiées.
 */

#pragma once

#include "core/Cell.hpp"
#include "core/Geometry.hpp"
#include "terminal/ITerminalBackend.hpp"

#include <vector>

namespace tui {

/// Grille de cellules "back buffer" dans laquelle les widgets peignent.
/// Ne connaît pas le terminal ; c'est une simple matrice adressable.
class Buffer {
public:
    Buffer(int width = 0, int height = 0) : width_(width), height_(height), cells_(cell_count(width, height)) {}

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] Rect bounds() const { return {0, 0, width_, height_}; }

    void clear(const Cell& fill = Cell::blank()) {
        std::fill(cells_.begin(), cells_.end(), fill);
    }

    void resize(int new_width, int new_height) {
        width_ = new_width;
        height_ = new_height;
        cells_.assign(cell_count(new_width, new_height), Cell::blank());
    }

    [[nodiscard]] bool in_bounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }

    Cell& at(int x, int y) { return cells_[static_cast<size_t>(y) * width_ + x]; }
    [[nodiscard]] const Cell& at(int x, int y) const { return cells_[static_cast<size_t>(y) * width_ + x]; }

    /// Écrit `cell` à (x,y) si dans les bornes ET dans le clip courant ;
    /// no-op sinon. C'est la méthode que les widgets doivent utiliser
    /// pour peindre (contrairement à at(), qui reste un accès direct
    /// non clippé, utile pour les tests/inspections).
    void set(int x, int y, const Cell& cell) {
        if (!in_bounds(x, y)) return;
        if (!clip_stack_.empty() && !clip_stack_.back().contains({x, y})) return;
        at(x, y) = cell;
    }

    /// Empile une région de clip, intersectée avec le clip courant s'il
    /// y en a un. Un Container doit envelopper la peinture de ses
    /// enfants entre push_clip(bounds())/pop_clip() pour qu'ils ne
    /// débordent jamais de leur zone assignée.
    void push_clip(Rect r) {
        clip_stack_.push_back(clip_stack_.empty() ? r : clip_stack_.back().intersect(r));
    }

    void pop_clip() {
        if (!clip_stack_.empty()) clip_stack_.pop_back();
    }

    [[nodiscard]] Rect current_clip() const {
        return clip_stack_.empty() ? bounds() : clip_stack_.back();
    }

private:
    [[nodiscard]] static size_t cell_count(int w, int h) {
        return w > 0 && h > 0 ? static_cast<size_t>(w) * static_cast<size_t>(h) : 0;
    }

    int width_;
    int height_;
    std::vector<Cell> cells_;
    std::vector<Rect> clip_stack_;
};

/// RAII : push_clip() à la construction, pop_clip() garanti à la
/// destruction (y compris si paint() d'un enfant lève une exception).
class ClipGuard {
public:
    ClipGuard(Buffer& buffer, Rect region) : buffer_(buffer) { buffer_.push_clip(region); }
    ~ClipGuard() { buffer_.pop_clip(); }
    ClipGuard(const ClipGuard&) = delete;
    ClipGuard& operator=(const ClipGuard&) = delete;

private:
    Buffer& buffer_;
};

/// Double-buffer avec suivi des cellules modifiées (dirty tracking) :
/// les widgets peignent dans `back()`, puis `diff_and_flush()` ne
/// transmet au backend que les cellules qui ont changé depuis la
/// dernière frame, avant de swapper front/back.
class DoubleBuffer {
public:
    DoubleBuffer() = default;

    [[nodiscard]] Buffer& back() { return back_; }
    [[nodiscard]] const Buffer& back() const { return back_; }

    /// Redimensionne les deux buffers ; force un repaint complet à la
    /// prochaine diff_and_flush() (le front buffer est invalidé).
    void resize(int width, int height) {
        back_.resize(width, height);
        front_.resize(width, height);
        force_full_repaint_ = true;
    }

    [[nodiscard]] int width() const { return back_.width(); }
    [[nodiscard]] int height() const { return back_.height(); }

    /// Compare back_ à front_, n'émet draw_cell() que pour les cellules
    /// modifiées (ou toutes si un repaint complet a été forcé), puis
    /// swap et flush(). Retourne le nombre de cellules redessinées
    /// (utile pour les tests / diagnostics de performance).
    size_t diff_and_flush(ITerminalBackend& backend) {
        size_t redrawn = 0;
        for (int y = 0; y < back_.height(); ++y) {
            for (int x = 0; x < back_.width(); ++x) {
                const Cell& next = back_.at(x, y);
                if (!force_full_repaint_ && next == front_.at(x, y)) continue;
                backend.draw_cell(x, y, next);
                ++redrawn;
            }
        }
        std::swap(back_, front_);
        force_full_repaint_ = false;
        if (redrawn > 0) backend.flush();
        return redrawn;
    }

private:
    Buffer back_;
    Buffer front_;
    bool force_full_repaint_ = true;
};

} // namespace tui
