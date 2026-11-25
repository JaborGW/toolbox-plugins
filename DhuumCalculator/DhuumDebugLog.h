#pragma once

#ifndef NDEBUG

#include <filesystem>

namespace DhuumDebugLog {
    void Initialize();
    void SetFolder(const std::filesystem::path& folder);
    void Terminate();
    void Update();
}

#endif
