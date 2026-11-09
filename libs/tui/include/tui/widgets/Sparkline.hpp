/**
 * @file Sparkline.hpp
 * @brief Mini-graphique en barres « ▁▂▃▄▅▆▇█ » d'une série de valeurs,
 * rééchantillonnée à la largeur disponible.
 * CSS : propriétés « min-color » et « max-color » (dégradé selon la hauteur).
 */

#pragma once

#include "../style/ColorUtil.hpp"
#include "../widget/Widget.hpp"
#include "../Buffer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tui {

class Sparkline : public Widget {
public:
    enum class Summary { Max, Mean, Min };

    explicit Sparkline(std::vector<double> data = {}) : data_(std::move(data)) {}

    void set_data(std::vector<double> d) { data_ = std::move(d); need_repaint(); }
    void push(double v, size_t keep = 0) {
        data_.push_back(v);
        if (keep && data_.size() > keep) data_.erase(data_.begin(), data_.end() - static_cast<long>(keep));
        need_repaint();
    }
    [[nodiscard]] const std::vector<double>& data() const { return data_; }
    void set_summary(Summary s) { summary_ = s; need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, 1}); }

    /// Valeurs agrégées par colonne (exposé pour les tests).
    [[nodiscard]] std::vector<double> buckets(int width) const {
        std::vector<double> out;
        if (data_.empty() || width <= 0) return out;
        size_t n = data_.size();
        int cols = std::min<int>(width, static_cast<int>(n));
        for (int i = 0; i < cols; ++i) {
            size_t a = n * static_cast<size_t>(i) / static_cast<size_t>(cols);
            size_t z = std::max(a + 1, n * static_cast<size_t>(i + 1) / static_cast<size_t>(cols));
            double acc = summary_ == Summary::Min ? data_[a] : summary_ == Summary::Max ? data_[a] : 0.0;
            for (size_t k = a; k < z; ++k) {
                if (summary_ == Summary::Max) acc = std::max(acc, data_[k]);
                else if (summary_ == Summary::Min) acc = std::min(acc, data_[k]);
                else acc += data_[k];
            }
            if (summary_ == Summary::Mean) acc /= static_cast<double>(z - a);
            out.push_back(acc);
        }
        return out;
    }

    void paint(Buffer& b) const override {
        static constexpr char32_t bars[] = {U' ', U'▁', U'▂', U'▃', U'▄', U'▅', U'▆', U'▇', U'█'};
        if (bounds_.empty()) return;
        auto vals = buckets(bounds_.width);
        if (vals.empty()) return;
        auto [mn_it, mx_it] = std::minmax_element(vals.begin(), vals.end());
        double mn = std::min(0.0, *mn_it), mx = *mx_it;
        double span = mx - mn > 0 ? mx - mn : 1.0;
        auto lo = style().extra_color("min-color"), hi = style().extra_color("max-color");
        int h = bounds_.height;
        for (size_t i = 0; i < vals.size(); ++i) {
            double t = (vals[i] - mn) / span;
            int units = static_cast<int>(std::lround(t * h * 8));
            Color fg = Color::Default();
            if (lo && hi) fg = color::blend(*lo, *hi, t);
            else if (hi) fg = *hi;
            for (int row = 0; row < h; ++row) {
                int level = std::clamp(units - row * 8, 0, 8);
                if (row > 0 && level == 0) break;
                b.set(bounds_.x + static_cast<int>(i), bounds_.bottom() - 1 - row,
                      Cell{bars[level == 0 && row == 0 ? 1 : level], 1, fg, Color::Default(), TextStyle::None});
            }
        }
    }

private:
    std::vector<double> data_;
    Summary summary_ = Summary::Max;
};

} // namespace tui
