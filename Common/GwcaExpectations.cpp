#include <Windows.h>

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <functional>
#include <string>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Constants/ItemIDs.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Packets/StoC.h>

#include <utility>

// The plugins rely on these numeric values, not just the names; a GWCA update has renumbered UI messages before.
namespace {
    using GW::UI::UIMessage;
    static_assert(std::to_underlying(UIMessage::kAgentSkillInterrupted) == 0x10000026);
    static_assert(std::to_underlying(UIMessage::kMessage_0x1000005d) == 0x1000005D);
    static_assert(std::to_underlying(UIMessage::kAgentSkillStartedCast) == 0x10000027);
    static_assert(std::to_underlying(UIMessage::kSkillActivated) == 0x1000005B);
    static_assert(std::to_underlying(UIMessage::kSendUseItem) == 0x30000008);
    static_assert(std::to_underlying(UIMessage::kDialogBody) == 0x100000A6);
    static_assert(std::to_underlying(UIMessage::kLogout) == 0x1000009D);

    static_assert(GW::UI::ControlAction_Interact == 0x80);
    static_assert(GW::UI::ControlAction_CancelAction == 0xAF);

    using GW::Constants::Allegiance;
    static_assert(std::to_underlying(Allegiance::Ally_NonAttackable) == 1);
    static_assert(std::to_underlying(Allegiance::Neutral) == 2);
    static_assert(std::to_underlying(Allegiance::Enemy) == 3);
    static_assert(std::to_underlying(Allegiance::Spirit_Pet) == 4);
    static_assert(std::to_underlying(Allegiance::Minion) == 5);
    static_assert(std::to_underlying(Allegiance::Npc_Minipet) == 6);

    static_assert(GW::Packet::StoC::GenericValueID::skill_activated == 60);
    static_assert(GW::Constants::ItemID::GhostInTheBox == 6368);
}
