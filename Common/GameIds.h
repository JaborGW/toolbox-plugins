#pragma once

#include <cstdint>

namespace GameIds {
    constexpr uint32_t SkillIdCount = 3440;

    namespace ItemID {
        constexpr int GuildLord = 36648;
        constexpr int HighPriestZhang = 36649;
        constexpr int GhostlyPriest = 36650;
        constexpr int RiftWarden = 36651;
    }

    namespace ModelID {
        namespace UW {
            constexpr int WailingLord = 2376;
            constexpr int BanishedDreamRider = 2377;
            constexpr int MindbladeSpectre = 2380;
            constexpr int DeadCollector = 2382;
            constexpr int DeadThresher = 2383;
        }

        namespace FoW {
            constexpr int Banshee = 2376;
        }
    }
}
