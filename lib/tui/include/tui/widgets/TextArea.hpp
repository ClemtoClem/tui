/**
 * @file TextArea.hpp
 * @brief Éditeur de texte multi-ligne : curseur/sélection, undo/redo, défilement.
 *
 * Modèle de buffer : un std::u32string par ligne (pas de rope/gap
 * buffer). Suffisant pour la Phase 1 ; si une future phase (éditeur de
 * code) doit gérer de très gros fichiers, c'est le point à revisiter.
 *
 * Explicitement hors-scope ici (documenté, pas oublié) :
 * - Mouvement de curseur conscient des graphem clusters Unicode
 *   (emoji ZWJ...) - le niveau codepoint est le compromis retenu.
 * - Coloration syntaxique - le hook `style_hook_` existe pour qu'une
 *   phase ultérieure (éditeur de code) puisse la brancher, mais rien
 *   ici ne l'implémente.
 *
 * Multicurseur (secondary_cursors_) : curseurs additionnels sans
 * sélection propre, pour taper le même texte à plusieurs endroits à la
 * fois (Alt+clic ou Ctrl+Alt+Haut/Bas pour en ajouter, Echap pour tout
 * effacer). Portée volontairement limitée :
 * - Seul le curseur PRINCIPAL (selection_) porte une sélection ;
 *   taper avec une sélection active alors que des curseurs secondaires
 *   existent ignore cette sélection plutôt que de la supprimer.
 * - Toute touche de navigation (flèches, Home/End, PageUp/Down) sauf
 *   Ctrl+Alt+Haut/Bas efface les curseurs secondaires et ne déplace que
 *   le principal - pas de "déplacer tous les curseurs en parallèle".
 * - Undo/redo n'historise pas l'édition multicurseur comme un seul
 *   groupe : chaque curseur pousse sa propre commande, donc annuler une
 *   frappe tapée à N endroits demande N Ctrl+Z. Documenté, pas un bug.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Scrollbar.hpp"
#include "../widget/Widget.hpp"
#include "SelectionState.hpp"
#include "UndoRedoHistory.hpp"

#include <functional>
#include <string>
#include <vector>

namespace tui {

/// Style d'un caractère individuel retourné par TextArea::StyleHook :
/// attributs (gras/souligné/...) + couleur de premier plan, pour que la
/// coloration syntaxique (branchée via set_style_hook) puisse réellement
/// colorer le texte, pas seulement le mettre en gras/souligné.
struct CharStyle {
    TextStyle style = TextStyle::None;
    Color fg = Color::Default();
};

class TextArea : public Widget {
public:
    using Pos = SelectionState::Pos;
    using StyleHook = std::function<CharStyle(int line, int col)>;

    void set_text(const std::string& text);
    [[nodiscard]] std::string text() const;

    void set_on_change(std::function<void()> cb) { on_change_ = std::move(cb); }

    /// Lecture seule : navigation/sélection/copie restent actives (utile
    /// pour un visualiseur de diff/log/blame réutilisant TextArea), toute
    /// frappe qui muterait le buffer est ignorée.
    void set_read_only(bool read_only) { read_only_ = read_only; }
    [[nodiscard]] bool read_only() const { return read_only_; }

    /// Consulté en tout premier dans on_key(), avant tout traitement
    /// interne : renvoie true pour "consommé" (TextArea n'y touche plus
    /// ensuite). Permet à l'application hôte de brancher des raccourcis
    /// globaux (ex: Ctrl+S pour sauvegarder) sans que TextArea en
    /// connaisse le sens - même esprit que style_hook_ pour la coloration.
    void set_key_intercept(std::function<bool(const KeyEvent&)> hook) { key_intercept_ = std::move(hook); }

    /// Appelé par paint() pour chaque cellule si défini ; permet à une
    /// phase ultérieure (éditeur de code) de brancher une coloration
    /// syntaxique sans que TextArea ait à l'implémenter.
    void set_style_hook(StyleHook hook) { style_hook_ = std::move(hook); }

    [[nodiscard]] Size measure(const Constraints& c) const override;
    void arrange(Rect final_rect) override;
    void paint(Buffer& buffer) const override;

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override;
    bool on_mouse(const MouseEvent& e) override;

    [[nodiscard]] Pos cursor() const { return selection_.cursor; }
    [[nodiscard]] bool has_selection() const { return !selection_.empty(); }
    [[nodiscard]] std::string selected_text() const;
    std::string cut_selection();

    [[nodiscard]] bool can_undo() const { return history_.can_undo(); }
    [[nodiscard]] bool can_redo() const { return history_.can_redo(); }
    void undo();
    void redo();

    [[nodiscard]] size_t line_count() const { return lines_.size(); }
    [[nodiscard]] const std::u32string& line(size_t i) const { return lines_[i]; }
    [[nodiscard]] int first_visible_line() const { return scroll_line_; }

    /// Ajoute un curseur secondaire (clampé, ignoré s'il coïncide avec un
    /// curseur existant) - voir la note multicurseur en tête de fichier.
    void add_cursor_at(Pos p);
    void clear_secondary_cursors();
    [[nodiscard]] const std::vector<Pos>& secondary_cursors() const { return secondary_cursors_; }
    [[nodiscard]] bool has_multiple_cursors() const { return !secondary_cursors_.empty(); }

    /// Réserve la dernière colonne pour une piste de défilement vertical
    /// cliquable, comme ListView::set_show_scrollbar().
    void set_show_scrollbar(bool show) { show_scrollbar_ = show; need_repaint(); }
    void set_accent_color(Color color) { accent_color_ = color; need_repaint(); }

private:
    void notify_changed();
    void insert_text_at_cursor(std::u32string text, bool allow_coalesce);
    void delete_selection();

    void insert_text_at_all_cursors(const std::u32string& text);
    void backspace_at_all_cursors();
    void delete_at_all_cursors();

    void erase_range(Pos start, Pos end);           // mutation brute, sans historiser
    void insert_raw(Pos pos, const std::u32string& text); // idem
    [[nodiscard]] std::u32string text_in_range(Pos start, Pos end) const;

    void move_cursor(Pos new_pos, bool extend_selection);
    [[nodiscard]] Pos clamp_pos(Pos p) const;
    [[nodiscard]] Pos move_left(Pos p) const;
    [[nodiscard]] Pos move_right(Pos p) const;
    [[nodiscard]] Pos move_word_left(Pos p) const;
    [[nodiscard]] Pos move_word_right(Pos p) const;
    void ensure_cursor_visible();
    void scroll_lines_by(int delta);
    [[nodiscard]] int content_width() const;

    void apply_edit_command_inverse(const EditCommand& cmd);
    void apply_edit_command_forward(const EditCommand& cmd);

    std::vector<std::u32string> lines_{std::u32string()};
    SelectionState selection_;
    std::vector<Pos> secondary_cursors_;
    UndoRedoHistory history_;
    StyleHook style_hook_;
    std::function<bool(const KeyEvent&)> key_intercept_;
    std::function<void()> on_change_;
    bool focused_ = false;
    bool read_only_ = false;
    bool show_scrollbar_ = false;
    Color accent_color_ = Color::Default();
    int scroll_line_ = 0;
    int scroll_col_ = 0;
};

} // namespace tui
