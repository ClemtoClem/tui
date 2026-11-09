/* @file widget/Widget.cpp — identité CSS et boîte stylée des widgets. */

#include "tui/widget/Widget.hpp"

#include "tui/Buffer.hpp"
#include "tui/style/ColorUtil.hpp"
#include "tui/widget/BoxFrame.hpp"

#include <cxxabi.h>

#include <cstdlib>
#include <typeinfo>

namespace tui {

std::string Widget::type_name() const {
    if (type_name_cache_.empty()) {
        const char* raw = typeid(*this).name();
        int status = 0;
        char* dem = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
        std::string full = status == 0 && dem ? dem : raw;
        std::free(dem);
        // Retire les arguments de template puis l'espace de noms.
        if (auto lt = full.find('<'); lt != std::string::npos) full.erase(lt);
        if (auto sep = full.rfind("::"); sep != std::string::npos) full.erase(0, sep + 2);
        type_name_cache_ = full;
    }
    return type_name_cache_;
}

namespace {
template <class F> void for_each_word(std::string_view names, F f) {
    size_t i = 0;
    while (i < names.size()) {
        while (i < names.size() && (names[i] == ' ' || names[i] == '\t')) ++i;
        size_t j = i;
        while (j < names.size() && names[j] != ' ' && names[j] != '\t') ++j;
        if (j > i) f(names.substr(i, j - i));
        i = j;
    }
}
} // namespace

void Widget::add_class(std::string_view names) {
    bool changed = false;
    for_each_word(names, [&](std::string_view n) {
        if (!has_class(n)) {
            css_classes_.emplace_back(n);
            changed = true;
        }
    });
    if (changed) need_repaint();
}

void Widget::remove_class(std::string_view names) {
    bool changed = false;
    for_each_word(names, [&](std::string_view n) {
        auto it = std::find(css_classes_.begin(), css_classes_.end(), n);
        if (it != css_classes_.end()) {
            css_classes_.erase(it);
            changed = true;
        }
    });
    if (changed) need_repaint();
}

Widget::RowCursor Widget::row_cursor(bool focused) const {
    RowCursor c;
    auto bg = css_style_.extra_color("cursor-background");
    if (!bg) {
        c.active = focused;
        c.style = TextStyle::Reverse;
        return c;
    }
    if (!focused) {
        auto blurred = css_style_.extra_color("cursor-blurred-background");
        if (!blurred) return c;
        bg = blurred;
    }
    c.active = true;
    c.full_row = true;
    c.bg = *bg;
    c.fg = (focused ? css_style_.extra_color("cursor-color") : css_style_.extra_color("cursor-blurred-color"))
               .value_or(color::contrast_text(*bg));
    c.style = css_style_.extra_text_style(focused ? "cursor-text-style" : "cursor-blurred-text-style")
                  .value_or(focused ? TextStyle::Bold : TextStyle::None);
    return c;
}

Size Widget::measure_outer(const Constraints& c) const {
    if (css_style_.empty()) return measure(c);
    if (!visible()) return c.clamp({0, 0});
    const ComputedStyle& st = css_style_;
    Spacing in = st.insets();
    int hw = in.left + in.right, hh = in.top + in.bottom;
    Constraints ic{std::max(0, c.min_width - hw), std::max(0, c.max_width - hw), std::max(0, c.min_height - hh),
                   std::max(0, c.max_height - hh)};

    auto pick = [](std::optional<int> v, std::optional<int> lo, std::optional<int> hi) {
        if (v && lo) v = std::max(*v, *lo);
        if (v && hi) v = std::min(*v, *hi);
        return v;
    };
    std::optional<int> W = st.width ? st.width->resolve(c.max_width) : std::nullopt;
    std::optional<int> H = st.height ? st.height->resolve(c.max_height) : std::nullopt;
    W = pick(W, st.min_width, st.max_width);
    H = pick(H, st.min_height, st.max_height);
    if (W) {
        int iw = std::clamp(*W - hw, 0, ic.max_width);
        ic.min_width = ic.max_width = iw;
    } else if (st.max_width) {
        ic.max_width = std::min(ic.max_width, std::max(0, *st.max_width - hw));
        ic.min_width = std::min(ic.min_width, ic.max_width);
    }
    if (H) {
        int ih = std::clamp(*H - hh, 0, ic.max_height);
        ic.min_height = ic.max_height = ih;
    } else if (st.max_height) {
        ic.max_height = std::min(ic.max_height, std::max(0, *st.max_height - hh));
        ic.min_height = std::min(ic.min_height, ic.max_height);
    }
    Size s = measure(ic);
    s.width += hw;
    s.height += hh;
    if (W) s.width = *W;
    if (H) s.height = *H;
    if (st.min_width) s.width = std::max(s.width, *st.min_width);
    if (st.min_height) s.height = std::max(s.height, *st.min_height);
    if (st.max_width) s.width = std::min(s.width, *st.max_width);
    if (st.max_height) s.height = std::min(s.height, *st.max_height);
    return c.clamp(s);
}

void Widget::layout(Rect outer) {
    if (css_style_.empty()) {
        arrange(outer);
        return;
    }
    Spacing in = css_style_.insets();
    Rect inner{outer.x + in.left, outer.y + in.top, std::max(0, outer.width - in.left - in.right),
               std::max(0, outer.height - in.top - in.bottom)};
    arrange(inner);
}

namespace {

void draw_title(Buffer& b, Rect outer, int y, const std::string& text, Color fg, Color bg, TextStyle st,
                bool right) {
    if (text.empty() || outer.width < 5) return;
    auto decoded = TextHelper::truncate_to_width(TextHelper::decode_utf8(" " + text + " "), outer.width - 4, U"…");
    int w = TextHelper::display_width(decoded);
    int x = right ? outer.right() - 2 - w : outer.x + 2;
    for (char32_t ch : decoded) {
        int cw = std::max(1, TextHelper::display_width(std::u32string(1, ch)));
        b.set(x, y, Cell{ch, static_cast<uint8_t>(cw), fg, bg, st});
        if (cw == 2) b.set(x + 1, y, Cell::continuation());
        x += cw;
    }
}

} // namespace

void Widget::render(Buffer& buffer) const {
    if (css_style_.empty()) {
        paint(buffer);
        return;
    }
    if (!visible()) return;
    const ComputedStyle& st = css_style_;
    Rect outer = outer_bounds();
    Color bg = st.background.value_or(Color::Default());

    if (st.has_border()) {
        Color bc = st.border_color.value_or(st.color.value_or(Color::Default()));
        Color tc = st.border_title_color.value_or(bc);
        TextStyle ts = st.border_title_style.value_or(TextStyle::Bold);
        if (*st.border == BorderStyle::Blank) {
            for (int y = outer.y; y < outer.bottom(); ++y)
                for (int x = outer.x; x < outer.right(); ++x)
                    if (y == outer.y || y == outer.bottom() - 1 || x == outer.x || x == outer.right() - 1)
                        buffer.set(x, y, Cell{U' ', 1, bc, bg, TextStyle::None});
        } else {
            draw_box_frame(buffer, outer, "", *st.border, bc, tc, bg);
        }
        draw_title(buffer, outer, outer.y, border_title_, tc, bg, ts, false);
        draw_title(buffer, outer, outer.bottom() - 1, border_subtitle_, tc, bg, TextStyle::None, true);
    }

    paint(buffer);

    // Passe d'héritage : les cellules aux couleurs « par défaut » prennent
    // celles du style ; le style de texte s'applique aux widgets feuilles.
    Rect area = outer.intersect(buffer.current_clip()).intersect(buffer.bounds());
    bool leaf = children().empty();
    Color auto_fallback = st.color.value_or(Color::Default());
    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) {
            Cell& c = buffer.at(x, y);
            if (c.bg.is_default && st.background) c.bg = bg;
            if (c.fg.is_default) {
                if (st.color_auto) {
                    Color base = c.bg.is_default ? auto_fallback : color::contrast_text(c.bg);
                    if (!base.is_default && !c.bg.is_default) base = color::blend(c.bg, base, *st.color_auto);
                    c.fg = base;
                } else if (st.color) {
                    c.fg = *st.color;
                }
            }
            if (leaf && st.text_style && c.width != 0) c.style = c.style | *st.text_style;
        }
    }
}

} // namespace tui
