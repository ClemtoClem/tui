#include <tui/terminal/VTParser.hpp>
#include <tui/core/Text.hpp>

#include <charconv>
#include <optional>

namespace tui {

namespace {

struct CsiModifier {
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
};

CsiModifier decode_modifier(long m) {
    CsiModifier mod;
    if (m <= 1) return mod;
    long bits = m - 1;
    mod.shift = (bits & 1) != 0;
    mod.alt = (bits & 2) != 0;
    mod.ctrl = (bits & 4) != 0;
    return mod;
}

std::optional<long> parse_long(std::string_view s) {
    if (s.empty()) return std::nullopt;
    long value = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), value);
    if (res.ec != std::errc{}) return std::nullopt;
    return value;
}

std::vector<std::string_view> split_params(std::string_view params) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= params.size()) {
        auto pos = params.find(';', start);
        if (pos == std::string_view::npos) {
            parts.push_back(params.substr(start));
            break;
        }
        parts.push_back(params.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::optional<Key> csi_letter_to_key(char final_byte) {
    switch (final_byte) {
        case 'A': return Key::Up;
        case 'B': return Key::Down;
        case 'C': return Key::Right;
        case 'D': return Key::Left;
        case 'H': return Key::Home;
        case 'F': return Key::End;
        case 'Z': return Key::BackTab;
        default: return std::nullopt;
    }
}

std::optional<Key> csi_tilde_code_to_key(long code) {
    switch (code) {
        case 1: case 7: return Key::Home;
        case 2: return Key::Insert;
        case 3: return Key::Delete;
        case 4: case 8: return Key::End;
        case 5: return Key::PageUp;
        case 6: return Key::PageDown;
        case 11: return Key::F1;
        case 12: return Key::F2;
        case 13: return Key::F3;
        case 14: return Key::F4;
        case 15: return Key::F5;
        case 17: return Key::F6;
        case 18: return Key::F7;
        case 19: return Key::F8;
        case 20: return Key::F9;
        case 21: return Key::F10;
        case 23: return Key::F11;
        case 24: return Key::F12;
        default: return std::nullopt;
    }
}

std::optional<Key> ss3_final_to_key(char final_byte) {
    switch (final_byte) {
        case 'P': return Key::F1;
        case 'Q': return Key::F2;
        case 'R': return Key::F3;
        case 'S': return Key::F4;
        case 'A': return Key::Up;
        case 'B': return Key::Down;
        case 'C': return Key::Right;
        case 'D': return Key::Left;
        case 'H': return Key::Home;
        case 'F': return Key::End;
        default: return std::nullopt;
    }
}

} // namespace

void VTParser::feed(std::string_view bytes) {
    buffer_.append(bytes);
}

bool VTParser::has_pending_escape() const {
    return !buffer_.empty() && static_cast<unsigned char>(buffer_[0]) == 0x1B;
}

std::optional<Event> VTParser::flush_pending_escape() {
    if (!has_pending_escape()) return std::nullopt;
    buffer_.erase(0, 1);
    return KeyEvent{Key::Escape};
}

std::vector<Event> VTParser::pump() {
    std::vector<Event> events;

    while (true) {
        if (paste_mode_) {
            static constexpr std::string_view kTerminator = "\x1b[201~";
            auto term = buffer_.find(kTerminator);
            if (term == std::string::npos) {
                // Conserve les derniers octets au cas où le terminateur
                // serait scindé entre deux feed(); déverse le reste dans
                // le buffer de collage en cours.
                if (buffer_.size() > kTerminator.size() - 1) {
                    size_t keep = kTerminator.size() - 1;
                    paste_buffer_.append(buffer_, 0, buffer_.size() - keep);
                    buffer_.erase(0, buffer_.size() - keep);
                }
                break;
            }
            paste_buffer_.append(buffer_, 0, term);
            buffer_.erase(0, term + kTerminator.size());
            events.push_back(PasteEvent{std::move(paste_buffer_)});
            paste_buffer_.clear();
            paste_mode_ = false;
            continue;
        }

        if (buffer_.empty()) break;

        static constexpr std::string_view kPasteStart = "\x1b[200~";
        if (buffer_.compare(0, kPasteStart.size(), kPasteStart) == 0) {
            buffer_.erase(0, kPasteStart.size());
            paste_mode_ = true;
            continue;
        }

        DecodeResult result = decode_at(0);
        if (result.incomplete) break;
        if (result.consumed == 0) break; // filet de sécurité anti-boucle infinie

        if (result.event) events.push_back(*result.event);
        buffer_.erase(0, result.consumed);
    }

    return events;
}

VTParser::DecodeResult VTParser::decode_at(size_t pos) const {
    unsigned char c = static_cast<unsigned char>(buffer_[pos]);

    if (c == 0x1B) return decode_escape(pos);
    if (c < 0x20 || c == 0x7F) return decode_control(pos);
    return decode_utf8_char(pos);
}

VTParser::DecodeResult VTParser::decode_escape(size_t pos) const {
    if (pos + 1 >= buffer_.size()) return {std::nullopt, 0, true};

    char next = buffer_[pos + 1];
    if (next == '[') return decode_csi(pos);
    if (next == 'O') return decode_ss3(pos);

    // ESC suivi d'un caractère ordinaire : convention Meta/Alt+touche.
    DecodeResult inner = decode_at(pos + 1);
    if (inner.incomplete) return {std::nullopt, 0, true};
    if (inner.consumed == 0) return {KeyEvent{Key::Escape}, 1, false};

    DecodeResult result;
    result.consumed = 1 + inner.consumed;
    if (inner.event) {
        Event ev = *inner.event;
        if (auto* key = std::get_if<KeyEvent>(&ev)) {
            key->alt = true;
        }
        result.event = ev;
    }
    return result;
}

VTParser::DecodeResult VTParser::decode_csi(size_t pos) const {
    // buffer_[pos] == ESC, buffer_[pos+1] == '['
    size_t i = pos + 2;
    while (i < buffer_.size()) {
        unsigned char c = static_cast<unsigned char>(buffer_[i]);
        if (c >= 0x30 && c <= 0x3F) { ++i; continue; } // paramètres
        if (c >= 0x20 && c <= 0x2F) { ++i; continue; } // intermédiaires (rare)
        break;
    }
    if (i >= buffer_.size()) return {std::nullopt, 0, true};

    unsigned char final_byte = static_cast<unsigned char>(buffer_[i]);
    if (final_byte < 0x40 || final_byte > 0x7E) {
        // Octet final invalide : on avale la séquence pour ne pas boucler.
        return {std::nullopt, i - pos + 1, false};
    }

    std::string_view params(buffer_.data() + pos + 2, i - (pos + 2));
    size_t consumed = i - pos + 1;

    if (!params.empty() && params.front() == '<') {
        // Souris SGR : ESC [ < Cb ; Cx ; Cy (M|m)
        auto parts = split_params(params.substr(1));
        if (parts.size() != 3) return {std::nullopt, consumed, false};
        auto cb = parse_long(parts[0]);
        auto cx = parse_long(parts[1]);
        auto cy = parse_long(parts[2]);
        if (!cb || !cx || !cy) return {std::nullopt, consumed, false};

        MouseEvent me;
        me.x = static_cast<int>(*cx) - 1;
        me.y = static_cast<int>(*cy) - 1;
        bool motion = (*cb & 32) != 0;
        bool wheel = (*cb & 64) != 0;
        int btn_code = static_cast<int>(*cb & 3);
        me.shift = (*cb & 4) != 0;
        me.alt = (*cb & 8) != 0;
        me.ctrl = (*cb & 16) != 0;

        if (wheel) {
            me.button = (btn_code == 0) ? MouseEvent::Button::WheelUp : MouseEvent::Button::WheelDown;
            me.action = MouseEvent::Action::Press;
        } else {
            me.button = btn_code == 0 ? MouseEvent::Button::Left
                      : btn_code == 1 ? MouseEvent::Button::Middle
                      : btn_code == 2 ? MouseEvent::Button::Right
                                      : MouseEvent::Button::None;
            if (final_byte == 'm') {
                me.action = MouseEvent::Action::Release;
            } else {
                me.action = motion ? MouseEvent::Action::Drag : MouseEvent::Action::Press;
            }
        }
        return {Event{me}, consumed, false};
    }

    auto parts = split_params(params);

    if (final_byte == '~') {
        auto code = parts.empty() ? std::nullopt : parse_long(parts[0]);
        if (!code) return {std::nullopt, consumed, false};
        auto key = csi_tilde_code_to_key(*code);
        if (!key) return {std::nullopt, consumed, false};

        CsiModifier mod;
        if (parts.size() >= 2) {
            if (auto m = parse_long(parts[1])) mod = decode_modifier(*m);
        }
        KeyEvent ke{*key, 0, mod.shift, mod.ctrl, mod.alt};
        return {Event{ke}, consumed, false};
    }

    if (auto key = csi_letter_to_key(static_cast<char>(final_byte))) {
        CsiModifier mod;
        if (parts.size() >= 2) {
            if (auto m = parse_long(parts[1])) mod = decode_modifier(*m);
        }
        KeyEvent ke{*key, 0, mod.shift, mod.ctrl, mod.alt};
        return {Event{ke}, consumed, false};
    }

    // Séquence CSI reconnue syntaxiquement mais sémantiquement inconnue.
    return {std::nullopt, consumed, false};
}

VTParser::DecodeResult VTParser::decode_ss3(size_t pos) const {
    // buffer_[pos] == ESC, buffer_[pos+1] == 'O'
    if (pos + 2 >= buffer_.size()) return {std::nullopt, 0, true};
    char final_byte = buffer_[pos + 2];
    size_t consumed = 3;
    if (auto key = ss3_final_to_key(final_byte)) {
        return {Event{KeyEvent{*key, 0, false, false, false}}, consumed, false};
    }
    return {std::nullopt, consumed, false};
}

VTParser::DecodeResult VTParser::decode_control(size_t pos) const {
    unsigned char c = static_cast<unsigned char>(buffer_[pos]);

    if (c == '\r' || c == '\n') return {Event{KeyEvent{Key::Enter}}, 1, false};
    if (c == '\t') return {Event{KeyEvent{Key::Tab}}, 1, false};
    if (c == 0x7F || c == 0x08) return {Event{KeyEvent{Key::Backspace}}, 1, false};

    if (c >= 1 && c <= 26) {
        // Ctrl+A..Ctrl+Z (Ctrl+I=Tab et Ctrl+M=Enter déjà couverts ci-dessus)
        char letter = static_cast<char>('a' + (c - 1));
        return {Event{KeyEvent{Key::Char, static_cast<char32_t>(letter), false, true, false}}, 1, false};
    }

    return {Event{KeyEvent{Key::Unknown, c, false, false, false}}, 1, false};
}

VTParser::DecodeResult VTParser::decode_utf8_char(size_t pos) const {
    unsigned char c = static_cast<unsigned char>(buffer_[pos]);
    size_t len = 1;
    if ((c & 0x80) == 0x00) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    else len = 1; // octet de tête invalide, traité comme 1 octet isolé

    if (pos + len > buffer_.size()) return {std::nullopt, 0, true};

    std::u32string decoded = TextHelper::decode_utf8(std::string_view(buffer_.data() + pos, len));
    char32_t cp = decoded.empty() ? 0xFFFD : decoded[0];
    return {Event{KeyEvent{Key::Char, cp, false, false, false}}, len, false};
}

} // namespace tui
