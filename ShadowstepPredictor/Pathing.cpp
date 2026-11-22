#include <GamePatterns.h>
#include "Pathing.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/Utilities/Scanner.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace {
    using FindPath_pt = void(__cdecl*)(PathPoint* start, PathPoint* goal, float range, uint32_t max_count, uint32_t* count, PathPoint* path);
    FindPath_pt FindPath_Func = nullptr;

    constexpr uint32_t pathing_count = 10;
    constexpr float pathing_range = 10'000.f;
    constexpr auto sample_count = 16;

    enum class PathingResult { CanPath, CanPartiallyPath, CannotPath, Unknown };

    float Cross(const GW::Vec2f& lhs, const GW::Vec2f& rhs)
    {
        return lhs.x * rhs.y - lhs.y * rhs.x;
    }

    PathingResult CanPathTo(const PathPoint& from, const PathPoint& to)
    {
        if (!FindPath_Func) {
            return PathingResult::Unknown;
        }
        auto start = from;
        auto end = to;
        std::array<PathPoint, pathing_count> path;
        auto count = pathing_count;
        FindPath_Func(&start, &end, pathing_range, count, &count, path.data());

        if (count == 1 && GW::GetSquareDistance(path[0].pos, end.pos) > 10) {
            return PathingResult::CannotPath;
        }
        if (GW::GetSquareDistance(start.pos, path[0].pos) > GW::Constants::SqrRange::Compass) {
            return PathingResult::CanPath;
        }
        if (GW::GetSquareDistance(start.pos, end.pos) > GW::Constants::SqrRange::Compass) {
            return PathingResult::CanPartiallyPath;
        }
        return PathingResult::CanPath;
    }

    bool IsOnTrapezoid(const GW::GamePos& p, const GW::PathingTrapezoid* trapezoid)
    {
        if (!trapezoid) {
            return false;
        }
        const auto a = GW::Vec2f{trapezoid->XTL, trapezoid->YT};
        const auto b = GW::Vec2f{trapezoid->XBL, trapezoid->YB};
        const auto c = GW::Vec2f{trapezoid->XBR, trapezoid->YB};
        const auto d = GW::Vec2f{trapezoid->XTR, trapezoid->YT};

        constexpr float tolerance = 2.0f;
        if (a.y < p.y || b.y > p.y) {
            return false;
        }
        if (b.x > p.x && a.x > p.x) {
            return false;
        }
        if (c.x < p.x && d.x < p.x) {
            return false;
        }
        const auto point = GW::Vec2f{p};
        return Cross(b - a, a - point) <= tolerance && Cross(d - c, c - point) <= tolerance;
    }

    const GW::PathingTrapezoid* FindContainingNeighbour(const GW::GamePos& pos, const GW::PathingTrapezoid* trapezoid)
    {
        if (!trapezoid) {
            return nullptr;
        }
        if (IsOnTrapezoid(pos, trapezoid)) {
            return trapezoid;
        }
        for (const auto neighbour : trapezoid->adjacent) {
            if (IsOnTrapezoid(pos, neighbour)) {
                return neighbour;
            }
        }
        return nullptr;
    }

    GW::GamePos OffsetPosition(GW::GamePos center, const float distance, const uint8_t direction)
    {
        const auto phi = direction * std::numbers::pi_v<float> / sample_count;
        center.y += distance * std::sin(phi);
        center.x += distance * std::cos(phi);
        return center;
    }
}

void InitializePathing()
{
    FindPath_Func = reinterpret_cast<FindPath_pt>(GamePatterns::Find(GamePatterns::FindPath));
}

const GW::PathingTrapezoid* FindTrapezoid(const GW::GamePos& pos, const GW::PathingMapArray* path_map)
{
    for (const auto& map : *path_map) {
        for (uint32_t i = 0u; i < map.trapezoid_count; ++i) {
            if (IsOnTrapezoid(pos, &map.trapezoids[i])) {
                return &map.trapezoids[i];
            }
        }
    }
    return nullptr;
}

OutcomeChances DeathsChargePrediction(const PathPoint& player, const GW::Agent* target, const GW::PathingMapArray* path_map)
{
    if (!target) {
        return {};
    }
    const auto target_trapezoid = FindTrapezoid(target->pos, path_map);
    if (!target_trapezoid) {
        return {};
    }

    constexpr float landing_offset = 100.f;
    constexpr auto sample_weight = 1.f / sample_count;
    OutcomeChances chances;
    for (uint8_t direction = 0u; direction < sample_count; ++direction) {
        GW::GamePos pos{};
        const GW::PathingTrapezoid* trapezoid = nullptr;
        for (auto offset = landing_offset; !trapezoid; offset /= 2) {
            pos = OffsetPosition(target->pos, offset, direction);
            trapezoid = FindContainingNeighbour(pos, target_trapezoid);
        }
        switch (CanPathTo(player, {GW::GamePos{pos.x, pos.y, target->pos.zplane}, trapezoid})) {
            case PathingResult::CanPath:
                chances.success += sample_weight;
                break;
            case PathingResult::CanPartiallyPath:
                chances.partial += sample_weight;
                break;
            case PathingResult::CannotPath:
                chances.failure += sample_weight;
                break;
            case PathingResult::Unknown:
                break;
        }
    }
    return chances;
}

OutcomeChances ShadowOfHastePrediction(const PathPoint& player, const PathPoint& return_spot)
{
    switch (CanPathTo(player, return_spot)) {
        case PathingResult::CanPath:
            return {.success = 1.f};
        case PathingResult::CanPartiallyPath:
            return {.partial = 1.f};
        case PathingResult::CannotPath:
            return {.failure = 1.f};
        case PathingResult::Unknown:
            return {.failure = 1.f};
    }
    return {};
}
