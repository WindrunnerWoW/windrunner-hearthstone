#include "HsAllianceMageQuest.h"
#include "quests/HsAllianceMageRules.h"
#include "Camera.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

namespace
{
    bool Eligible(Player const* player)
    {
        return player && player->GetTeam() == ALLIANCE && player->GetLevel() >= 10 &&
            player->GetQuestRewardStatus(kHsAllianceMageIntro);
    }

    bool Open(Player const* player, uint32 quest)
    {
        uint32 const previous = quest == kHsAllianceMageQuiz ? kHsAllianceMageIntro : quest - 1;
        auto const status = player ? player->GetQuestStatus(quest) : QUEST_STATUS_NONE;
        return player && HsAllianceMageLessonAllowed(player->GetTeam() == ALLIANCE, player->GetLevel(),
            player->GetQuestRewardStatus(kHsAllianceMageIntro),
            status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE,
            player->GetQuestRewardStatus(quest), player->GetQuestRewardStatus(previous));
    }

    uint32 Question(Player const* player)
    {
        auto const* data = player ? player->GetQuestStatusData(kHsAllianceMageQuiz) : nullptr;
        return data ? HsAllianceMageQuizNextQuestion(data->m_creatureOrGOcount[0]) : kHsAllianceMageQuestionCount;
    }

    bool VisitDone(Player const* player, uint32 index)
    {
        auto const* data = player ? player->GetQuestStatusData(kHsAllianceMageTour) : nullptr;
        return !data || index >= kHsAllianceMageImages.size() || data->m_creatureOrGOcount[index] != 0;
    }

    bool ImageVisible(Player const* player)
    {
        auto const status = player ? player->GetQuestStatus(kHsAllianceMageTour) : QUEST_STATUS_NONE;
        return player && HsAllianceMageImageVisible(player->GetTeam() == ALLIANCE, player->GetLevel(),
            player->GetQuestRewardStatus(kHsAllianceMageIntro),
            status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE,
            player->GetQuestRewardStatus(kHsAllianceMageTour), player->GetQuestRewardStatus(kHsAllianceMageQuiz));
    }

    bool Interaction(Player const* player, Creature const* creature)
    {
        return player && creature && player->GetSession() && player->IsInWorld() && creature->IsInWorld() &&
            player->IsAlive() && creature->IsAlive() && !player->IsInCombat() && !creature->IsInCombat() &&
            player->GetMapId() == creature->GetMapId() && player->GetInstanceId() == creature->GetInstanceId() &&
            player->GetSession()->GetCurrentGossipGUID() == creature->GetObjectGuid() &&
            player->IsWithinDistInMap(creature, INTERACTION_DISTANCE) && player->IsWithinLOSInMap(creature);
    }

    void Option(GossipMenu& menu, char const* text, uint32 sender, uint32 action)
    {
        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS) return;
        menu.AddMenuItem(GOSSIP_ICON_CHAT, text, sender, action, "", false);
        menu.AddGossipMenuItemData(0, 0, 0);
    }

    void Page(Player* player, Creature* creature, uint32 offset)
    {
        player->PlayerTalkClass->SendGossipMenu(kHsAllianceMageFirstText + offset, creature->GetObjectGuid());
    }

    struct QuizQuestion
    {
        char const* choices[3];
        uint32 correct;
        char const* acknowledgement;
    };

    QuizQuestion const kQuestions[] = {
        {{"They came from Azeroth.", "Their home was Draenor.", "Kalimdor, I think."}, 1, "That explains the crossing."},
        {{"That was Khadgar.", "It was Antonidas.", "Medivh helped open it."}, 2, "I'll remember the name."},
        {{"Stormwind fell.", "It was Lordaeron.", "Dalaran was destroyed."}, 0, "I see. Stormwind was rebuilt."},
        {{"He was prince of Stromgarde.", "Lordaeron was his kingdom.", "He belonged to Alterac."}, 1, "Lordaeron. I understand."},
        {{"Medivh led them.", "That was Khadgar.", "Jaina Proudmoore."}, 2, "That was quite a journey."},
        {{"At Blackrock Mountain.", "At Mount Hyjal.", "At the gates of Ironforge."}, 1, "I'll remember Hyjal."},
        {{"Troggs.", "Kobolds, surely.", "The Dark Iron dwarves."}, 0, "A hard way to lose a city."},
        {{"They called themselves the Scourge.", "The Cult of the Damned.", "The Forsaken."}, 2, "That's an important difference."},
        {{"The Earth Mother.", "It is Elune's temple.", "They honor the Light."}, 1, "I'll remember whose temple it is."},
        {{"We are standing on Nordrassil.", "Aldrassil, perhaps.", "The tree is Teldrassil."}, 2, "I'd like to see more of it."},
    };
    static_assert(sizeof(kQuestions) / sizeof(kQuestions[0]) == kHsAllianceMageQuestionCount,
        "Every quiz question must have its choices and acknowledgement");

    char const* const kTourReplies[3][3] = {
        {"Who keeps watch here?", "What is it like up close?", "That sounds like a fair account."},
        {"It seems calm for a workshop district.", "Everything here was grown?", "You should write that down."},
        {"What is in the middle?", "Did you want to stay?", "That's an honest way to put it."},
    };

    void ShowQuestion(Player* player, Creature* creature, uint32 question, uint32 sender)
    {
        // Only the current question is offered. The native counter is the sole saved quiz state.
        player->PlayerTalkClass->ClearMenus();
        if (question >= kHsAllianceMageQuestionCount) return;
        auto& menu = player->PlayerTalkClass->GetGossipMenu();
        for (uint32 choice = 0; choice < 3; ++choice)
            Option(menu, kQuestions[question].choices[choice], sender,
                kHsAllianceMageActionFirst + 16 + question * 3 + choice);
        Page(player, creature, 20 + question * 3);
    }

    bool MageAction(uint32 action)
    {
        return action >= kHsAllianceMageActionFirst && action <= kHsAllianceMageActionLast;
    }

    void ClearOwnMenu(Player* player)
    {
        if (!player || !player->PlayerTalkClass) return;
        // Shared duel/deck choices use their own actions, but still belong to this teacher's window.
        Creature* creature = player->IsInWorld() && player->GetSession() ?
            player->GetMap()->GetCreature(player->GetSession()->GetCurrentGossipGUID()) : nullptr;
        bool const owned = creature && (creature->GetEntry() == kHsMitchellJones ||
            HsAllianceMageImageIndex(creature->GetEntry()) < kHsAllianceMageImages.size());
        if (owned)
        {
            player->PlayerTalkClass->ClearMenus();
            player->PlayerTalkClass->CloseGossip();
            return;
        }
        auto& menu = player->PlayerTalkClass->GetGossipMenu();
        for (uint32 index = 0; index < menu.MenuItemCount(); ++index)
        {
            if (!MageAction(menu.MenuItemAction(index))) continue;
            player->PlayerTalkClass->ClearMenus();
            if (player->GetSession()) player->PlayerTalkClass->CloseGossip();
            return;
        }
    }

    class Images final : public AllCreatureScript
    {
    public:
        Images() : AllCreatureScript("hearthstone_mitchell_images") {}
        void OnCreatureAddWorld(Creature* creature) override
        {
            if (HsAllianceMageImageIndex(creature->GetEntry()) < kHsAllianceMageImages.size())
                // Owner-placed, selectable DB creatures keep their normal AI/movement initialization.
                creature->SetReactState(REACT_PASSIVE);
        }
        bool CanSeeCreature(Player const* player, Creature const* creature) const override
        {
            if (HsAllianceMageImageIndex(creature->GetEntry()) >= kHsAllianceMageImages.size()) return true;
            return ImageVisible(player);
        }
    };

    class Recovery final : public PlayerScript
    {
    public:
        Recovery() : PlayerScript("hearthstone_mitchell_recovery", {PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_PLAYER_JUST_DIED, PLAYERHOOK_ON_BEFORE_LOGOUT, PLAYERHOOK_ON_BEFORE_TELEPORT}) {}
        void OnLogin(Player* player) override
        {
            if (player && player->IsInWorld()) player->GetCamera().UpdateVisibilityForOwner();
        }
        void OnPlayerJustDied(Player* player) override { ClearOwnMenu(player); }
        void OnBeforeLogout(Player* player) override { ClearOwnMenu(player); }
        void OnBeforeTeleport(Player* player, uint32, float, float, float, float) override { ClearOwnMenu(player); }
    };
}

void AddHsAllianceMageQuestScripts()
{
    new Images();
    new Recovery();
}

bool HsAllianceMageGossipActive(Player* player, Creature* creature)
{
    if (!player || !creature) return false;
    if (creature->GetEntry() == kHsMitchellJones) return true;
    return HsAllianceMageImageIndex(creature->GetEntry()) < kHsAllianceMageImages.size() && ImageVisible(player);
}

void HsAllianceMageAppendGossip(Player* player, Creature* creature, GossipMenu& menu, uint32_t sender)
{
    if (!HsAllianceMageGossipActive(player, creature)) return;
    uint32 const image = HsAllianceMageImageIndex(creature->GetEntry());
    if (image < kHsAllianceMageImages.size())
    {
        bool const done = VisitDone(player, image);
        menu.SetMenuId(kHsAllianceMageFirstMenu + (done ? 20 : 17) + image);
        if (!done && player->GetQuestStatus(kHsAllianceMageTour) == QUEST_STATUS_INCOMPLETE)
            Option(menu, kTourReplies[image][0], sender, kHsAllianceMageActionFirst + 64 + image * 4);
        return;
    }

    uint32 greeting = Eligible(player) ? 1 : 0;
    if (Eligible(player))
    {
        if (player->GetQuestRewardStatus(kHsAllianceMageDuel))
        {
            greeting = 13;
            Option(menu, "What will you write about next?", sender, kHsAllianceMageActionFirst + 80);
        }
        else if (Open(player, kHsAllianceMageDuel))
            greeting = player->GetQuestStatus(kHsAllianceMageDuel) == QUEST_STATUS_COMPLETE ? 12 : 10;
        else if (player->GetQuestRewardStatus(kHsAllianceMageTour)) greeting = 9;
        else if (Open(player, kHsAllianceMageTour))
            greeting = player->GetQuestStatus(kHsAllianceMageTour) == QUEST_STATUS_COMPLETE ? 8 : 7;
        else if (player->GetQuestRewardStatus(kHsAllianceMageQuiz)) greeting = 6;
        else if (Open(player, kHsAllianceMageQuiz))
        {
            uint32 const question = Question(player);
            greeting = question >= kHsAllianceMageQuestionCount ||
                player->GetQuestStatus(kHsAllianceMageQuiz) == QUEST_STATUS_COMPLETE ? 5 : question ? 3 : 2;
            if (question < kHsAllianceMageQuestionCount && player->GetQuestStatus(kHsAllianceMageQuiz) == QUEST_STATUS_INCOMPLETE)
                Option(menu, question ? "Go on. I'm listening." : "Ask away.", sender,
                    kHsAllianceMageActionFirst + question);
        }
    }
    menu.SetMenuId(kHsAllianceMageFirstMenu + greeting);
}

bool HsAllianceMageHandleSelect(Player* player, Creature* creature, uint32_t action, uint32_t sender)
{
    if (!MageAction(action)) return false;
    if (!player || !player->PlayerTalkClass) return true;
    auto const reply = HsAllianceMageDecode(action);
    auto& menu = player->PlayerTalkClass->GetGossipMenu();
    bool offered = false;
    for (uint32 index = 0; index < menu.MenuItemCount(); ++index)
        offered |= menu.MenuItemSender(index) == sender && menu.MenuItemAction(index) == action;
    bool const teacher = creature && creature->GetEntry() == kHsMitchellJones;
    uint32 const image = creature ? HsAllianceMageImageIndex(creature->GetEntry()) : 3;
    uint32 const question = Question(player);
    bool const quiz = HsAllianceMageQuizReplyAllowed(reply, offered, teacher,
        Open(player, kHsAllianceMageQuiz) && player->GetQuestStatus(kHsAllianceMageQuiz) == QUEST_STATUS_INCOMPLETE,
        question);
    bool const tour = HsAllianceMageTourReplyAllowed(reply, offered, image,
        Open(player, kHsAllianceMageTour) && player->GetQuestStatus(kHsAllianceMageTour) == QUEST_STATUS_INCOMPLETE,
        VisitDone(player, image));
    bool const afterward = offered && teacher && reply.kind == HsAllianceMageAction::Afterward &&
        Eligible(player) && player->GetQuestRewardStatus(kHsAllianceMageDuel);
    if (!Interaction(player, creature) || !HsAllianceMageGossipActive(player, creature) || !(quiz || tour || afterward))
    {
        player->PlayerTalkClass->ClearMenus();
        player->PlayerTalkClass->CloseGossip();
        return true;
    }

    if (reply.kind == HsAllianceMageAction::Launch)
        ShowQuestion(player, creature, question, sender);
    else if (reply.kind == HsAllianceMageAction::Answer)
    {
        player->PlayerTalkClass->ClearMenus();
        Option(player->PlayerTalkClass->GetGossipMenu(), kQuestions[question].acknowledgement, sender,
            kHsAllianceMageActionFirst + 48 + question);
        Page(player, creature, 20 + question * 3 + (reply.page == kQuestions[question].correct ? 1 : 2));
    }
    else if (reply.kind == HsAllianceMageAction::Acknowledge)
    {
        // Clear the offered acknowledgement before credit; repeated/stale replies cannot increment twice.
        player->PlayerTalkClass->ClearMenus();
        player->KilledMonsterCredit(kHsAllianceMageQuizCredit);
        uint32 const next = Question(player);
        if (next < kHsAllianceMageQuestionCount && player->GetQuestStatus(kHsAllianceMageQuiz) == QUEST_STATUS_INCOMPLETE)
            ShowQuestion(player, creature, next, sender);
        else
        {
            player->PrepareQuestMenu(creature->GetObjectGuid());
            Page(player, creature, 4);
        }
    }
    else if (reply.kind == HsAllianceMageAction::Tour)
    {
        player->PlayerTalkClass->ClearMenus();
        if (reply.page == 2)
        {
            player->KilledMonsterCredit(kHsAllianceMageTourCredits[image]);
            if (!VisitDone(player, image))
            {
                // Native raid rules can reject credit. Keep the final acknowledgement retryable.
                Option(player->PlayerTalkClass->GetGossipMenu(), kTourReplies[image][2], sender, action);
                Page(player, creature, 50 + image * 5 + 2);
                return true;
            }
        }
        else Option(player->PlayerTalkClass->GetGossipMenu(), kTourReplies[image][reply.page + 1], sender, action + 1);
        Page(player, creature, 50 + image * 5 + reply.page + 1);
    }
    else
    {
        player->PlayerTalkClass->ClearMenus();
        player->PrepareQuestMenu(creature->GetObjectGuid());
        Page(player, creature, 14);
    }
    return true;
}

void HsAllianceMageAfterQuestChange(Player* player)
{
    if (!player) return;
    if (player->PlayerTalkClass)
    {
        auto& menu = player->PlayerTalkClass->GetGossipMenu();
        for (uint32 index = 0; index < menu.MenuItemCount(); ++index)
        {
            auto const reply = HsAllianceMageDecode(menu.MenuItemAction(index));
            if (reply.kind == HsAllianceMageAction::Invalid) continue;
            bool const allowed = reply.kind == HsAllianceMageAction::Afterward ?
                player->GetQuestRewardStatus(kHsAllianceMageDuel) :
                Open(player, reply.kind == HsAllianceMageAction::Tour ? kHsAllianceMageTour : kHsAllianceMageQuiz) &&
                player->GetQuestStatus(reply.kind == HsAllianceMageAction::Tour ? kHsAllianceMageTour : kHsAllianceMageQuiz) ==
                    QUEST_STATUS_INCOMPLETE;
            if (allowed) continue;
            ClearOwnMenu(player);
            break;
        }
    }
    // Called by the existing post-dispatch ACCEPT/REMOVE/CHOOSE_REWARD packet hook,
    // after native quest state changes (including failed rewards that leave it active).
    if (player->IsInWorld()) player->GetCamera().UpdateVisibilityForOwner();
}
