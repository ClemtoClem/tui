#pragma once

// Graphique temporel multi-series pour terminal (courbes CPU/memoire/
// reseau/disque). Deux modes d'echelle verticale :
//  - FixedPercent : 0-100%, grille fixe tous les 25% (utilise pour CPU
//    et Memoire/Swap, comme demande).
//  - AutoScale : l'echelle se recalcule sur le maximum courant (utilise
//    pour Reseau/Disque, en octets/s).
// Technique de tracé : une colonne = un echantillon (le timer de l'app
// pousse un echantillon par seconde), segment vertical entre la ligne
// precedente et la ligne courante pour simuler une courbe continue
// (technique sparkline/ttyplot standard) ; pas de canevas braille pour
// rester simple.

#include <tui/Buffer.hpp>
#include <tui/core/Text.hpp>
#include <tui/widget/Widget.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace sysmon {

enum class YMode { FixedPercent, AutoScale };

class LineChart : public tui::Widget {
public:
    explicit LineChart(YMode mode, int window_seconds = 60, int tick_seconds = 10)
        : mode_(mode), window_(std::max(window_seconds, 1)), tick_(std::max(tick_seconds, 1)) {}

    int add_series(std::string name, tui::Color color) {
        series_.push_back(Series{std::move(name), color, {}});
        return static_cast<int>(series_.size()) - 1;
    }

    void push_sample(int series_index, double value) {
        if (series_index < 0 || series_index >= static_cast<int>(series_.size())) return;
        auto& samples = series_[static_cast<size_t>(series_index)].samples;
        samples.push_back(value);
        while (static_cast<int>(samples.size()) > window_) samples.pop_front();
        need_repaint();
    }

    void set_y_formatter(std::function<std::string(double)> fmt) { formatter_ = std::move(fmt); }

    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override {
        int h = std::max(c.min_height, std::min(10, c.max_height));
        return c.clamp({c.max_width, h});
    }

    void paint(tui::Buffer& buffer) const override {
        tui::ClipGuard clip(buffer, bounds_);
        if (bounds_.width <= 2 || bounds_.height <= 2) return;

        double y_max = compute_y_max();
        std::string top_label = format_value(y_max);
        int left_margin = std::clamp(static_cast<int>(top_label.size()) + 1, 5, 10);

        int plot_x = bounds_.x + left_margin;
        int plot_y = bounds_.y;
        int plot_w = bounds_.width - left_margin;
        int plot_h = bounds_.height - 1; // derniere ligne = graduations X
        if (plot_w <= 0 || plot_h <= 0) return;

        draw_grid(buffer, plot_x, plot_y, plot_w, plot_h, y_max, left_margin);
        draw_x_axis(buffer, plot_x, plot_y, plot_w, plot_h);
        draw_series(buffer, plot_x, plot_y, plot_w, plot_h, y_max);
    }

private:
    struct Series {
        std::string name;
        tui::Color color;
        std::deque<double> samples;
    };

    [[nodiscard]] double compute_y_max() const {
        if (mode_ == YMode::FixedPercent) return 100.0;
        double max_v = 0.0;
        for (const auto& s : series_) {
            for (double v : s.samples) max_v = std::max(max_v, v);
        }
        if (max_v <= 0.0) return 1.0;
        double magnitude = std::pow(10.0, std::floor(std::log10(max_v)));
        double normalized = max_v / magnitude;
        double nice = normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0;
        return nice * magnitude;
    }

    [[nodiscard]] std::string format_value(double v) const {
        if (mode_ == YMode::FixedPercent) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(v + 0.5));
            return buf;
        }
        if (formatter_) return formatter_(v);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }

    void put_text(tui::Buffer& buffer, int x, int y, int width, const std::string& text, tui::Color fg) const {
        auto decoded = tui::TextHelper::decode_utf8(text);
        auto truncated = tui::TextHelper::truncate_to_width(decoded, width);
        int cx = x;
        for (char32_t ch : truncated) {
            buffer.set(cx, y, tui::Cell{ch, 1, fg, tui::Color::Default(), tui::TextStyle::None});
            ++cx;
        }
    }

    void draw_grid(tui::Buffer& buffer, int plot_x, int plot_y, int plot_w, int plot_h, double y_max,
                    int left_margin) const {
        static constexpr double kFractions[] = {0.0, 0.25, 0.5, 0.75, 1.0};
        tui::Color grid_color = tui::Color{60, 60, 60};
        tui::Color label_color = tui::Color::Gray();

        for (double f : kFractions) {
            int row = plot_h - 1 - static_cast<int>(std::lround(f * (plot_h - 1)));
            row = std::clamp(row, 0, plot_h - 1);
            for (int x = 0; x < plot_w; ++x) {
                buffer.set(plot_x + x, plot_y + row, tui::Cell{U'·', 1, grid_color, tui::Color::Default(), tui::TextStyle::None});
            }
            std::string label = format_value(f * y_max);
            int label_x = plot_x - left_margin;
            int pad = std::max(left_margin - 1 - static_cast<int>(label.size()), 0);
            put_text(buffer, label_x + pad, plot_y + row, left_margin - 1, label, label_color);
        }
    }

    void draw_x_axis(tui::Buffer& buffer, int plot_x, int plot_y, int plot_w, int plot_h) const {
        int y = plot_y + plot_h;
        tui::Color label_color = tui::Color::Gray();
        for (int seconds_ago = 0; seconds_ago <= window_; seconds_ago += tick_) {
            int col = plot_w - 1 - seconds_ago;
            if (col < 0) break;
            std::string label = std::to_string(seconds_ago) + "s";
            int lx = plot_x + col - static_cast<int>(label.size()) + 1;
            if (lx < plot_x) lx = plot_x + col;
            put_text(buffer, lx, y, static_cast<int>(label.size()), label, label_color);
        }
    }

    void draw_series(tui::Buffer& buffer, int plot_x, int plot_y, int plot_w, int plot_h, double y_max) const {
        for (const auto& s : series_) {
            int count = std::min<int>(plot_w, static_cast<int>(s.samples.size()));
            if (count <= 0) continue;
            int start_col = plot_w - count;
            int prev_row = -1;
            for (int i = 0; i < count; ++i) {
                double value = s.samples[s.samples.size() - static_cast<size_t>(count) + static_cast<size_t>(i)];
                double frac = y_max > 0.0 ? std::clamp(value / y_max, 0.0, 1.0) : 0.0;
                int row = plot_h - 1 - static_cast<int>(std::lround(frac * (plot_h - 1)));
                row = std::clamp(row, 0, plot_h - 1);
                int col = start_col + i;

                if (prev_row >= 0) {
                    int lo = std::min(prev_row, row);
                    int hi = std::max(prev_row, row);
                    for (int r = lo; r <= hi; ++r) {
                        buffer.set(plot_x + col, plot_y + r, tui::Cell{U'│', 1, s.color, tui::Color::Default(), tui::TextStyle::None});
                    }
                } else {
                    buffer.set(plot_x + col, plot_y + row, tui::Cell{U'│', 1, s.color, tui::Color::Default(), tui::TextStyle::None});
                }
                prev_row = row;
            }
        }
    }

    YMode mode_;
    int window_;
    int tick_;
    std::vector<Series> series_;
    std::function<std::string(double)> formatter_;
};

} // namespace sysmon
