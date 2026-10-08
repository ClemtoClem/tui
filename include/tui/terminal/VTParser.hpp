/**
 * @file VTParser.hpp
 * @brief Machine à états décodant un flux d'octets terminal en Event.
 *
 * Utilisation par le backend Posix : `feed()` à chaque lecture de
 * `read(STDIN_FILENO, ...)`, puis `pump()` pour récupérer les
 * événements complets disponibles. Si `has_pending_escape()` est vrai
 * et qu'aucun octet supplémentaire n'arrive avant un court délai (le
 * backend gère ce timing via son `poll()`), le backend doit appeler
 * `flush_pending_escape()` pour interpréter l'ESC isolé comme la
 * touche Echap plutôt que comme le début d'une séquence CSI/SS3.
 */

#pragma once

#include "../core/Event.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tui {

class VTParser {
public:
    /// Ajoute des octets bruts reçus du terminal au buffer interne.
    void feed(std::string_view bytes);

    /// Extrait tous les événements actuellement décodables ; les
    /// séquences incomplètes restent bufferisées pour le prochain feed().
    [[nodiscard]] std::vector<Event> pump();

    /// Vrai si le buffer contient un ESC isolé en attente d'un éventuel
    /// octet suivant (début possible de séquence CSI/SS3).
    [[nodiscard]] bool has_pending_escape() const;

    /// Force l'interprétation d'un ESC en attente comme touche Echap.
    /// À appeler par le backend après un court timeout sans nouvel octet.
    [[nodiscard]] std::optional<Event> flush_pending_escape();

private:
    // Tente de décoder UNE séquence en tête de buffer_ à partir de `pos`.
    // Retourne l'événement et avance `pos` si succès ; sur séquence
    // incomplète retourne std::nullopt sans avancer `pos` (le buffer
    // restant est conservé pour le prochain feed()) ; sur séquence
    // reconnue comme invalide, avance `pos` d'au moins 1 pour ne pas
    // boucler indéfiniment.
    struct DecodeResult {
        std::optional<Event> event;
        size_t consumed = 0;
        bool incomplete = false;
    };

    [[nodiscard]] DecodeResult decode_at(size_t pos) const;
    [[nodiscard]] DecodeResult decode_escape(size_t pos) const;
    [[nodiscard]] DecodeResult decode_csi(size_t pos) const;
    [[nodiscard]] DecodeResult decode_ss3(size_t pos) const;
    [[nodiscard]] DecodeResult decode_utf8_char(size_t pos) const;
    [[nodiscard]] DecodeResult decode_control(size_t pos) const;

    std::string buffer_;
    bool paste_mode_ = false;
    std::string paste_buffer_;
};

} // namespace tui
