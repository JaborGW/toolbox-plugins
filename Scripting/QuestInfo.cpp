#include <QuestInfo.h>

#include <Windows.h>

#include <GWCA/GameEntities/Quest.h>

#include <GWCA/Context/WorldContext.h>

#include <GWCA/Managers/QuestMgr.h>
#include <GWCA/stdlib/StoCMgr.h>
#include <GWCA/stdlib/UIMgr.h>

#include <GWCA/Packets/StoC.h>

#include <enumUtils.h>
#include <DecodedText.h>

#include <ranges>
#include <chrono>

namespace {
    std::wstring getObjectiveContent(std::wstring_view enc_str)
    {
        auto content_start = enc_str.find(0x10a);
        if (content_start == std::wstring::npos) return {};
        content_start++;
        return std::wstring{enc_str.substr(content_start, enc_str.size() - content_start - 1)};
    }
    
    GW::HookEntry ObjectiveUpdateName_Entry;
    GW::HookEntry ObjectiveDone_Entry;
    GW::HookEntry DisplayDialogue_Entry;
    GW::HookEntry QuestAdd_Entry;
    GW::HookEntry InstanceLoadFile_Entry;
    GW::HookEntry UpdateQuestnames_Entry;
}

QuestStatus QuestInfo::getMissionObjectiveStatus(uint32_t id) const
{
    const auto it = missionObjectiveStatus.find(id);
    return (it != missionObjectiveStatus.end()) ? it->second : QuestStatus::NotStarted;
}

std::optional<QuestInfo::Quest> QuestInfo::getQuest(std::string_view name) const
{
    const auto questLog = GW::QuestMgr::GetQuestLog();
    if (!questLog) return {};

    for (const auto& quest : *questLog) 
    {
        auto string = DecodedText::Utf8(DecodedText::Get(quest.name));
        if (string.empty()) continue;
        if (string.contains(name)) 
        {
            return QuestInfo::Quest{std::move(string), quest.quest_id};
        }
    }

    return std::nullopt;
}

std::vector<QuestInfo::Objective> QuestInfo::listObjectives(GW::Constants::QuestID id) const 
{
    const auto questLog = GW::QuestMgr::GetQuestLog();
    if (!questLog) return {};
    
    const auto quest = std::ranges::find_if(*questLog, [&](const auto& quest) { return quest.quest_id == id; });
    if (quest == questLog->end()) return {};

    const auto decoded = DecodedText::Utf8(DecodedText::Get(quest->objectives));
    if (decoded.empty()) return {};

    std::vector<QuestInfo::Objective> result;
    for (const auto word : std::views::split(decoded, '{'))
    {
        auto objective = std::string{word.begin(), word.end()};
        // Technically the objective now starts with "s}" or "sc}"; currently we just check "contains" on this, so dont care
        const auto isCompleted = objective.starts_with("sc");
        result.emplace_back(std::move(objective), isCompleted);
    }
    return result;
}

std::vector<QuestInfo::Objective> QuestInfo::listMissionObjectives() const 
{
    const auto& objectives = GW::GetWorldContext()->mission_objectives;
    std::vector<QuestInfo::Objective> result;
    result.reserve(objectives.size());

    for (const auto& objective : objectives) 
    {
        auto decoded = DecodedText::Utf8(DecodedText::Get(getObjectiveContent(objective.enc_str ? objective.enc_str : L"")));
        if (decoded.empty()) continue;

        const auto status = getMissionObjectiveStatus(objective.objective_id);
        result.emplace_back(std::move(decoded), status == QuestStatus::Completed);
    }

    return result;
}

void QuestInfo::decodeStrings() 
{
    if (const auto questLog = GW::QuestMgr::GetQuestLog()) 
    {
        for (const auto& quest : *questLog) 
        {
            DecodedText::Get(quest.name);
            if (quest.objectives)
                DecodedText::Get(quest.objectives);
            else if (!requestedQuestInfos.contains(quest.quest_id))
                questsMissingObjectives.push(quest.quest_id);
        }
    }

    if (const auto world = GW::GetWorldContext())
    {
        for (const auto& objective : world->mission_objectives) 
        {
            if (objective.enc_str)
                DecodedText::Get(getObjectiveContent(objective.enc_str));
        }
    }
}

void QuestInfo::initialize()
{
    decodeStrings();

    GW::StoC::RegisterPostPacketCallback<GW::Packet::StoC::InstanceLoadFile>(&InstanceLoadFile_Entry, [this](GW::HookStatus*, const auto*)
    {
        requestedQuestInfos.clear();
        decodeStrings();
        missionObjectiveStatus.clear();
    });
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::ObjectiveUpdateName>(&ObjectiveUpdateName_Entry, [this](GW::HookStatus*, const GW::Packet::StoC::ObjectiveUpdateName* packet) {
        this->missionObjectiveStatus[packet->objective_id] = QuestStatus::Running;
        decodeStrings();
    });
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::ObjectiveDone>(&ObjectiveDone_Entry, [this](GW::HookStatus*, const GW::Packet::StoC::ObjectiveDone* packet) {
        this->missionObjectiveStatus[packet->objective_id] = QuestStatus::Completed;
        decodeStrings();
    });
    GW::StoC::RegisterPostPacketCallback<GW::Packet::StoC::DisplayDialogue>(&DisplayDialogue_Entry, [this](GW::HookStatus*, const GW::Packet::StoC::DisplayDialogue* packet) {
        if (wmemcmp(packet->message, L"\x8102\x5d41\xa992\xf927\x29f7", 5) == 0)
        {
             // Dhuum quest does not send a `ObjectiveUpdateName` StoC
            this->missionObjectiveStatus[157] = QuestStatus::Running;
        }
    });

    const GW::StoC::PacketCallback updateQuestNamesLambda = [this](GW::HookStatus*, const auto*) { decodeStrings(); };
    GW::StoC::RegisterPostPacketCallback<GW::Packet::StoC::QuestAdd>(&QuestAdd_Entry, updateQuestNamesLambda);
    GW::StoC::RegisterPostPacketCallback(&UpdateQuestnames_Entry, GAME_SMSG_QUEST_DESCRIPTION, updateQuestNamesLambda);
    GW::StoC::RegisterPostPacketCallback(&UpdateQuestnames_Entry, GAME_SMSG_QUEST_REMOVE, updateQuestNamesLambda);
    GW::StoC::RegisterPostPacketCallback(&UpdateQuestnames_Entry, GAME_SMSG_QUEST_GENERAL_INFO, updateQuestNamesLambda);
    GW::StoC::RegisterPostPacketCallback(&UpdateQuestnames_Entry, GAME_SMSG_QUEST_UPDATE_NAME, updateQuestNamesLambda);
    GW::StoC::RegisterPostPacketCallback(&UpdateQuestnames_Entry, GAME_SMSG_MISSION_OBJECTIVE_ADD, updateQuestNamesLambda);
}

void QuestInfo::terminate()
{
    GW::StoC::RemovePostCallback<GW::Packet::StoC::ObjectiveUpdateName>(&ObjectiveUpdateName_Entry);
    GW::StoC::RemovePostCallback<GW::Packet::StoC::ObjectiveDone>(&ObjectiveDone_Entry);
    GW::StoC::RemovePostCallback<GW::Packet::StoC::InstanceLoadFile>(&InstanceLoadFile_Entry);
    GW::StoC::RemovePostCallback<GW::Packet::StoC::QuestAdd>(&QuestAdd_Entry);
    GW::StoC::RemovePostCallback<GW::Packet::StoC::DisplayDialogue>(&DisplayDialogue_Entry);
    GW::StoC::RemoveCallbacks(&UpdateQuestnames_Entry);
}

void QuestInfo::update()
{
    if (questsMissingObjectives.empty()) return;

    const auto questId = questsMissingObjectives.front();
    questsMissingObjectives.pop();
    if (requestedQuestInfos.contains(questId)) return;

    const auto quest = GW::QuestMgr::GetQuest(questId);
    if (!quest) return;
    if (quest->objectives)
    {
        DecodedText::Get(quest->objectives);
        return;
    }
    requestedQuestInfos.insert(questId);
    GW::QuestMgr::RequestQuestInfo(quest);
}
