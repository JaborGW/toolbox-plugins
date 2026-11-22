#pragma once

#include <GWCA/Context/CharContext.h>
#include <GWCA/GameContainers/Array.h>

#include <optional>

inline std::optional<float> InstanceProgress()
{
    const auto character = GW::GetCharContext();
    if (!character || character->progress_bars.size() == 0) {
        return std::nullopt;
    }
    return character->progress_bars[0].progress;
}
