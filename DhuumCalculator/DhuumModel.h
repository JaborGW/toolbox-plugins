#pragma once

#include <algorithm>
#include <deque>
#include <optional>

namespace DhuumModel {
    constexpr float dhuum_max_hp = 80'240.f;
    constexpr float damage_target_fraction = 0.25f;

    constexpr double ghost_cast_seconds = 0.7125;
    constexpr double rest_per_ghost_cast = 0.0021;
    constexpr double fury_damage = 250.0;

    constexpr double RestRecharge(const bool quickening_zephyr) { return quickening_zephyr ? 2.0 : 4.0; }
    constexpr double FuryRecharge(const bool quickening_zephyr) { return quickening_zephyr ? 3.0 : 5.0; }
    constexpr double GhostRestRate(const bool quickening_zephyr) { return rest_per_ghost_cast / (ghost_cast_seconds + RestRecharge(quickening_zephyr)); }
    constexpr double GhostFuryDps(const bool quickening_zephyr) { return fury_damage / (ghost_cast_seconds + FuryRecharge(quickening_zephyr)); }

    constexpr unsigned GhostsForPlayers(const unsigned players, const bool quickening_zephyr) { return quickening_zephyr && players > 0 ? players - 1 : players; }

    struct Sample {
        double time;
        double value;
    };

    inline std::optional<double> Slope(const std::deque<Sample>& samples, const double min_span)
    {
        if (samples.size() < 2 || samples.back().time - samples.front().time < min_span) {
            return std::nullopt;
        }
        double mean_t = 0;
        double mean_v = 0;
        for (const auto& [t, v] : samples) {
            mean_t += t;
            mean_v += v;
        }
        mean_t /= static_cast<double>(samples.size());
        mean_v /= static_cast<double>(samples.size());
        double covariance = 0;
        double variance = 0;
        for (const auto& [t, v] : samples) {
            covariance += (t - mean_t) * (v - mean_v);
            variance += (t - mean_t) * (t - mean_t);
        }
        if (variance <= 0) {
            return std::nullopt;
        }
        return covariance / variance;
    }

    struct Switch {
        enum class Kind { At, Now, NotNeeded, RestWouldStall };
        Kind kind;
        double seconds_from_now = 0;
        double rest_finish_from_now = 0;
        double damage_finish_from_now = 0;
    };

    // rest_rate in bar fraction per second, dps in HP per second; both measured before the switch.
    inline Switch PlanSwitch(const double rest_left, const double damage_left, const double rest_rate, const double dps, const unsigned switchers, const bool quickening_zephyr)
    {
        const double rest_after = rest_rate - switchers * GhostRestRate(quickening_zephyr);
        const double dps_after = dps + switchers * GhostFuryDps(quickening_zephyr);
        if (dps > 0 && damage_left / dps <= rest_left / rest_rate) {
            return {Switch::Kind::NotNeeded, 0, rest_left / rest_rate, damage_left / dps};
        }
        if (rest_after <= 0) {
            return {Switch::Kind::RestWouldStall, 0, 0, damage_left / dps_after};
        }
        const double seconds = (damage_left * rest_after - rest_left * dps_after) / (dps * rest_after - rest_rate * dps_after);
        if (seconds <= 0) {
            return {Switch::Kind::Now, 0, rest_left / rest_after, damage_left / dps_after};
        }
        const double finish = seconds + (rest_left - rest_rate * seconds) / rest_after;
        return {Switch::Kind::At, seconds, finish, finish};
    }
}
