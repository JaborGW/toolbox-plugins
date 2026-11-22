#pragma once

#include <imgui.h>

#include <array>
#include <cstdint>
#include <span>

struct IDirect3DDevice9;
struct IDirect3DTexture9;

class RingTimer {
public:
    struct Icon {
        std::span<const uint8_t> png;
        std::array<ImVec2, 4> sizes;
        float centering;
    };

    RingTimer(ImVec4 default_color, float duration_seconds, Icon icon);

    void Draw(IDirect3DDevice9* device, float elapsed_portion);
    void DrawSettings();
    void ReleaseTexture();

    [[nodiscard]] float Radius() const { return radius; }
    [[nodiscard]] float Thickness() const { return radius / 5; }
    [[nodiscard]] bool HasDefaultSettings() const;

    template <typename Visitor>
    void VisitSettings(Visitor&& visit)
    {
        visit("radius", radius);
        visit("BorderColorR", color.x);
        visit("BorderColorG", color.y);
        visit("BorderColorB", color.z);
        visit("showIcon", show_icon);
        visit("showText", show_text);
        visit("showCircle", show_circle);
        visit("showBackground", show_background);
        visit("fontSizeIndex", font_size_index);
        visit("imageSizeIndex", image_size_index);
    }

private:
    static constexpr float default_radius = 50.f;

    void LoadTexture(IDirect3DDevice9* device);

    ImVec4 default_color;
    float duration_seconds;
    Icon icon;

    float radius = default_radius;
    ImVec4 color;
    bool show_icon = true;
    bool show_text = false;
    bool show_circle = true;
    bool show_background = true;
    int font_size_index = 1;
    int image_size_index = 1;

    IDirect3DTexture9* texture = nullptr;
    bool texture_loading_failed = false;
};
