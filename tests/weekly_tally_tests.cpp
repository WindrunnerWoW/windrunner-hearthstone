#include "../src/match/HsMatch.h"
#include "../src/match/HsMatchInternal.h"
#include "../src/quests/HsWeeklyQuests.h"
#include "match_all_cards.h"
#include <cstdio>

using namespace Hs::Match;
using TA = TestAccess;
static int checks = 0, failed = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failed; std::printf("tally failed at %d: %s\n", __LINE__, #value); } } while (0)

static std::unique_ptr<Match> Fresh(char const* cls = "MAGE", int first = 0)
{
    MatchSetup setup;
    setup.seed = 7;
    setup.firstSide = first;
    for (int side = 0; side < 2; ++side)
    {
        setup.sides[side].name = "Weekly";
        setup.sides[side].heroClass = cls;
        setup.sides[side].deck = std::vector<std::string>(30, "CS2_168");
    }
    std::string error;
    auto match = Match::Create(MatchAllCards::Db(), setup, error);
    CHECK(match != nullptr);
    CHECK(match->Tally(0).cardsDrawn == 0 && match->Tally(1).cardsDrawn == 0);
    Action mull; mull.type = ActionType::Mulligan;
    mull.replace = TA::HandIds(*match, 0);
    CHECK(match->Apply(0, mull) == ActionError::None);
    CHECK(match->Tally(0).cardsDrawn == 0);
    mull.replace.clear();
    CHECK(match->Apply(1, mull) == ActionError::None);
    CHECK(match->Tally(first).cardsDrawn == 1 && match->Tally(1-first).cardsDrawn == 0);
    CHECK(match->Tally(first).turns == 1 && match->Tally(1-first).turns == 0);
    TA::SetHand(*match, 0, {}); TA::SetHand(*match, 1, {});
    TA::SetMana(*match, 0, 10, 10); TA::SetMana(*match, 1, 10, 10);
    return match;
}

static void Play(Match& m, EntityId card, EntityId target = 0)
{
    Action a; a.type = ActionType::Play; a.source = card; a.target = target;
    CHECK(m.Apply(m.ActiveSide(), a) == ActionError::None);
}

int main()
{
    {
        auto m = Fresh();
        auto hand = TA::SetHand(*m, 0, {"CS2_201", "CS2_171"}); // Core Hound (7), Stonetusk Boar (1)
        Play(*m, hand[0]); Play(*m, hand[1]);
        CHECK(m->Tally(0).minionsPlayed == 2 && m->Tally(0).beastsPlayed == 2);
        CHECK(m->Tally(0).expensivePlayed == 1 && m->Tally(1).minionsPlayed == 0);
        TA::SetMana(*m, 0, 10, 10);
        hand = TA::SetHand(*m, 0, {"CS2_027"}); // Mirror Image summons two minions
        Play(*m, hand[0]);
        CHECK(m->Tally(0).spellsPlayed == 1 && m->Tally(0).minionsPlayed == 2);
        hand = TA::SetHand(*m, 0, {"CS2_023"}); // Arcane Intellect
        Play(*m, hand[0]);
        CHECK(m->Tally(0).cardsDrawn == 3);
        Action power; power.type = ActionType::HeroPower; power.source = TA::PowerId(*m, 0); power.target = TA::HeroId(*m, 1);
        CHECK(m->Apply(0, power) == ActionError::None);
        CHECK(m->Tally(0).heroPowers == 1 && m->Tally(0).heroDamage == 1);
        CHECK(m->Apply(0, power) != ActionError::None && m->Tally(0).heroPowers == 1);
        Action end; end.type = ActionType::EndTurn;
        CHECK(m->Apply(0, end) == ActionError::None);
        CHECK(m->Tally(1).turns == 1 && m->Tally(1).cardsDrawn == 1);
        CHECK(m->Apply(1, end) == ActionError::None);
        CHECK(m->Tally(0).turns == 2);
    }
    {
        auto m = Fresh();
        TA::SetHero(*m, 1, 2, 3);
        auto hand = TA::SetHand(*m, 0, {"CS2_029"}); // Fireball, six damage capped to five health+armour
        Play(*m, hand[0], TA::HeroId(*m, 1));
        CHECK(m->Tally(0).heroDamage == 5 && m->Tally(1).heroDamage == 0);
    }
    {
        auto m = Fresh();
        TA::SetSecrets(*m, 1, {"EX1_295"}); // Ice Block prevents the whole hit
        TA::SetHero(*m, 1, 2, 0);
        auto hand = TA::SetHand(*m, 0, {"CS2_029"});
        Play(*m, hand[0], TA::HeroId(*m, 1));
        CHECK(m->Tally(0).heroDamage == 0);
    }
    {
        auto m = Fresh("WARLOCK");
        TA::SetBoard(*m, 0, {"CS2_168"}); TA::SetBoard(*m, 1, {"CS2_168", "CS2_168"});
        auto hand = TA::SetHand(*m, 0, {"CS2_062"}); // Hellfire: both boards, both heroes
        Play(*m, hand[0]);
        CHECK(m->Tally(0).minionsDestroyed == 2 && m->Tally(1).minionsDestroyed == 1);
        CHECK(m->Tally(0).heroDamage == 3 && m->Tally(1).heroDamage == 0);
        TA::SetHero(*m, 0, 1, 0);
        hand = TA::SetHand(*m, 0, {"EX1_620"}); // Molten Giant costs zero now
        Play(*m, hand[0]);
        CHECK(m->Tally(0).expensivePlayed == 0);
    }
    {
        auto m = Fresh();
        TA::SetHand(*m, 0, std::vector<std::string>(10, "CS2_168"));
        auto hand = TA::HandIds(*m, 0);
        TA::SetHand(*m, 0, {"CS2_023", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168", "CS2_168"});
        hand = TA::HandIds(*m, 0);
        Play(*m, hand[0]);
        CHECK(m->Tally(0).cardsDrawn == 2); // one draw reaches hand, the other burns
        TA::SetDeck(*m, 0, {}); TA::SetHand(*m, 0, {"CS2_023"});
        Play(*m, TA::HandIds(*m, 0)[0]);
        CHECK(m->Tally(0).cardsDrawn == 2 && m->Tally(1).heroDamage == 0); // fatigue is not enemy damage
    }
    {
        auto m = Fresh("MAGE", 1);
        CHECK(m->Tally(0).turns == 0 && m->Tally(1).turns == 1);
    }
    {
        auto m = Fresh();
        auto board = TA::SetBoard(*m, 1, {"EX1_029"}); // Leper Gnome damages the opposing hero after dying
        auto hand = TA::SetHand(*m, 0, {"CS2_029"});
        Play(*m, hand[0], board[0]);
        CHECK(m->Tally(1).heroDamage == 2 && m->Tally(0).heroDamage == 0);
        CHECK(m->Tally(0).minionsDestroyed == 1);
    }
    {
        auto m = Fresh("HUNTER", 1);
        TA::SetSecrets(*m, 0, {"EX1_610"}); // Explosive Trap, on the other side's turn
        auto board = TA::SetBoard(*m, 1, {"CS2_168"});
        Action attack; attack.type = ActionType::Attack; attack.source = board[0]; attack.target = TA::HeroId(*m, 0);
        CHECK(m->Apply(1, attack) == ActionError::None);
        CHECK(m->Tally(0).heroDamage == 2 && m->Tally(0).minionsDestroyed == 1);
    }
    {
        auto m = Fresh("WARRIOR");
        TA::SetWeapon(*m, 0, "CS2_106");
        Action attack; attack.type = ActionType::Attack; attack.source = TA::HeroId(*m, 0); attack.target = TA::HeroId(*m, 1);
        CHECK(m->Apply(0, attack) == ActionError::None);
        CHECK(m->Tally(0).heroDamage == 3);
    }
    {
        auto m = Fresh("HUNTER");
        auto hand = TA::SetHand(*m, 0, {"DS1_184"}); // Tracking draws only the chosen card
        Play(*m, hand[0]);
        CHECK(m->Tally(0).cardsDrawn == 1);
        auto view = m->ViewFor(0, 0);
        CHECK(!view.pick.empty());
        Action choose; choose.type = ActionType::Choose; choose.source = view.pick[0].id;
        CHECK(m->Apply(0, choose) == ActionError::None);
        CHECK(m->Tally(0).cardsDrawn == 2);
    }
    {
        auto m = Fresh("WARLOCK");
        TA::SetDeck(*m, 0, {"EX1_310", "EX1_319"});
        auto hand = TA::SetHand(*m, 0, {"EX1_317"}); // Sense Demons uses the tutor draw path
        Play(*m, hand[0]);
        CHECK(m->Tally(0).cardsDrawn == 3);
    }
    Hs::MatchEnd end;
    end.res = "loss"; end.why = "hero";
    end.tallies[0] = {8, 5, 7, 4, 3, 2, 29, 1, 6};
    CHECK(Hs::WeeklyCredit(*Hs::FindWeeklyQuest(919992), end, false) == 2);
    CHECK(Hs::WeeklyCredit(*Hs::FindWeeklyQuest(919982), end, false) == 8);
    CHECK(Hs::WeeklyCredit(*Hs::FindWeeklyQuest(919994), end, false) == 6);
    end.why = "concede";
    CHECK(Hs::WeeklyCredit(*Hs::FindWeeklyQuest(919992), end, false) == 0);
    std::printf("Weekly tallies: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
