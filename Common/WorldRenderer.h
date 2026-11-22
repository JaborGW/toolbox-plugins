#pragma once

#include <GWCA/GameContainers/GamePos.h>

#include <optional>
#include <vector>

struct IDirect3DDevice9;

namespace WorldRenderer {
    void Clear();
    void AddCircle(GW::GamePos center, float radius, unsigned int color, bool filled, std::optional<int> ms_to_show = std::nullopt);
    void AddEllipse(GW::GamePos center, GW::Vec2f a_direction, float a, float b, unsigned int color, bool filled, std::optional<int> ms_to_show = std::nullopt);
    void SetPolyline(std::vector<GW::GamePos>&& polyline, unsigned int color);
    void ClearPolyline();
    void Draw(IDirect3DDevice9* device);
    void Terminate();
}
