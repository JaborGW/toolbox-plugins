#pragma once

#include <GWCA/GameContainers/GamePos.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Pathing.h>

struct PathPoint {
    GW::GamePos pos = {};
    const GW::PathingTrapezoid* trapezoid = nullptr;
};

struct OutcomeChances {
    float success = 0.f;
    float partial = 0.f;
    float failure = 0.f;
};

void InitializePathing();
bool IsPathingAvailable();
const GW::PathingTrapezoid* FindTrapezoid(const GW::GamePos& pos, const GW::PathingMapArray* path_map);
OutcomeChances DeathsChargePrediction(const PathPoint& player, const GW::Agent* target, const GW::PathingMapArray* path_map);
OutcomeChances ShadowOfHastePrediction(const PathPoint& player, const PathPoint& return_spot);
