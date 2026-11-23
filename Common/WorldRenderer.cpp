#include "WorldRenderer.h"

#include <Windows.h>
#include <d3d9.h>
#include <DirectXMath.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/Maps.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Camera.h>
#include <GWCA/GameEntities/Pathing.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/CameraMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/RenderMgr.h>

#include <Widgets/Minimap/Shaders/game_world_renderer_dotted_ps.h>
#include <Widgets/Minimap/Shaders/game_world_renderer_vs.h>

#include <mapbox/earcut.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <numbers>
#include <utility>

namespace mapbox::util {
    template <>
    struct nth<0, GW::GamePos> {
        static auto get(const GW::GamePos& t) { return t.x; }
    };

    template <>
    struct nth<1, GW::GamePos> {
        static auto get(const GW::GamePos& t) { return t.y; }
    };
}

namespace {
    constexpr float render_max_distance = 5000.f;
    constexpr float fog_factor = 0.5f;
    constexpr unsigned lerp_steps_per_line = 10;
    constexpr auto altitude_unknown = std::numeric_limits<float>::max();
    constexpr DWORD custom_vertex_fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

    std::mutex renderables_mutex;
    IDirect3DVertexShader9* vertex_shader = nullptr;
    IDirect3DPixelShader9* pixel_shader = nullptr;
    IDirect3DVertexDeclaration9* vertex_declaration = nullptr;

    struct D3DVertex {
        float x;
        float y;
        float z;
        D3DCOLOR color;
    };

    class PolyRenderable {
    public:
        PolyRenderable(std::vector<GW::GamePos> points, const unsigned int color, const bool filled, const std::optional<std::chrono::steady_clock::time_point> despawn_time) noexcept
            : map_id(GW::Map::GetMapID()), color(color), points(std::move(points)), despawn_time(despawn_time), filled(filled)
        {
        }

        ~PolyRenderable() noexcept
        {
            if (vertex_buffer) {
                vertex_buffer->Release();
            }
        }

        PolyRenderable(const PolyRenderable&) = delete;
        PolyRenderable& operator=(const PolyRenderable&) = delete;

        PolyRenderable(PolyRenderable&& other) noexcept { *this = std::move(other); }

        PolyRenderable& operator=(PolyRenderable&& other) noexcept
        {
            if (vertex_buffer && vertex_buffer != other.vertex_buffer) {
                vertex_buffer->Release();
            }
            vertex_buffer = std::exchange(other.vertex_buffer, nullptr);
            map_id = other.map_id;
            color = other.color;
            points = std::move(other.points);
            vertices = std::move(other.vertices);
            vertices_zplanes = std::move(other.vertices_zplanes);
            despawn_time = other.despawn_time;
            filled = other.filled;
            vertices_processed = other.vertices_processed;
            return *this;
        }

        void Draw(IDirect3DDevice9* device);

        GW::Constants::MapID map_id{};
        std::optional<std::chrono::steady_clock::time_point> despawn_time;

    private:
        void BuildVertices();
        bool UploadToDevice(IDirect3DDevice9* device);

        unsigned int color = 0u;
        std::vector<GW::GamePos> points;
        std::vector<D3DVertex> vertices;
        std::vector<uint32_t> vertices_zplanes;
        bool filled = false;
        unsigned int vertices_processed = 0u;
        IDirect3DVertexBuffer9* vertex_buffer = nullptr;
    };

    GW::Vec2f Lerp(const GW::Vec2f& a, const GW::Vec2f& b, const float t)
    {
        return a * t + b * (1.f - t);
    }

    std::vector<GW::GamePos> EllipsePoints(const GW::GamePos& center, const GW::Vec2f a_direction, const float a, const float b)
    {
        constexpr auto pi = std::numbers::pi_v<float>;
        constexpr size_t points_per_ellipse = 48;
        constexpr auto slice = 2.0f * pi / static_cast<float>(points_per_ellipse);

        const auto b_direction = GW::Vec2f{-a_direction.y, a_direction.x};
        const auto scaled_a = a * a_direction;
        const auto scaled_b = b * b_direction;

        std::vector<GW::GamePos> points;
        for (auto i = 0u; i < points_per_ellipse; i++) {
            const auto angle = slice * static_cast<float>(i);
            auto point = GW::Vec2f{center};
            point += std::cos(angle) * scaled_a + std::sin(angle) * scaled_b;
            points.push_back({point.x, point.y, center.zplane});
        }
        points.push_back(points.front());
        return points;
    }

    void PolyRenderable::BuildVertices()
    {
        if (filled && points.size() >= 3) {
            std::vector<GW::GamePos> lerp_points;
            for (size_t i = 0; i < points.size(); i++) {
                if (!lerp_points.empty()) {
                    for (auto j = 1u; j < lerp_steps_per_line; j++) {
                        const auto split = Lerp(points[i], points[i - 1], static_cast<float>(j) / static_cast<float>(lerp_steps_per_line));
                        lerp_points.emplace_back(split.x, split.y, points[i].zplane);
                    }
                }
                lerp_points.push_back(points[i]);
            }
            const auto indices = mapbox::earcut<unsigned>(std::vector<std::vector<GW::GamePos>>{lerp_points});
            for (const auto index : indices) {
                const auto& point = lerp_points[index];
                vertices.push_back({point.x, point.y, altitude_unknown, color});
                vertices_zplanes.push_back(point.zplane);
            }
            return;
        }
        for (size_t i = 0; i < points.size(); i++) {
            if (!vertices.empty()) {
                for (auto j = 1u; j < lerp_steps_per_line; j++) {
                    const auto split = Lerp(points[i], points[i - 1], static_cast<float>(j) / static_cast<float>(lerp_steps_per_line));
                    vertices.push_back({split.x, split.y, altitude_unknown, color});
                    vertices_zplanes.push_back(std::max({points[i].zplane, points[i - 1].zplane, points.front().zplane, points.back().zplane}));
                }
            }
            vertices.push_back({points[i].x, points[i].y, altitude_unknown, color});
            vertices_zplanes.push_back(points[i].zplane);
        }
    }

    bool PolyRenderable::UploadToDevice(IDirect3DDevice9* device)
    {
        if (vertex_buffer) {
            return true;
        }
        if (vertices.empty() || vertices_processed == vertices.size()) {
            return false;
        }
        const auto pathing_map = GW::Map::GetPathingMap();
        if (!pathing_map || pathing_map->size() == 0) {
            return false;
        }

        auto query_pos = GW::GamePos{vertices.front().x, vertices.front().y, vertices_zplanes.front()};
        const auto first_altitude = GW::Map::QueryAltitude(&query_pos, 5.f);
        ++vertices_processed;
        vertices.front().z = first_altitude;
        query_pos = GW::GamePos{vertices.back().x, vertices.back().y, vertices_zplanes.back()};
        const auto last_altitude = GW::Map::QueryAltitude(&query_pos, 5.f);
        vertices.back().z = last_altitude;
        const auto last_index = static_cast<float>(vertices.size() - 1);

        for (size_t i = vertices_processed; i < vertices.size() - 1; i++, vertices_processed++) {
            query_pos = GW::GamePos{vertices[i].x, vertices[i].y, vertices_zplanes[i]};
            auto altitude = GW::Map::QueryAltitude(&query_pos, 5.f);
            if (altitude < vertices[i].z) {
                vertices[i].z = altitude;
            }
            const auto guessed_altitude = first_altitude + (last_altitude - first_altitude) * static_cast<float>(i) / last_index;
            if (std::abs(altitude - guessed_altitude) > 20.f) {
                auto min_diff = std::abs(altitude - guessed_altitude);
                for (unsigned zplane = pathing_map->size() - 1; zplane >= 1; --zplane) {
                    query_pos = GW::GamePos{vertices[i].x, vertices[i].y, zplane};
                    altitude = GW::Map::QueryAltitude(&query_pos, 5.f);
                    const auto diff = std::abs(altitude - guessed_altitude);
                    if (diff < min_diff && altitude < vertices[i].z) {
                        min_diff = diff;
                        vertices[i].z = altitude;
                    }
                }
            }
        }
        ++vertices_processed;

        const auto byte_size = static_cast<UINT>(vertices.size() * sizeof(D3DVertex));
        if (device->CreateVertexBuffer(byte_size, D3DUSAGE_WRITEONLY, custom_vertex_fvf, D3DPOOL_MANAGED, &vertex_buffer, nullptr) != S_OK) {
            vertex_buffer = nullptr;
            return false;
        }
        void* memory = nullptr;
        if (vertex_buffer->Lock(0, byte_size, &memory, D3DLOCK_DISCARD) != S_OK || !memory) {
            vertex_buffer->Release();
            vertex_buffer = nullptr;
            return false;
        }
        std::memcpy(memory, vertices.data(), byte_size);
        vertex_buffer->Unlock();
        return true;
    }

    void PolyRenderable::Draw(IDirect3DDevice9* device)
    {
        if (vertices.empty()) {
            BuildVertices();
        }
        if (!UploadToDevice(device)) {
            return;
        }
        if (device->SetStreamSource(0, vertex_buffer, 0, sizeof(D3DVertex)) != D3D_OK) {
            return;
        }
        constexpr BOOL dotted_effect[1] = {FALSE};
        if (device->SetPixelShaderConstantB(0, dotted_effect, 1) != D3D_OK) {
            return;
        }
        if (filled) {
            device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(vertices.size() / 3));
        }
        else {
            device->DrawPrimitive(D3DPT_LINESTRIP, 0, static_cast<UINT>(vertices.size() - 1));
        }
    }

    bool ConfigurePipeline(IDirect3DDevice9* device)
    {
        if (vertex_shader && pixel_shader && vertex_declaration) {
            return true;
        }
        constexpr D3DVERTEXELEMENT9 declaration[] = {
            {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
            D3DDECL_END(),
        };
        return (vertex_declaration || device->CreateVertexDeclaration(declaration, &vertex_declaration) == D3D_OK)
            && (vertex_shader || device->CreateVertexShader(reinterpret_cast<const DWORD*>(game_world_renderer_vs), &vertex_shader) == D3D_OK)
            && (pixel_shader || device->CreatePixelShader(reinterpret_cast<const DWORD*>(game_world_renderer_dotted_ps), &pixel_shader) == D3D_OK);
    }

    bool SetTransforms(IDirect3DDevice9* device, const GW::Camera* camera)
    {
        constexpr auto view_matrix_register = 0u;
        constexpr auto projection_matrix_register = 4u;

        const DirectX::XMFLOAT3 eye = {camera->position.x, camera->position.y, camera->position.z};
        const DirectX::XMFLOAT3 target = {camera->look_at_target.x, camera->look_at_target.y, camera->look_at_target.z};
        constexpr DirectX::XMFLOAT3 up = {0.0f, 0.0f, -1.0f};
        DirectX::XMFLOAT4X4A view{};
        XMStoreFloat4x4A(&view, XMMatrixTranspose(DirectX::XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up))));
        if (device->SetVertexShaderConstantF(view_matrix_register, reinterpret_cast<const float*>(&view), 4) != D3D_OK) {
            return false;
        }

        const auto aspect_ratio = static_cast<float>(GW::Render::GetViewportWidth()) / static_cast<float>(GW::Render::GetViewportHeight());
        DirectX::XMFLOAT4X4A projection{};
        XMStoreFloat4x4A(&projection, XMMatrixTranspose(DirectX::XMMatrixPerspectiveFovLH(GW::Render::GetFieldOfView(), aspect_ratio, 0.1f, 100000.0f)));
        return device->SetVertexShaderConstantF(projection_matrix_register, reinterpret_cast<const float*>(&projection), 4) == D3D_OK;
    }

    std::vector<PolyRenderable> renderables;
    std::optional<PolyRenderable> polyline;

    // GW skips redundant state sets, so anything left changed leaks into its next frame (red water).
    class DeviceStateGuard {
    public:
        explicit DeviceStateGuard(IDirect3DDevice9* device) : device(device)
        {
            device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor_test);
            device->GetRenderState(D3DRS_STENCILENABLE, &stencil);
            device->GetVertexShaderConstantF(0, vertex_constants, 8);
            device->GetPixelShaderConstantF(0, pixel_constants, 3);
            device->GetPixelShaderConstantB(0, &pixel_bool, 1);
            device->GetVertexShader(&vertex_shader);
            device->GetPixelShader(&pixel_shader);
            device->GetVertexDeclaration(&declaration);
            device->GetFVF(&fvf);
            device->GetStreamSource(0, &stream, &stream_offset, &stream_stride);
        }

        ~DeviceStateGuard()
        {
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor_test);
            device->SetRenderState(D3DRS_STENCILENABLE, stencil);
            device->SetVertexShaderConstantF(0, vertex_constants, 8);
            device->SetPixelShaderConstantF(0, pixel_constants, 3);
            device->SetPixelShaderConstantB(0, &pixel_bool, 1);
            device->SetVertexShader(vertex_shader);
            device->SetPixelShader(pixel_shader);
            if (fvf) {
                device->SetFVF(fvf);
            }
            else {
                device->SetVertexDeclaration(declaration);
            }
            device->SetStreamSource(0, stream, stream_offset, stream_stride);
            for (auto resource : {static_cast<IUnknown*>(vertex_shader), static_cast<IUnknown*>(pixel_shader), static_cast<IUnknown*>(declaration), static_cast<IUnknown*>(stream)}) {
                if (resource) {
                    resource->Release();
                }
            }
        }

        DeviceStateGuard(const DeviceStateGuard&) = delete;
        DeviceStateGuard& operator=(const DeviceStateGuard&) = delete;

    private:
        IDirect3DDevice9* device;
        DWORD scissor_test = 0;
        DWORD stencil = 0;
        float vertex_constants[8 * 4]{};
        float pixel_constants[3 * 4]{};
        BOOL pixel_bool = FALSE;
        IDirect3DVertexShader9* vertex_shader = nullptr;
        IDirect3DPixelShader9* pixel_shader = nullptr;
        IDirect3DVertexDeclaration9* declaration = nullptr;
        DWORD fvf = 0;
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT stream_offset = 0;
        UINT stream_stride = 0;
    };
}

namespace WorldRenderer {
    void Clear()
    {
        std::scoped_lock lock{renderables_mutex};
        renderables.clear();
        polyline.reset();
    }

    void AddCircle(const GW::GamePos center, const float radius, const unsigned int color, const bool filled, const std::optional<int> ms_to_show)
    {
        AddEllipse(center, {1.f, 1.f}, radius, radius, color, filled, ms_to_show);
    }

    void AddEllipse(const GW::GamePos center, const GW::Vec2f a_direction, const float a, const float b, const unsigned int color, const bool filled, const std::optional<int> ms_to_show)
    {
        auto points = EllipsePoints(center, a_direction, a, b);
        std::optional<std::chrono::steady_clock::time_point> despawn_time;
        if (ms_to_show) {
            despawn_time = std::chrono::steady_clock::now() + std::chrono::milliseconds{*ms_to_show};
        }
        std::scoped_lock lock{renderables_mutex};
        renderables.emplace_back(std::move(points), color, filled, despawn_time);
    }

    void SetPolyline(std::vector<GW::GamePos>&& points, const unsigned int color)
    {
        std::scoped_lock lock{renderables_mutex};
        polyline.emplace(std::move(points), color, false, std::nullopt);
    }

    void ClearPolyline()
    {
        std::scoped_lock lock{renderables_mutex};
        polyline.reset();
    }

    void Draw(IDirect3DDevice9* device)
    {
        std::scoped_lock lock{renderables_mutex};
        if ((renderables.empty() && !polyline) || !device || !ConfigurePipeline(device)) {
            return;
        }
        const auto camera = GW::CameraMgr::GetCamera();
        if (!camera) {
            return;
        }
        const DeviceStateGuard state_guard{device};
        if (device->SetVertexShader(vertex_shader) != D3D_OK || device->SetPixelShader(pixel_shader) != D3D_OK || device->SetVertexDeclaration(vertex_declaration) != D3D_OK) {
            return;
        }

        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

        const float current_position[4] = {camera->look_at_target.x, camera->look_at_target.y, camera->look_at_target.z, 0.0f};
        const float max_distance[4] = {render_max_distance, 0.0f, 0.0f, 0.0f};
        const float fog_starts_at[4] = {render_max_distance - render_max_distance * fog_factor, 0.0f, 0.0f, 0.0f};
        if (SetTransforms(device, camera)
            && device->SetPixelShaderConstantF(0, current_position, 1) == D3D_OK
            && device->SetPixelShaderConstantF(1, max_distance, 1) == D3D_OK
            && device->SetPixelShaderConstantF(2, fog_starts_at, 1) == D3D_OK) {
            const auto map_id = GW::Map::GetMapID();
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(renderables, [&now](const PolyRenderable& renderable) { return renderable.despawn_time && now > *renderable.despawn_time; });
            for (auto& renderable : renderables) {
                if (renderable.map_id == map_id) {
                    renderable.Draw(device);
                }
            }
            if (polyline) {
                polyline->Draw(device);
            }
        }
    }

    void Terminate()
    {
        Clear();
        for (auto resource : {static_cast<IUnknown*>(vertex_shader), static_cast<IUnknown*>(pixel_shader), static_cast<IUnknown*>(vertex_declaration)}) {
            if (resource) {
                resource->Release();
            }
        }
        vertex_shader = nullptr;
        pixel_shader = nullptr;
        vertex_declaration = nullptr;
    }
}
