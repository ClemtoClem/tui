/**
 * @file Timer.hpp
 * @brief Minuteries pilotées explicitement par un instant "now" fourni par l'appelant.
 *
 * Aucune méthode n'appelle Clock::now() en interne : `add`/`time_until_next`/
 * `run_due` reçoivent toutes un `Clock::time_point now` explicite. Ça permet
 * aux tests de piloter le temps déterministiquement (aucun vrai sleep())
 * tandis qu'App, en usage réel, passe simplement Clock::now() à chaque appel.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace tui {

using TimerId = uint64_t;

class TimerManager {
public:
    using Clock = std::chrono::steady_clock;

    /// Programme un callback à `now + interval`. Si `repeat` est vrai,
    /// se reprogramme automatiquement pour `interval` de plus après
    /// chaque exécution (drift-free : basé sur le fire prévu, pas sur
    /// l'instant réel d'exécution).
    TimerId add(Clock::time_point now, Clock::duration interval, bool repeat, std::function<void()> callback) {
        TimerId id = next_id_++;
        timers_.push_back({id, now + interval, interval, repeat, false, std::move(callback)});
        return id;
    }

    /// Annule un timer ; no-op si l'id est inconnu ou déjà annulé.
    void cancel(TimerId id) {
        for (auto& t : timers_) {
            if (t.id == id) { t.cancelled = true; return; }
        }
    }

    /// Délai avant le prochain timer actif, ou nullopt s'il n'y en a aucun.
    [[nodiscard]] std::optional<Clock::duration> time_until_next(Clock::time_point now) const {
        std::optional<Clock::time_point> earliest;
        for (const auto& t : timers_) {
            if (t.cancelled) continue;
            if (!earliest || t.next_fire < *earliest) earliest = t.next_fire;
        }
        if (!earliest) return std::nullopt;
        auto delta = *earliest - now;
        return delta > Clock::duration::zero() ? delta : Clock::duration::zero();
    }

    /// Exécute tous les timers dont l'échéance est <= now, puis les
    /// reprogramme (repeat) ou les retire (une seule fois). Les timers
    /// annulés pendant l'exécution d'un callback ne sont pas relancés.
    void run_due(Clock::time_point now) {
        for (auto& t : timers_) {
            if (t.cancelled || t.next_fire > now) continue;
            t.callback();
            if (t.cancelled) continue; // annulé depuis son propre callback
            if (t.repeat) {
                t.next_fire += t.interval;
                if (t.next_fire <= now) t.next_fire = now + t.interval; // rattrape un gros retard sans rafale
            } else {
                t.cancelled = true;
            }
        }
        std::erase_if(timers_, [](const TimerEntry& t) { return t.cancelled; });
    }

    [[nodiscard]] size_t active_count() const {
        return static_cast<size_t>(std::count_if(timers_.begin(), timers_.end(),
            [](const TimerEntry& t) { return !t.cancelled; }));
    }

private:
    struct TimerEntry {
        TimerId id;
        Clock::time_point next_fire;
        Clock::duration interval;
        bool repeat;
        bool cancelled;
        std::function<void()> callback;
    };

    std::vector<TimerEntry> timers_;
    TimerId next_id_ = 1;
};

} // namespace tui
