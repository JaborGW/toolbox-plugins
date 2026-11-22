#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace PluginBackups {
    constexpr size_t max_backups_per_plugin = 9;

    struct Backup {
        std::filesystem::path path;
        std::filesystem::file_time_type last_write;
        std::uintmax_t size = 0;
    };

    inline std::vector<Backup> List(const std::filesystem::path& backup_folder, const std::filesystem::path& extension)
    {
        std::vector<Backup> backups;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(backup_folder, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != extension) {
                continue;
            }
            backups.push_back({entry.path(), entry.last_write_time(ec), entry.file_size(ec)});
        }
        std::ranges::sort(backups, std::ranges::greater{}, &Backup::last_write);
        return backups;
    }

    inline bool SameContent(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        std::error_code ec;
        if (std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec) || ec) {
            return false;
        }
        std::ifstream file_a(a, std::ios::binary);
        std::ifstream file_b(b, std::ios::binary);
        return std::equal(std::istreambuf_iterator<char>(file_a), {}, std::istreambuf_iterator<char>(file_b));
    }

    inline void Save(const std::filesystem::path& settings_file, const std::filesystem::path& backup_folder)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(settings_file, ec)) {
            return;
        }
        std::filesystem::create_directories(backup_folder, ec);
        const auto extension = settings_file.extension();
        const auto backups = List(backup_folder, extension);
        if (!backups.empty() && SameContent(backups.front().path, settings_file)) {
            return;
        }
        if (backups.size() >= max_backups_per_plugin) {
            std::filesystem::remove(backups.back().path, ec);
        }
        for (auto index = 0;; ++index) {
            auto target = backup_folder / (std::to_wstring(index) + extension.wstring());
            if (!std::filesystem::exists(target, ec)) {
                std::filesystem::copy_file(settings_file, target, ec);
                return;
            }
        }
    }

    inline bool Restore(const Backup& backup, const std::filesystem::path& settings_file)
    {
        std::error_code ec;
        std::filesystem::copy_file(backup.path, settings_file, std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }

    inline std::string FormatLocalTime(const std::filesystem::file_time_type time)
    {
        const auto as_time_t = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(time)));
        std::tm local{};
        localtime_s(&local, &as_time_t);
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
        return buffer;
    }
}
