#include <tui/terminal/AnsiScreen.hpp>

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace tui {

namespace {

[[nodiscard]] std::vector<int> parse_semicolon_ints(const std::string& s) {
    std::vector<int> result;
    size_t start = 0;
    while (start <= s.size()) {
        size_t semi = s.find(';', start);
        std::string tok = s.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        result.push_back(tok.empty() ? 0 : std::atoi(tok.c_str()));
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return result;
}

[[nodiscard]] TextStyle clear_style(TextStyle flags, TextStyle bit) {
    return static_cast<TextStyle>(static_cast<uint8_t>(flags) & ~static_cast<uint8_t>(bit));
}

[[nodiscard]] Color ansi_color(int idx, bool bright) {
    static constexpr Color kNormal[8] = {
        {0, 0, 0}, {205, 0, 0}, {0, 205, 0}, {205, 205, 0}, {0, 0, 238}, {205, 0, 205}, {0, 205, 205}, {229, 229, 229},
    };
    static constexpr Color kBright[8] = {
        {127, 127, 127}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
        {92, 92, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
    };
    idx = std::clamp(idx, 0, 7);
    return bright ? kBright[idx] : kNormal[idx];
}

[[nodiscard]] Color color_256(int n) {
    n = std::clamp(n, 0, 255);
    if (n < 8) return ansi_color(n, false);
    if (n < 16) return ansi_color(n - 8, true);
    if (n < 232) {
        int v = n - 16;
        int r = v / 36, g = (v / 6) % 6, b = v % 6;
        auto lvl = [](int x) -> uint8_t { return x == 0 ? 0 : static_cast<uint8_t>(55 + x * 40); };
        return Color{lvl(r), lvl(g), lvl(b)};
    }
    auto gray = static_cast<uint8_t>(8 + (n - 232) * 10);
    return Color{gray, gray, gray};
}

/// Consomme `38;5;N` / `38;2;R;G;B` (et l'équivalent 48;...) à partir de
/// nums[i] (qui vaut 38 ou 48) ; retourne l'indice du dernier token
/// consommé (la boucle appelante fera ++i par-dessus).
size_t parse_extended_color(const std::vector<int>& nums, size_t i, Color& out) {
    if (i + 1 >= nums.size()) return i;
    int mode = nums[i + 1];
    if (mode == 5 && i + 2 < nums.size()) {
        out = color_256(nums[i + 2]);
        return i + 2;
    }
    if (mode == 2 && i + 4 < nums.size()) {
        out = Color{
            static_cast<uint8_t>(std::clamp(nums[i + 2], 0, 255)),
            static_cast<uint8_t>(std::clamp(nums[i + 3], 0, 255)),
            static_cast<uint8_t>(std::clamp(nums[i + 4], 0, 255)),
        };
        return i + 4;
    }
    return i + 1;
}

} // namespace

void AnsiScreen::resize(int cols, int rows) {
    cols_ = std::max(cols, 1);
    rows_ = std::max(rows, 1);
    grid_.resize(cols_, rows_);
    cursor_row_ = std::clamp(cursor_row_, 0, rows_ - 1);
    cursor_col_ = std::clamp(cursor_col_, 0, cols_ - 1);
}

void AnsiScreen::feed(std::string_view bytes) {
    buffer_.append(bytes);
    size_t pos = 0;

    while (pos < buffer_.size()) {
        unsigned char c = static_cast<unsigned char>(buffer_[pos]);

        if (state_ == ParseState::Csi) {
            csi_buffer_ += static_cast<char>(c);
            ++pos;
            if (c >= 0x40 && c <= 0x7E) {
                handle_csi(csi_buffer_.substr(0, csi_buffer_.size() - 1), static_cast<char>(c));
                csi_buffer_.clear();
                state_ = ParseState::Normal;
            } else if (csi_buffer_.size() > 64) {
                csi_buffer_.clear();
                state_ = ParseState::Normal;
            }
            continue;
        }
        if (state_ == ParseState::Escape) {
            ++pos;
            if (c == '[') { state_ = ParseState::Csi; csi_buffer_.clear(); }
            else if (c == ']') { state_ = ParseState::Osc; }
            else { state_ = ParseState::Normal; } // ESC + autre (charset, DECSC...) : ignoré
            continue;
        }
        if (state_ == ParseState::Osc) {
            ++pos;
            if (c == 0x07) {
                state_ = ParseState::Normal;
            } else if (c == 0x1b && pos < buffer_.size() && static_cast<unsigned char>(buffer_[pos]) == '\\') {
                ++pos;
                state_ = ParseState::Normal;
            }
            continue;
        }

        // ParseState::Normal
        if (c == 0x1b) { state_ = ParseState::Escape; ++pos; continue; }
        if (c == '\r') { cursor_col_ = 0; ++pos; continue; }
        if (c == '\n') { line_feed(); ++pos; continue; }
        if (c == '\b') { cursor_col_ = std::max(cursor_col_ - 1, 0); ++pos; continue; }
        if (c == '\t') { cursor_col_ = std::min(((cursor_col_ / 8) + 1) * 8, cols_ - 1); ++pos; continue; }
        if (c < 0x20) { ++pos; continue; } // BEL et autres contrôles : ignorés

        int len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (pos + static_cast<size_t>(len) > buffer_.size()) break; // séquence UTF-8 incomplète : attend le prochain feed()

        auto decoded = TextHelper::decode_utf8(buffer_.substr(pos, static_cast<size_t>(len)));
        put_char(decoded.empty() ? U'?' : decoded[0]);
        pos += static_cast<size_t>(len);
    }
    buffer_.erase(0, pos);
}

void AnsiScreen::put_char(char32_t ch) {
    if (cursor_col_ >= cols_) { cursor_col_ = 0; line_feed(); }
    grid_.set(cursor_col_, cursor_row_, Cell{ch, 1, cur_fg_, cur_bg_, cur_style_});
    ++cursor_col_;
}

void AnsiScreen::line_feed() {
    if (cursor_row_ + 1 >= rows_) scroll_up();
    else ++cursor_row_;
}

void AnsiScreen::scroll_up() {
    for (int y = 1; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) grid_.at(x, y - 1) = grid_.at(x, y);
    }
    for (int x = 0; x < cols_; ++x) grid_.at(x, rows_ - 1) = Cell::blank();
}

void AnsiScreen::erase_in_line(int mode) {
    int from = 0, to = cols_;
    if (mode == 0) from = cursor_col_;
    else if (mode == 1) to = std::min(cursor_col_ + 1, cols_);
    for (int x = from; x < to; ++x) grid_.at(x, cursor_row_) = Cell::blank();
}

void AnsiScreen::erase_in_display(int mode) {
    if (mode == 2 || mode == 3) {
        for (int y = 0; y < rows_; ++y)
            for (int x = 0; x < cols_; ++x) grid_.at(x, y) = Cell::blank();
        return;
    }
    if (mode == 0) {
        erase_in_line(0);
        for (int y = cursor_row_ + 1; y < rows_; ++y)
            for (int x = 0; x < cols_; ++x) grid_.at(x, y) = Cell::blank();
    } else if (mode == 1) {
        erase_in_line(1);
        for (int y = 0; y < cursor_row_; ++y)
            for (int x = 0; x < cols_; ++x) grid_.at(x, y) = Cell::blank();
    }
}

void AnsiScreen::handle_csi(const std::string& params, char final_byte) {
    if (!params.empty() && (params[0] == '?' || params[0] == '>' || params[0] == '=')) return; // modes privés DEC : ignorés

    auto nums = parse_semicolon_ints(params);
    auto arg = [&](size_t i, int def) { return i < nums.size() && nums[i] > 0 ? nums[i] : def; };

    switch (final_byte) {
        case 'A': cursor_row_ = std::max(cursor_row_ - arg(0, 1), 0); break;
        case 'B': cursor_row_ = std::min(cursor_row_ + arg(0, 1), rows_ - 1); break;
        case 'C': cursor_col_ = std::min(cursor_col_ + arg(0, 1), cols_ - 1); break;
        case 'D': cursor_col_ = std::max(cursor_col_ - arg(0, 1), 0); break;
        case 'H': case 'f':
            cursor_row_ = std::clamp(arg(0, 1) - 1, 0, rows_ - 1);
            cursor_col_ = std::clamp(arg(1, 1) - 1, 0, cols_ - 1);
            break;
        case 'G': cursor_col_ = std::clamp(arg(0, 1) - 1, 0, cols_ - 1); break;
        case 'd': cursor_row_ = std::clamp(arg(0, 1) - 1, 0, rows_ - 1); break;
        case 'K': erase_in_line(nums.empty() ? 0 : nums[0]); break;
        case 'J': erase_in_display(nums.empty() ? 0 : nums[0]); break;
        case 'm': handle_sgr(params); break;
        default: break; // reconnu mais non pertinent (régions de défilement, etc.) : ignoré sans casser le flux
    }
}

void AnsiScreen::handle_sgr(const std::string& params) {
    auto nums = parse_semicolon_ints(params);
    if (nums.empty()) nums.push_back(0);

    for (size_t i = 0; i < nums.size(); ++i) {
        int n = nums[i];
        if (n == 0) { cur_fg_ = Color::Default(); cur_bg_ = Color::Default(); cur_style_ = TextStyle::None; }
        else if (n == 1) cur_style_ = cur_style_ | TextStyle::Bold;
        else if (n == 2) cur_style_ = cur_style_ | TextStyle::Dim;
        else if (n == 3) cur_style_ = cur_style_ | TextStyle::Italic;
        else if (n == 4) cur_style_ = cur_style_ | TextStyle::Underline;
        else if (n == 5) cur_style_ = cur_style_ | TextStyle::Blink;
        else if (n == 7) cur_style_ = cur_style_ | TextStyle::Reverse;
        else if (n == 9) cur_style_ = cur_style_ | TextStyle::Strikethrough;
        else if (n == 22) cur_style_ = clear_style(clear_style(cur_style_, TextStyle::Bold), TextStyle::Dim);
        else if (n == 23) cur_style_ = clear_style(cur_style_, TextStyle::Italic);
        else if (n == 24) cur_style_ = clear_style(cur_style_, TextStyle::Underline);
        else if (n == 27) cur_style_ = clear_style(cur_style_, TextStyle::Reverse);
        else if (n >= 30 && n <= 37) cur_fg_ = ansi_color(n - 30, false);
        else if (n == 38 && i + 1 < nums.size()) i = parse_extended_color(nums, i, cur_fg_);
        else if (n == 39) cur_fg_ = Color::Default();
        else if (n >= 40 && n <= 47) cur_bg_ = ansi_color(n - 40, false);
        else if (n == 48 && i + 1 < nums.size()) i = parse_extended_color(nums, i, cur_bg_);
        else if (n == 49) cur_bg_ = Color::Default();
        else if (n >= 90 && n <= 97) cur_fg_ = ansi_color(n - 90, true);
        else if (n >= 100 && n <= 107) cur_bg_ = ansi_color(n - 100, true);
    }
}

} // namespace tui
