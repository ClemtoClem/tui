/**
 * @file CodeEditor.hpp
 * @brief Panneau d'édition de code multi-fichiers : bande d'onglets +
 * marge de numéros de ligne + TextArea coloré syntaxiquement.
 *
 * Ne charge/sauve rien sur disque lui-même (add_tab prend le texte déjà
 * lu, mark_saved() est appelé par l'hôte après écriture réussie) : ce
 * découplage laisse l'application décider de la source (fichier local,
 * buffer scratch, contenu généré...).
 *
 * Retokenisation : le fichier entier est retokenisé à chaque frappe
 * (retokenize()). Largement suffisant pour des fichiers de taille
 * ordinaire dans un éditeur terminal ; pas conçu pour des fichiers de
 * plusieurs dizaines de milliers de lignes (documenté, pas oublié).
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/SyntaxHighlight.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"
#include "TextArea.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tui {

struct CodeEditorTab {
    std::string path;
    std::string language; // clé de language_specs() ; vide = pas de coloration
    std::shared_ptr<TextArea> editor = std::make_shared<TextArea>();
    bool dirty = false;
    std::vector<std::vector<Token>> line_tokens;
};

class CodeEditor : public Widget {
public:
    size_t add_tab(const std::string& path, const std::string& language, const std::string& initial_text) {
        auto tab = std::make_shared<CodeEditorTab>();
        tab->path = path;
        tab->language = language;
        tab->editor->set_parent(this);
        tab->editor->set_show_scrollbar(true);
        tab->editor->set_accent_color(accent_color_);
        tab->editor->set_text(initial_text);

        size_t index = tabs_.size();
        tabs_.push_back(tab);
        wire_tab(index);
        wire_style_hook(*tab);
        retokenize(*tab);

        active_index_ = index;
        need_repaint();
        return index;
    }

    void close_tab(size_t i) {
        if (i >= tabs_.size()) return;
        tabs_.erase(tabs_.begin() + static_cast<long>(i));
        for (size_t j = i; j < tabs_.size(); ++j) rewire_index(j); // les lambdas capturées par indice doivent suivre le décalage
        if (tabs_.empty()) active_index_ = 0;
        else if (active_index_ >= tabs_.size()) active_index_ = tabs_.size() - 1;
        else if (i < active_index_) --active_index_;
        need_repaint();
    }

    void set_active_index(size_t i) {
        if (i >= tabs_.size() || i == active_index_) return;
        active_index_ = i;
        need_repaint();
        if (on_active_changed_) on_active_changed_(i);
    }
    [[nodiscard]] size_t active_index() const { return active_index_; }
    [[nodiscard]] size_t tab_count() const { return tabs_.size(); }
    [[nodiscard]] const std::string& path_at(size_t i) const { return tabs_[i]->path; }
    [[nodiscard]] bool is_dirty(size_t i) const { return tabs_[i]->dirty; }
    [[nodiscard]] std::shared_ptr<TextArea> editor_at(size_t i) const { return tabs_[i]->editor; }
    [[nodiscard]] std::shared_ptr<TextArea> active_editor() const {
        return tabs_.empty() ? nullptr : tabs_[active_index_]->editor;
    }

    /// À appeler par l'hôte après une écriture disque réussie.
    void mark_saved(size_t i) {
        if (i >= tabs_.size() || !tabs_[i]->dirty) return;
        tabs_[i]->dirty = false;
        need_repaint();
    }

    void set_on_tab_close_requested(std::function<void(size_t)> cb) { on_close_requested_ = std::move(cb); }
    void set_on_active_changed(std::function<void(size_t)> cb) { on_active_changed_ = std::move(cb); }
    void set_on_tab_modified(std::function<void(size_t)> cb) { on_modified_ = std::move(cb); }

    void set_colors(Color accent, Color foreground, Color muted) {
        accent_color_ = accent;
        foreground_color_ = foreground;
        muted_color_ = muted;
        for (auto& tab : tabs_) tab->editor->set_accent_color(accent_color_);
        need_repaint();
    }

    [[nodiscard]] SyntaxTheme& syntax_theme() { return syntax_theme_; }

    /// Annotation optionnelle affichée dans la marge après le numéro de
    /// ligne (ex: "a1b2c3d jdoe" pour un git-blame) ; l'hôte fournit le
    /// texte, CodeEditor ne sait rien de git. Chaîne vide = rien affiché
    /// pour cette ligne.
    void set_blame_provider(std::function<std::string(const std::string& path, int line)> fn) {
        blame_provider_ = std::move(fn);
        need_repaint();
    }
    void set_show_blame(bool show) { show_blame_ = show; need_repaint(); }
    [[nodiscard]] bool show_blame() const { return show_blame_; }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        strip_rect_ = Rect{final_rect.x, final_rect.y, final_rect.width, 1};
        Rect body{final_rect.x, final_rect.y + 1, final_rect.width, std::max(final_rect.height - 1, 0)};
        if (tabs_.empty()) return;
        auto& tab = *tabs_[active_index_];
        int gw = gutter_width(tab);
        gutter_rect_ = Rect{body.x, body.y, gw, body.height};
        Rect editor_rect{body.x + gw, body.y, std::max(body.width - gw, 0), body.height};
        tab.editor->arrange(editor_rect);
    }

    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        draw_tab_strip(buffer);
        if (tabs_.empty()) {
            draw_empty_state(buffer);
            return;
        }
        auto& tab = *tabs_[active_index_];
        draw_gutter(buffer, tab);
        if (tab.editor->visible()) tab.editor->paint(buffer);
    }

    [[nodiscard]] bool focusable() const override { return !tabs_.empty(); }

    bool on_key(const KeyEvent& e) override {
        if (tabs_.empty()) return false;
        if (e.ctrl && e.key == Key::PageDown) { set_active_index((active_index_ + 1) % tabs_.size()); return true; }
        if (e.ctrl && e.key == Key::PageUp) { set_active_index((active_index_ + tabs_.size() - 1) % tabs_.size()); return true; }
        return false;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button != MouseEvent::Button::Left || e.action != MouseEvent::Action::Press) return false;
        if (e.y != strip_rect_.y) return false;
        for (size_t i = 0; i < strip_segments_.size(); ++i) {
            const auto& seg = strip_segments_[i];
            if (e.x >= seg.close_x0 && e.x < seg.close_x1) {
                if (on_close_requested_) on_close_requested_(i);
                return true;
            }
            if (e.x >= seg.label_x0 && e.x < seg.close_x0) {
                set_active_index(i);
                return true;
            }
        }
        return true; // clic dans la bande d'onglets mais hors de tout onglet : consommé quand même
    }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        std::vector<std::shared_ptr<Widget>> result;
        if (!tabs_.empty()) result.push_back(tabs_[active_index_]->editor);
        return result;
    }

private:
    struct StripSegment {
        int label_x0, label_x1, close_x0, close_x1;
    };

    void wire_tab(size_t index) {
        tabs_[index]->editor->set_on_change([this, index] { on_tab_text_changed(index); });
    }

    /// Après close_tab(), les indices capturés par les lambdas des onglets
    /// suivants ont tous décalé de -1 : on les rebranche à leur nouvel
    /// indice plutôt que de risquer un index caduc dans la closure.
    void rewire_index(size_t new_index) { wire_tab(new_index); }

    void on_tab_text_changed(size_t index) {
        if (index >= tabs_.size()) return;
        tabs_[index]->dirty = true;
        retokenize(*tabs_[index]);
        need_repaint();
        if (on_modified_) on_modified_(index);
    }

    void retokenize(CodeEditorTab& tab) const {
        tab.line_tokens.clear();
        auto it = language_specs().find(tab.language);
        if (it == language_specs().end()) {
            tab.line_tokens.assign(tab.editor->line_count(), {});
            return;
        }
        bool in_comment = false;
        tab.line_tokens.reserve(tab.editor->line_count());
        for (size_t i = 0; i < tab.editor->line_count(); ++i) {
            tab.line_tokens.push_back(tokenize_line(tab.editor->line(i), it->second, in_comment));
        }
    }

    void wire_style_hook(CodeEditorTab& tab) const {
        CodeEditorTab* tab_ptr = &tab;
        const SyntaxTheme* theme = &syntax_theme_;
        tab.editor->set_style_hook([tab_ptr, theme](int line, int col) -> CharStyle {
            if (line < 0 || static_cast<size_t>(line) >= tab_ptr->line_tokens.size()) return {};
            for (const auto& t : tab_ptr->line_tokens[static_cast<size_t>(line)]) {
                if (col >= t.start_col && col < t.start_col + t.length) {
                    return CharStyle{TextStyle::None, theme->color(t.role)};
                }
            }
            return {};
        });
    }

    [[nodiscard]] int gutter_width(const CodeEditorTab& tab) const {
        int digits = 1;
        size_t n = tab.editor->line_count();
        while (n >= 10) { n /= 10; ++digits; }
        int width = std::max(digits + 2, 4);
        if (show_blame_ && blame_provider_) width += kBlameColumnWidth;
        return width;
    }

    [[nodiscard]] static std::string display_name(const std::string& path) {
        auto slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    void draw_tab_strip(Buffer& buffer) const {
        strip_segments_.clear();
        int x = strip_rect_.x;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            bool active = i == active_index_;
            std::string label = " " + display_name(tabs_[i]->path) + (tabs_[i]->dirty ? " *" : "") + " ";
            auto decoded = TextHelper::decode_utf8(label);
            Color fg = active ? foreground_color_ : muted_color_;
            TextStyle style = active ? TextStyle::Bold : TextStyle::None;

            int label_x0 = x;
            for (char32_t ch : decoded) {
                if (x >= strip_rect_.right()) break;
                buffer.set(x, strip_rect_.y, Cell{ch, 1, fg, Color::Default(), style});
                ++x;
            }
            int close_x0 = x;
            for (char32_t ch : std::u32string(U"× ")) {
                if (x >= strip_rect_.right()) break;
                buffer.set(x, strip_rect_.y, Cell{ch, 1, muted_color_, Color::Default(), TextStyle::None});
                ++x;
            }
            strip_segments_.push_back({label_x0, x - 2, close_x0, x});
        }
    }

    void draw_empty_state(Buffer& buffer) const {
        std::string msg = "Aucun fichier ouvert - ouvrez-en un depuis le navigateur de fichiers.";
        auto decoded = TextHelper::decode_utf8(msg);
        Rect body{bounds_.x, bounds_.y + 1, bounds_.width, std::max(bounds_.height - 1, 0)};
        if (body.height <= 0) return;
        int y = body.y + body.height / 2;
        int x = body.x + std::max((body.width - TextHelper::display_width(decoded)) / 2, 0);
        for (char32_t ch : decoded) {
            if (x >= body.right()) break;
            buffer.set(x, y, Cell{ch, 1, muted_color_, Color::Default(), TextStyle::None});
            ++x;
        }
    }

    void draw_gutter(Buffer& buffer, const CodeEditorTab& tab) const {
        if (gutter_rect_.width <= 0) return;
        bool blame_active = show_blame_ && static_cast<bool>(blame_provider_);
        int number_width = blame_active ? gutter_rect_.width - kBlameColumnWidth : gutter_rect_.width;
        int first_line = tab.editor->first_visible_line();
        int cursor_line = tab.editor->cursor().line;
        for (int row = 0; row < gutter_rect_.height; ++row) {
            int line_idx = first_line + row;
            if (static_cast<size_t>(std::max(line_idx, 0)) >= tab.editor->line_count() || line_idx < 0) continue;
            std::string num = std::to_string(line_idx + 1);
            bool is_cursor_line = line_idx == cursor_line;
            Color fg = is_cursor_line ? accent_color_ : muted_color_;
            TextStyle style = is_cursor_line ? TextStyle::Bold : TextStyle::None;
            int pad = std::max(number_width - 1 - static_cast<int>(num.size()), 0);
            int x = gutter_rect_.x + pad;
            for (char c : num) {
                if (x >= gutter_rect_.x + number_width) break;
                buffer.set(x, gutter_rect_.y + row, Cell{static_cast<char32_t>(c), 1, fg, Color::Default(), style});
                ++x;
            }
            if (blame_active) {
                std::string blame = blame_provider_(tab.path, line_idx);
                auto decoded = TextHelper::decode_utf8(" " + blame);
                auto truncated = TextHelper::truncate_to_width(decoded, kBlameColumnWidth);
                int bx = gutter_rect_.x + number_width;
                for (char32_t ch : truncated) {
                    buffer.set(bx, gutter_rect_.y + row, Cell{ch, 1, muted_color_, Color::Default(), TextStyle::None});
                    ++bx;
                }
            }
        }
    }

    static constexpr int kBlameColumnWidth = 14;

    std::vector<std::shared_ptr<CodeEditorTab>> tabs_;
    size_t active_index_ = 0;
    Rect strip_rect_;
    Rect gutter_rect_;
    mutable std::vector<StripSegment> strip_segments_;

    Color accent_color_ = Color::Default();
    Color foreground_color_ = Color::Default();
    Color muted_color_ = Color::Default();
    SyntaxTheme syntax_theme_;
    bool show_blame_ = false;
    std::function<std::string(const std::string&, int)> blame_provider_;

    std::function<void(size_t)> on_close_requested_;
    std::function<void(size_t)> on_active_changed_;
    std::function<void(size_t)> on_modified_;
};

} // namespace tui
