#include "RingTimer.h"

#include <Utils/FontLoader.h>

#include <WICTextureLoader/WICTextureLoader9.h>

#include <cmath>
#include <numbers>
#include <string>

namespace {
    ImVec2 operator+(const ImVec2 lhs, const ImVec2 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y}; }
    ImVec2 operator-(const ImVec2 lhs, const ImVec2 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y}; }
    ImVec2 operator*(const ImVec2 lhs, const float rhs) { return {lhs.x * rhs, lhs.y * rhs}; }
    ImVec2 operator+(const ImVec2 lhs, const float rhs) { return {lhs.x + rhs, lhs.y + rhs}; }

    void TextShadowed(const char* label)
    {
        const auto pos = ImGui::GetCursorPos();
        ImGui::PushStyleColor(ImGuiCol_Text, {0, 0, 0, 1});
        ImGui::SetCursorPos(pos + ImVec2{1, 1});
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SetCursorPos(pos);
        ImGui::TextUnformatted(label);
    }
}

RingTimer::RingTimer(const ImVec4 default_color, const float duration_seconds, const Icon icon)
    : default_color(default_color), duration_seconds(duration_seconds), icon(icon), color(default_color)
{
}

void RingTimer::LoadTexture(IDirect3DDevice9* device)
{
    if (!device || texture || texture_loading_failed) {
        return;
    }
    if (DirectX::CreateWICTextureFromMemoryEx(device, icon.png.data(), icon.png.size(), 0, 0, D3DPOOL_MANAGED, DirectX::WIC_LOADER_DEFAULT, &texture) != D3D_OK) {
        texture = nullptr;
        texture_loading_failed = true;
    }
}

void RingTimer::ReleaseTexture()
{
    if (texture) {
        texture->Release();
        texture = nullptr;
    }
    texture_loading_failed = false;
}

void RingTimer::Draw(IDirect3DDevice9* device, const float elapsed_portion)
{
    LoadTexture(device);

    const auto thickness = Thickness();
    const auto pos = ImGui::GetCursorScreenPos();
    const auto size = ImVec2(radius * 2, radius * 2);

    ImGui::Dummy(size);

    const auto draw_list = ImGui::GetWindowDrawList();
    draw_list->PathClear();

    constexpr auto segment_count = 64;
    constexpr auto pi = std::numbers::pi;
    constexpr auto phi_step_size = 2 * pi / segment_count;

    const auto center = pos + (size + thickness) * 0.5f;
    const auto point_on_circle = [&](const double phi, const float circle_radius) {
        return ImVec2(center.x + static_cast<float>(std::cos(-phi - pi / 2)) * circle_radius, center.y + static_cast<float>(std::sin(-phi - pi / 2)) * circle_radius);
    };

    if (show_background) {
        for (double phi = 0; phi < 2 * pi; phi += phi_step_size) {
            draw_list->PathLineTo(point_on_circle(phi, radius + 0.4f * thickness));
        }
        draw_list->PathFillConvex(ImGui::ColorConvertFloat4ToU32({0.15f, 0.15f, 0.15f, 0.8f}));
        draw_list->PathClear();
    }

    if (show_circle) {
        for (double phi = 0; phi < 2 * pi * elapsed_portion; phi += phi_step_size) {
            draw_list->PathLineTo(point_on_circle(phi, radius));
        }
        draw_list->PathLineTo(point_on_circle(2 * pi * elapsed_portion, radius));
        draw_list->PathStroke(ImGui::ColorConvertFloat4ToU32(color), false, thickness);
    }

    const auto content_center = (size + thickness) * 0.5f;
    const auto draw_icon = show_icon && texture;
    const auto image_size = icon.sizes[image_size_index];
    auto icon_top = content_center.y - image_size.y * icon.centering;

    if (show_text) {
        const auto deciseconds = static_cast<int>(duration_seconds * 10 * (1.f - elapsed_portion));
        const auto text = std::to_string(deciseconds / 10) + "." + std::to_string(deciseconds % 10);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushFont(FontLoader::GetFont(), static_cast<float>(FontLoader::font_sizes[font_size_index + 3]));
        const auto text_size = ImGui::CalcTextSize(text.c_str());
        const auto block_height = draw_icon ? image_size.y + text_size.y : text_size.y;
        icon_top = content_center.y - block_height * 0.5f;

        ImGui::SetCursorPos(ImVec2(content_center.x - text_size.x * 0.5f, content_center.y + block_height * 0.5f - text_size.y));
        if (show_background) {
            ImGui::TextUnformatted(text.c_str());
        }
        else {
            TextShadowed(text.c_str());
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    if (draw_icon) {
        ImGui::SetCursorPos(ImVec2(content_center.x - image_size.x * icon.centering, icon_top));
        ImGui::Image(reinterpret_cast<ImTextureID>(texture), image_size);
    }
}

void RingTimer::DrawSettings()
{
    ImGui::DragFloat("Indicator radius", &radius, 0.1f, 32.f, 150.f, "%1.f");
    ImGui::ColorEdit3("Border color", reinterpret_cast<float*>(&color));
    constexpr const char* size_names[] = {"Small", "Medium", "Large", "Very large"};
    ImGui::Combo("Icon size", &image_size_index, size_names, 4);
    ImGui::Combo("Text size", &font_size_index, size_names, 3);
    ImGui::Text("Show:");
    ImGui::SameLine();
    ImGui::Checkbox("Icon", &show_icon);
    ImGui::SameLine();
    ImGui::Checkbox("Duration", &show_text);
    ImGui::SameLine();
    ImGui::Checkbox("Ring", &show_circle);
    ImGui::SameLine();
    ImGui::Checkbox("Background", &show_background);
}

bool RingTimer::HasDefaultSettings() const
{
    return radius == default_radius && color.x == default_color.x && color.y == default_color.y && color.z == default_color.z
        && show_icon && !show_text && show_circle && show_background
        && font_size_index == 1 && image_size_index == 1;
}
