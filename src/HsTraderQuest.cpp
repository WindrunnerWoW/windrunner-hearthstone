#include "HsTraderQuest.h"
#include "quests/HsTraderQuestRules.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"

bool HsTraderQuestCanSendPacket(Player* player, WorldPacket const& packet)
{
    if (!player || !player->GetSession() || packet.size() < 4 ||
        packet.GetOpcode() != SMSG_QUESTUPDATE_ADD_ITEM)
        return true;
    uint32 const quest = HsTraderQuestForItem(packet.read<uint32>(0));
    return !quest || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE;
}
