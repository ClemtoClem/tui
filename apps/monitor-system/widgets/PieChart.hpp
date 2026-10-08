#pragma once

// Camembert trame pour terminal + legende. Aucune bibliotheque graphique
// disponible : chaque cellule de la zone circulaire est testee par son
// angle polaire (corrige du ratio ~1:2 largeur/hauteur d'une cellule de
// terminal, pour obtenir un disque visuellement rond plutot qu'ovale) et
// coloree selon la tranche a laquelle appartient cet angle.

#include <tui/Buffer.hpp>
#include <tui/core/Text.hpp>
#include <tui/widget/Widget.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sysmon {

namespace detail {
inline constexpr double kTwoPi = 6.28318530717958647692;
} // namespace detail

struct PieSlice {
    std::string label;
    double value = 0.0;
    std::string value_label; // deja formate par l'appelant (ex: "12.3 Go")
    tui::Color color;
};

class PieChart : public tui::Widget {
public:
    void set_slices(std::vector<PieSlice> slices) { slices_ = std::move(slices); need_repaint(); }
    void set_center_text(std::string text) { center_text_ = std::move(text); need_repaint(); }

    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override {
        int h = std::max(c.min_height, std::min(9, c.max_height));
        return c.clamp({c.max_width, h});
    }

    void paint(tui::Buffer& buffer) const override {
        tui::ClipGuard clip(buffer, bounds_);
        if (bounds_.width <= 4 || bounds_.height <= 2) return;

        int legend_width = std::clamp(bounds_.width / 2, 16, 30);
        int pie_area_width = std::max(bounds_.width - legend_width - 1, 4);
        int pie_area_height = bounds_.height;

        int rows_diameter = std::max(std::min(pie_area_width / 2, pie_area_height), 1);
        int cols_diameter = rows_diameter * 2;
        int pie_x = bounds_.x + std::max((pie_area_width - cols_diameter) / 2, 0);
        int pie_y = bounds_.y + std::max((pie_area_height - rows_diameter) / 2, 0);

        double total = 0.0;
        for (const auto& s : slices_) total += std::max(s.value, 0.0);

        if (total > 0.0 && rows_diameter >= 2) {
            draw_pie(buffer, pie_x, pie_y, cols_diameter, rows_diameter, total);
        }

        int legend_x = bounds_.x + pie_area_width + 1;
        draw_legend(buffer, legend_x, bounds_.y, legend_width, total);
    }

private:
    void draw_pie(tui::Buffer& buffer, int pie_x, int pie_y, int cols_diameter, int rows_diameter,
                  double total) const {
        double radius_cols = cols_diameter / 2.0;
        double radius_rows = rows_diameter / 2.0;

        std::vector<double> cum_start(slices_.size());
        std::vector<double> cum_end(slices_.size());
        double acc = 0.0;
        for (size_t i = 0; i < slices_.size(); ++i) {
            cum_start[i] = acc;
            acc += std::max(slices_[i].value, 0.0) / total;
            cum_end[i] = acc;
        }

        for (int y = 0; y < rows_diameter; ++y) {
            for (int x = 0; x < cols_diameter; ++x) {
                double nx = (x + 0.5 - radius_cols) / radius_cols;
                double ny = (y + 0.5 - radius_rows) / radius_rows;
                if (nx * nx + ny * ny > 1.0) continue;

                double angle = std::atan2(ny, nx);
                double fraction = angle / detail::kTwoPi;
                if (fraction < 0.0) fraction += 1.0;

                for (size_t i = 0; i < slices_.size(); ++i) {
                    if (fraction >= cum_start[i] && fraction < cum_end[i]) {
                        buffer.set(pie_x + x, pie_y + y,
                                    tui::Cell{U'█', 1, slices_[i].color, tui::Color::Default(), tui::TextStyle::None});
                        break;
                    }
                }
            }
        }
    }

    void put_text(tui::Buffer& buffer, int x, int y, int width, const std::string& text, tui::Color fg,
                  tui::TextStyle style = tui::TextStyle::None) const {
        auto decoded = tui::TextHelper::decode_utf8(text);
        auto truncated = tui::TextHelper::truncate_to_width(decoded, width, U"…");
        int cx = x;
        for (char32_t ch : truncated) {
            buffer.set(cx, y, tui::Cell{ch, 1, fg, tui::Color::Default(), style});
            ++cx;
        }
    }

    void draw_legend(tui::Buffer& buffer, int x, int y, int width, double total) const {
        if (width <= 0) return;
        int row = y;
        if (!center_text_.empty() && row < bounds_.y + bounds_.height) {
            put_text(buffer, x, row, width, center_text_, tui::Color::Default(), tui::TextStyle::Bold);
            row += 2;
        }
        for (const auto& s : slices_) {
            if (row >= bounds_.y + bounds_.height) break;
            buffer.set(x, row, tui::Cell{U'■', 1, s.color, tui::Color::Default(), tui::TextStyle::None});
            double pct = total > 0.0 ? 100.0 * std::max(s.value, 0.0) / total : 0.0;
            char pct_buf[16];
            std::snprintf(pct_buf, sizeof(pct_buf), " (%.0f%%)", pct);
            std::string line = " " + s.label + "  " + s.value_label + pct_buf;
            put_text(buffer, x + 1, row, width - 1, line, tui::Color::Default());
            ++row;
        }
    }

    std::vector<PieSlice> slices_;
    std::string center_text_;
};

} // namespace sysmon
