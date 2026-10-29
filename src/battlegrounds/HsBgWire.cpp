#include "HsBgWire.h"

#include "../protocol/HsProtocol.h"

#include <algorithm>
#include <map>

namespace Hs
{
    namespace Bg
    {
        namespace
        {
            bool ParseInt(std::string const& s, int maxValue, int& out)
            {
                if (s.empty() || s.size() > 9)
                    return false;
                int v = 0;
                for (char c : s)
                {
                    if (c < '0' || c > '9')
                        return false;
                    v = v * 10 + (c - '0');
                }
                if (v > maxValue)
                    return false;
                out = v;
                return true;
            }

            void PushFrames(std::vector<std::string>& out, char const* op, std::string const& reqId, std::string const& payload)
            {
                std::vector<std::string> frames;
                if (ChunkPayload(op, reqId, payload, frames))
                    for (std::string& f : frames)
                        out.push_back(std::move(f));
            }

            std::string Num(long v) { return std::to_string(v); }

            void AppendMinion(std::string& s, Minion const& m)
            {
                s += m.Def().id;
                s += '.';
                s += m.golden ? '1' : '0';
                s += '.';
                s += Num(m.attack);
                s += '.';
                s += Num(m.health);
                s += '.';
                s += Num(m.Keywords());
                s += '.';
                s += Num(m.uid);
            }

            std::string MinionList(std::vector<Minion> const& items)
            {
                std::string s;
                for (Minion const& m : items)
                {
                    if (!s.empty())
                        s += ',';
                    AppendMinion(s, m);
                }
                return s;
            }

            // The reason letters of a need, in one fixed order: T (triple), R (tribe), U (upgrade).
            std::string NeedReasons(uint32_t reasons)
            {
                std::string s;
                if ((reasons & kNeedTriple) != 0)
                    s += 'T';
                if ((reasons & kNeedTribe) != 0)
                    s += 'R';
                if ((reasons & kNeedUpgrade) != 0)
                    s += 'U';
                return s;
            }

            // What each card of a list sells for, one number per card (the UI's sell price).
            std::string SellList(Shop const& shop, std::vector<Minion> const& items)
            {
                std::string s;
                for (Minion const& m : items)
                {
                    if (!s.empty())
                        s += ',';
                    s += Num(shop.SellGold(m));
                }
                return s;
            }
        }

        // ---------------------------------------------------------------------------------------------- actions

        bool DecodeBgAction(std::string const& payload, Action& out)
        {
            std::map<std::string, std::string> kv;
            if (!KvDecode(payload, kv))
                return false;
            auto const take = [&kv](char const* name, std::string& value)
            {
                auto const it = kv.find(name);
                if (it == kv.end())
                    return false;
                value = it->second;
                kv.erase(it);
                return true;
            };
            std::string act;
            if (!take("act", act))
                return false;
            Action a;
            std::string t;
            auto const num = [&](char const* key, int maxValue, int& dst)
            {
                return take(key, t) && ParseInt(t, maxValue, dst);
            };
            if (act == "buy")
            {
                a.type = ActionType::Buy;
                if (!num("i", 15, a.index))
                    return false;
            }
            else if (act == "sell")
            {
                a.type = ActionType::Sell;
                if (!take("z", t) || (t != "b" && t != "h"))
                    return false;
                a.zone = t == "b" ? Zone::Board : Zone::Hand;
                if (!num("i", 15, a.index))
                    return false;
            }
            else if (act == "play")
            {
                a.type = ActionType::Play;
                int uid = 0;
                if (!num("i", 15, a.index) || !num("s", 15, a.slot))
                    return false;
                if (take("u", t))
                {
                    if (!ParseInt(t, 999999999, uid))
                        return false;
                    a.uid = static_cast<uint32_t>(uid);
                }
            }
            else if (act == "cast")
            {
                a.type = ActionType::Cast;
                int uid = 0;
                if (!num("i", 15, a.index))
                    return false;
                if (take("u", t))
                {
                    if (!ParseInt(t, 999999999, uid))
                        return false;
                    a.uid = static_cast<uint32_t>(uid);
                }
            }
            else if (act == "move")
            {
                a.type = ActionType::Move;
                if (!num("f", 15, a.index) || !num("t", 15, a.slot))
                    return false;
            }
            else if (act == "freeze")
            {
                a.type = ActionType::Freeze;
                int on = 0;
                if (!num("on", 1, on))
                    return false;
                a.flag = on == 1;
            }
            else if (act == "discover")
            {
                a.type = ActionType::Discover;
                if (!num("i", 15, a.index))
                    return false;
            }
            else if (act == "hero")
            {
                a.type = ActionType::Hero;
                if (!num("i", 15, a.index))
                    return false;
            }
            else if (act == "quest")
            {
                a.type = ActionType::Quest;
                if (!num("i", 15, a.index))
                    return false;
            }
            else if (act == "power")
            {
                a.type = ActionType::Power;
                a.index = -1;
                int uid = 0;
                if (take("u", t))
                {
                    if (!ParseInt(t, 999999999, uid))
                        return false;
                    a.uid = static_cast<uint32_t>(uid);
                }
                if (take("u2", t))
                {
                    if (!ParseInt(t, 999999999, uid))
                        return false;
                    a.uid2 = static_cast<uint32_t>(uid);
                }
                if (take("o", t))
                {
                    int o = 0;
                    if (!ParseInt(t, 15, o))
                        return false;
                    a.index = o;
                }
            }
            else if (act == "send")
            {
                a.type = ActionType::Send;
                if (!num("i", 15, a.index) || !num("to", 7, a.slot))
                    return false;
            }
            else if (act == "peek")
            {
                a.type = ActionType::Peek;
                if (!num("to", 7, a.slot))
                    return false;
            }
            else if (act == "refresh")
                a.type = ActionType::Refresh;
            else if (act == "upgrade")
                a.type = ActionType::Upgrade;
            else if (act == "end")
                a.type = ActionType::EndTurn;
            else
                return false;
            if (!kv.empty())
                return false;
            out = a;
            return true;
        }

        std::string EncodeBgAction(Action const& a)
        {
            KvList kv;
            switch (a.type)
            {
            case ActionType::Buy:
                kv.push_back({ "act", "buy" });
                kv.push_back({ "i", Num(a.index) });
                break;
            case ActionType::Sell:
                kv.push_back({ "act", "sell" });
                kv.push_back({ "z", a.zone == Zone::Board ? "b" : "h" });
                kv.push_back({ "i", Num(a.index) });
                break;
            case ActionType::Play:
                kv.push_back({ "act", "play" });
                kv.push_back({ "i", Num(a.index) });
                kv.push_back({ "s", Num(a.slot) });
                if (a.uid != 0)
                    kv.push_back({ "u", Num(a.uid) });
                break;
            case ActionType::Cast:
                kv.push_back({ "act", "cast" });
                kv.push_back({ "i", Num(a.index) });
                if (a.uid != 0)
                    kv.push_back({ "u", Num(a.uid) });
                break;
            case ActionType::Move:
                kv.push_back({ "act", "move" });
                kv.push_back({ "f", Num(a.index) });
                kv.push_back({ "t", Num(a.slot) });
                break;
            case ActionType::Refresh: kv.push_back({ "act", "refresh" }); break;
            case ActionType::Upgrade: kv.push_back({ "act", "upgrade" }); break;
            case ActionType::EndTurn: kv.push_back({ "act", "end" }); break;
            case ActionType::Freeze:
                kv.push_back({ "act", "freeze" });
                kv.push_back({ "on", a.flag ? "1" : "0" });
                break;
            case ActionType::Discover:
                kv.push_back({ "act", "discover" });
                kv.push_back({ "i", Num(a.index) });
                break;
            case ActionType::Hero:
                kv.push_back({ "act", "hero" });
                kv.push_back({ "i", Num(a.index) });
                break;
            case ActionType::Quest:
                kv.push_back({ "act", "quest" });
                kv.push_back({ "i", Num(a.index) });
                break;
            case ActionType::Power:
                kv.push_back({ "act", "power" });
                if (a.uid != 0)
                    kv.push_back({ "u", Num(a.uid) });
                if (a.uid2 != 0)
                    kv.push_back({ "u2", Num(a.uid2) });
                if (a.index >= 0)
                    kv.push_back({ "o", Num(a.index) });
                break;
            case ActionType::Send:
                kv.push_back({ "act", "send" });
                kv.push_back({ "i", Num(a.index) });
                kv.push_back({ "to", Num(a.slot) });
                break;
            case ActionType::Peek:
                kv.push_back({ "act", "peek" });
                kv.push_back({ "to", Num(a.slot) });
                break;
            }
            return KvEncode(kv);
        }

        // ---------------------------------------------------------------------------------------------- state

        std::string EncodeBgState(Session const& s, bool withGifts)
        {
            Game const& g = s.GetGame();
            PlayerShop const& me = s.Me();
            KvList kv;
            kv.push_back({ "me", Num(s.Seat()) });
            kv.push_back({ "rd", Num(g.Round()) });
            kv.push_back({ "ph", g.Over() ? "o" : (g.Picking() ? "p" : (g.QuestPicking() ? "q" : "s")) });
            kv.push_back({ "al", s.HumanAlive() ? "1" : "0" });
            kv.push_back({ "en", g.TurnEnded(s.Seat()) ? "1" : "0" });
            kv.push_back({ "g", Num(me.gold) });
            kv.push_back({ "t", Num(me.tier) });
            kv.push_back({ "uc", Num(g.GetShop().UpgradeCost(s.Seat())) });
            kv.push_back({ "bc", Num(g.GetShop().BuyCost(s.Seat())) });   // Friends and Family Discount
            kv.push_back({ "fz", g.GetShop().OffersFrozen(s.Seat()) ? "1" : "0" });
            kv.push_back({ "hp", Num(me.health) });
            kv.push_back({ "cap", g.DamageCapped() ? "1" : "0" });
            // The hero pick, the picked hero and its armor / power, the send flag and a gift note.
            std::string hc;
            if (s.Picking())
                for (int h : s.HeroChoices())
                    hc += (hc.empty() ? "" : ",") + std::string(HeroAt(h).id);
            kv.push_back({ "hc", hc });
            int const myHero = g.PlayerHero(s.Seat());
            kv.push_back({ "hero", myHero >= 0 ? std::string(HeroAt(myHero).id) : std::string() });
            kv.push_back({ "arm", Num(me.armor) });
            kv.push_back({ "mhp", Num(me.maxHealth) });
            // the health I healed resting last round (my lobby entry's lastOpponent is -3 then).
            kv.push_back({ "rh", Num(g.RestHealed(s.Seat())) });
            HeroPowerDef const* const pw = g.GetShop().Power(s.Seat());
            int const powerCost = pw ? HeroPowerCost(*pw, me.power) : 0;
            kv.push_back({ "pw", pw ? std::string(pw->id) : std::string() });
            kv.push_back({ "pwc", Num(powerCost) });
            // The small counter on the power button: a flightpath's turns left or Ragnaros's kills left, else Eudora's
            // digs left, else Zephrys' wishes left.
            int counter = 0;
            if (pw && pw->countdownTurns > 0)
                counter = me.power.powerCountdown;
            else if (pw && pw->killsForSulfuras > 0)
                counter = std::max(0, pw->killsForSulfuras - me.power.enemyKills);
            else if (pw && pw->digsForGolden)
                counter = std::max(1, pw->digsNeeded - me.power.digs);
            else if (pw && pw->ticketsInTavern)
                counter = std::max(0, 3 - me.power.tickets);          // Silas's tickets left
            else if (pw && pw->pot)
                counter = std::max(0, 3 - me.power.potCount);        // Cookie: throws left before the Discover
            else if (pw && pw->holdsOffer)
                counter = me.power.holdsOffer ? me.power.heldTurns : 0;   // Maiev: turns left on the held offer
            else if (pw && pw->skipTurns > 0)
                counter = std::max(0, pw->skipTurns - g.Round() + 1);     // A. F. Kay / Faelin: turns left to skip
            else if (pw && pw->prizesEvery > 0)
                counter = DarkmoonPrizeTurnsLeft(g.Round(), me.power.prizesTaken);   // Tickatus
            else if (pw && pw->usesPerGame > 0)
                counter = std::max(0, pw->usesPerGame - me.power.Uses(pw->id));
            kv.push_back({ "pwx", Num(counter) });
            // What a power whose effect changes does right now, as a short line under its text (empty for the rest).
            {
                std::string now;
                if (pw && pw->ratKingDiscover && me.power.ratKingTribe != 0)
                {
                    static char const* const names[] = { "Beast", "Demon", "Dragon", "Elemental", "Mech", "Murloc", "Naga",
                        "Pirate", "Quilboar" };
                    for (int bit = 0; bit < 9; ++bit)
                        if (me.power.ratKingTribe == (1u << bit))
                            now = std::string("Now: ") + names[bit];
                }
                else if (pw && pw->growsHealthBuff)
                    now = "Now: +0/+" + Num(2 + me.power.healthBuffBonus);
                else if (pw && std::string(pw->id) == "TB_BaconShop_HP_001")
                    now = "Now: +" + Num(1 + me.power.buysThisGame / 4) + "/+" + Num(1 + me.power.buysThisGame / 4);
                else if (pw && pw->cthunEndOfTurn)
                    now = "Now: repeat " + Num(me.power.cthunRepeats) + " times";
                kv.push_back({ "pwn", now });
            }
            char targetKind = 'n';
            bool usable = false;
            if (pw && pw->activate)
            {
                targetKind = pw->target == PowerTarget::FriendlyMinion ? 'f'
                    : pw->target == PowerTarget::TavernOffer ? 'o'
                    : pw->target == PowerTarget::Seat ? 's'
                    : pw->target == PowerTarget::TwoMinions ? 'm' : 'n';
                usable = !(pw->oncePerGame && me.power.Uses(pw->id) > 0) &&
                    !(pw->usesPerGame > 0 && me.power.Uses(pw->id) >= pw->usesPerGame) &&
                    !(pw->usesPerTurn > 0 ? me.power.usesThisTurn >= pw->usesPerTurn
                                          : (pw->oncePerTurn && me.power.usesThisTurn > 0)) &&
                    !(pw->holdsOffer && me.power.holdsOffer) &&
                    me.gold >= powerCost;
            }
            kv.push_back({ "pwu", usable ? "1" : "0" });
            kv.push_back({ "pwt", std::string(1, targetKind) });
            kv.push_back({ "pws", me.skipTurn ? "1" : "0" });
            // My Secrets in play order (card ids, comma separated).
            {
                std::string secrets;
                for (int card : me.secrets)
                    if (card >= 0)
                        secrets += (secrets.empty() ? "" : ",") + std::string(CardAt(card).id);
                kv.push_back({ "sec", secrets });
            }
            // Maiev's held tavern offer (id.turns) and Faelin's Tier minions (id.tier, comma separated).
            {
                std::string held;
                if (me.power.holdsOffer && me.power.heldOffer.card >= 0)
                    held = std::string(CardAt(me.power.heldOffer.card).id) + "." + Num(me.power.heldTurns);
                kv.push_back({ "pwh", held });
                std::string expedition;
                for (Minion const& m : me.power.expedition)
                    if (m.card >= 0)
                        expedition += (expedition.empty() ? "" : ",") + std::string(CardAt(m.card).id) + "." + Num(m.Tier());
                kv.push_back({ "pwe", expedition });
            }
            kv.push_back({ "snd", me.sentThisTurn ? "1" : "0" });
            {
                std::string gifts;   // comma separated card ids, oldest first
                if (withGifts)
                    for (int card : s.LastGifts())
                        gifts += (gifts.empty() ? "" : ",") + std::string(CardAt(card).id);
                kv.push_back({ "gift", gifts });
                std::string wheel;   // one digit per spin of the Wheel of Yogg-Saron (the outcome 0-5), oldest first
                if (withGifts)
                    for (int outcome : s.LastWheel())
                        wheel += static_cast<char>('0' + outcome);
                kv.push_back({ "whl", wheel });
            }
            kv.push_back({ "of", MinionList(me.offers) });
            {
                std::string frozen;   // one digit per offer, 1 = Frozen
                for (Minion const& o : me.offers)
                    frozen += (o.flags & kMinionFrozen) != 0 ? '1' : '0';
                kv.push_back({ "ofz", frozen });
                std::string tickets;   // one digit per offer, 1 = a Darkmoon Ticket
                for (Minion const& o : me.offers)
                    tickets += (o.flags & kMinionTicket) != 0 ? '1' : '0';
                kv.push_back({ "oft", tickets });
            }
            kv.push_back({ "hd", MinionList(me.hand.Items()) });
            kv.push_back({ "svh", SellList(g.GetShop(), me.hand.Items()) });   // what each hand card sells for
            // Where the human's cards would help: "<uid>.<seat><reasons>,...", best first, at most three allies per
            // card. Reasons: T triple, R tribe, U upgrade. Empty outside the shop phase and once the send is used.
            {
                std::string nd;
                bool const shopOpen = !g.Over() && !g.Picking() && !g.QuestPicking() && !me.sentThisTurn &&
                    g.Alive(s.Seat());
                if (shopOpen)
                {
                    struct AllyNeed
                    {
                        int score = 0;
                        int seat = 0;
                        uint32_t reasons = 0;
                    };
                    struct CardNeed
                    {
                        uint32_t uid = 0;
                        int best = 0;
                        std::vector<AllyNeed> allies;
                    };
                    std::vector<CardNeed> cards;
                    auto collect = [&](std::vector<Minion> const& list)
                    {
                        for (Minion const& m : list)
                        {
                            if (m.card < 0 || CardAt(m.card).role == CardRole::Spell)
                                continue;
                            CardNeed cn;
                            cn.uid = m.uid;
                            for (int q = 0; q < kPlayers; ++q)
                            {
                                if (q == s.Seat() || FactionOf(q) != FactionOf(s.Seat()) || !g.Alive(q))
                                    continue;
                                Need const n = Ai::NeedScore(g.GetShop(), q, m, s.Seat());
                                if (n.score < kSendNeedMin)
                                    continue;
                                // a free slot alone is no reason to show the ally: every card would list every ally
                                uint32_t reasons = n.reasons;
                                if ((reasons & kNeedBody) != 0)
                                    reasons &= ~kNeedUpgrade;
                                if ((reasons & (kNeedTriple | kNeedTribe | kNeedUpgrade)) == 0)
                                    continue;
                                AllyNeed a;
                                a.score = n.score;
                                a.seat = q;
                                a.reasons = reasons;
                                cn.allies.push_back(a);
                                cn.best = std::max(cn.best, n.score);
                            }
                            if (!cn.allies.empty())
                                cards.push_back(std::move(cn));
                        }
                    };
                    collect(me.hand.Items());
                    collect(me.board.Items());
                    if (me.gold >= g.GetShop().BuyCost(s.Seat()))
                        collect(me.offers);
                    collect(me.options);   // a Discover's choices (free to take)
                    std::sort(cards.begin(), cards.end(), [](CardNeed const& a, CardNeed const& b)
                    {
                        return a.best != b.best ? a.best > b.best : a.uid < b.uid;
                    });
                    for (CardNeed& cn : cards)
                    {
                        std::sort(cn.allies.begin(), cn.allies.end(), [](AllyNeed const& a, AllyNeed const& b)
                        {
                            return a.score != b.score ? a.score > b.score : a.seat < b.seat;
                        });
                        int shown = 0;
                        for (AllyNeed const& a : cn.allies)
                        {
                            if (shown >= 3)
                                break;
                            if (!nd.empty())
                                nd += ',';
                            nd += Num(cn.uid) + "." + Num(a.seat) + NeedReasons(a.reasons);
                            ++shown;
                        }
                    }
                }
                kv.push_back({ "nd", nd });
            }
            // The hand's temporary Spellcraft cards (uids), left off at the next StartTurn.
            {
                std::string temp;
                for (Minion const& m : me.hand.Items())
                    if (m.temporary)
                        temp += (temp.empty() ? "" : ",") + Num(m.uid);
                kv.push_back({ "tm", temp });
            }
            kv.push_back({ "bd", MinionList(me.board.Items()) });
            kv.push_back({ "svb", SellList(g.GetShop(), me.board.Items()) });   // what each board minion sells for
            kv.push_back({ "op", MinionList(me.options) });
            // A choice of hero powers instead of minions, as the power ids of the options.
            {
                std::string powerOptions;
                for (std::string const& id : me.powerChoices)
                    powerOptions += (powerOptions.empty() ? "" : ",") + id;
                kv.push_back({ "opw", powerOptions });
            }
            std::string tribes;
            for (uint32_t t : g.FactionTribes(FactionOf(s.Seat())))
                tribes += (tribes.empty() ? "" : ",") + Num(t);
            kv.push_back({ "tb", tribes });
            std::string enemyTribes;
            for (uint32_t t : g.FactionTribes(1 - FactionOf(s.Seat())))
                enemyTribes += (enemyTribes.empty() ? "" : ",") + Num(t);
            kv.push_back({ "te", enemyTribes });   // the other faction rolls its own set
            kv.push_back({ "rf", Num(g.GetShop().RefreshCost(s.Seat())) });   // what my next Refresh costs
            // The lobby, public in Battlegrounds: health.tier.alive.eliminationRound.lastOpponent.heroId.armor per seat
            // (lastOpponent: a seat, -3 when it rested last round, -2 before the first fight).
            std::string lobby;
            for (int p = 0; p < kPlayers; ++p)
            {
                PlayerShop const& ps = g.GetShop().State(p);
                if (p != 0)
                    lobby += ',';
                lobby += Num(ps.health) + "." + Num(ps.tier) + "." + (g.Alive(p) ? "1" : "0") + "." +
                    Num(g.EliminationRound(p)) + "." + Num(g.LastOpponent(p)) + "." +
                    (ps.hero >= 0 ? std::string(HeroAt(ps.hero).id) : std::string()) + "." + Num(ps.armor);
                // Two trailing fields, the seat's quest id and its active reward id (either may be empty).
                int const q = g.PlayerQuest(p);
                int const rw = g.PlayerReward(p);
                lobby += "." + (q >= 0 ? std::string(QuestAt(q).id) : std::string());
                lobby += "." + (g.QuestDone(p) && rw >= 0 ? std::string(RewardAt(rw).id) : std::string());
                // The seat's second quest and reward (Sire Denathrius; either may be empty).
                int const q2 = g.PlayerQuest2(p);
                int const rw2 = g.PlayerReward2(p);
                lobby += "." + (q2 >= 0 ? std::string(QuestAt(q2).id) : std::string());
                lobby += "." + (g.QuestDone2(p) && rw2 >= 0 ? std::string(RewardAt(rw2).id) : std::string());
            }
            kv.push_back({ "pl", lobby });
            // The live overall placing of every seat (the server's own OverallPlacement), comma separated.
            std::string ranks;
            for (int p = 0; p < kPlayers; ++p)
                ranks += (p == 0 ? "" : ",") + Num(s.OverallPlacement(p));
            kv.push_back({ "rk", ranks });
            // The seat names (the human's own is empty). Joined by '|' so a comma in a name is safe.
            std::string names;
            for (int p = 0; p < kPlayers; ++p)
                names += (p == 0 ? "" : "|") + s.SeatName(p);
            kv.push_back({ "nm", names });
            // The quest pick, my quest and its progress, and my active reward. Each offered pair carries the
            // minion types it names (Unlikely Duo's two, The Friends Along the Way's one; 0 = none).
            {
                std::string qo;
                int const slot = g.GetShop().OfferQuestSlot(s.Seat());
                QuestState const& qs = slot >= 0 ? g.GetShop().QuestSlot(s.Seat(), slot) : me.quest;
                for (std::size_t i = 0; i < qs.offer.size(); ++i)
                {
                    QuestTypes const t = i < qs.offerTypes.size() ? qs.offerTypes[i] : QuestTypes{};
                    qo += (qo.empty() ? "" : "|") + std::string(QuestAt(qs.offer[i].first).id) + ":" +
                        std::string(RewardAt(qs.offer[i].second).id) + ":" + Num(static_cast<int>(t.typeA)) + ":" +
                        Num(static_cast<int>(t.typeB)) + ":" + Num(static_cast<int>(t.rewardType));
                }
                kv.push_back({ "qo", qo });
            }
            int const myQuest = g.PlayerQuest(s.Seat());
            kv.push_back({ "q", myQuest >= 0 ? std::string(QuestAt(myQuest).id) : std::string() });
            kv.push_back({ "qa", Num(static_cast<int>(me.quest.typeA)) });
            kv.push_back({ "qb", Num(static_cast<int>(me.quest.typeB)) });
            // the type the reward names: the drawn one once it is active, else the one shown on the offer
            kv.push_back({ "rt", Num(static_cast<int>(me.rewardState.friendType != 0 ? me.rewardState.friendType
                                                                                     : me.quest.rewardType)) });
            kv.push_back({ "qp", Num(g.QuestProgress(s.Seat())) });
            kv.push_back({ "qr", Num(g.QuestRequired(s.Seat())) });
            int const myReward = g.PlayerReward(s.Seat());
            kv.push_back({ "rw", g.QuestDone(s.Seat()) && myReward >= 0 ? std::string(RewardAt(myReward).id) : std::string() });
            // Sire Denathrius's second quest and reward.
            int const myQuest2 = g.PlayerQuest2(s.Seat());
            kv.push_back({ "q2", myQuest2 >= 0 ? std::string(QuestAt(myQuest2).id) : std::string() });
            kv.push_back({ "qa2", Num(static_cast<int>(me.quest2.typeA)) });
            kv.push_back({ "qb2", Num(static_cast<int>(me.quest2.typeB)) });
            kv.push_back({ "rt2", Num(static_cast<int>(me.rewardState2.friendType != 0 ? me.rewardState2.friendType
                                                                                       : me.quest2.rewardType)) });
            kv.push_back({ "qp2", Num(g.QuestProgress2(s.Seat())) });
            kv.push_back({ "qr2", Num(g.QuestRequired2(s.Seat())) });
            int const myReward2 = g.PlayerReward2(s.Seat());
            kv.push_back({ "rw2", g.QuestDone2(s.Seat()) && myReward2 >= 0 ? std::string(RewardAt(myReward2).id) : std::string() });
            kv.push_back({ "win", Num(g.WinnerFaction()) });
            return KvEncode(kv);
        }

        // ---------------------------------------------------------------------------------------------- combat

        namespace
        {
            // Cards of the log are written once in a table and referenced by number.
            struct CardTable
            {
                std::vector<int> cards;
                int Ref(int card)
                {
                    if (card < 0)
                        return -1;
                    for (std::size_t i = 0; i < cards.size(); ++i)
                        if (cards[i] == card)
                            return static_cast<int>(i);
                    cards.push_back(card);
                    return static_cast<int>(cards.size()) - 1;
                }
            };

            std::string CombatPayload(Session const& s, bool withEvents)
            {
                HumanFight const& hf = s.Fight();
                CombatResult const& r = hf.fight.result;
                KvList kv;
                kv.push_back({ "rd", Num(hf.round) });
                kv.push_back({ "ms", Num(hf.mySide) });
                kv.push_back({ "opp", Num(hf.opponent) });
                kv.push_back({ "w", Num(r.winner) });
                kv.push_back({ "dm", Num(r.damage) });
                kv.push_back({ "fs", Num(r.firstSide) });
                kv.push_back({ "to", r.timedOut ? "1" : "0" });
                kv.push_back({ "stl", r.stalled ? "1" : "0" });
                kv.push_back({ "s0", MinionList(r.start[0].Items()) });
                kv.push_back({ "s1", MinionList(r.start[1].Items()) });
                kv.push_back({ "v0", MinionList(r.survivors[0].Items()) });
                kv.push_back({ "v1", MinionList(r.survivors[1].Items()) });
                kv.push_back({ "tr", withEvents ? "0" : "1" });
                if (withEvents)
                {
                    CardTable table;
                    std::string events;
                    for (CombatEvent const& e : r.events)
                    {
                        if (!events.empty())
                            events += ',';
                        events += Num(static_cast<int>(e.type)) + "." + Num(e.side) + "." + Num(e.actor) + "." + Num(e.target) +
                            "." + Num(e.amount) + "." + Num(e.amount2) + "." + Num(table.Ref(e.card)) + "." +
                            (e.golden ? "1" : "0") + "." + Num(e.position) + "." + Num(e.keywords);
                    }
                    std::string cards;
                    for (int c : table.cards)
                        cards += (cards.empty() ? "" : ",") + std::string(CardAt(c).id);
                    kv.push_back({ "cd", cards });
                    kv.push_back({ "ev", events });
                }
                return KvEncode(kv);
            }

            bool Fits(std::string const& payload)
            {
                std::vector<std::string> frames;
                return ChunkPayload(kOpBgCombat, kServerPushReqId, payload, frames);
            }
        }

        std::string EncodeBgCombat(Session const& s)
        {
            std::string full = CombatPayload(s, true);
            return Fits(full) ? full : CombatPayload(s, false);
        }

        // ---------------------------------------------------------------------------------------------- end

        std::string EncodeBgEnd(Session const& s)
        {
            Game const& g = s.GetGame();
            KvList kv;
            kv.push_back({ "win", Num(g.WinnerFaction()) });
            kv.push_back({ "myf", Num(FactionOf(s.Seat())) });
            kv.push_back({ "won", s.HumanFactionWon() ? "1" : "0" });
            kv.push_back({ "pf", Num(g.Placement(s.Seat())) });
            kv.push_back({ "po", Num(s.OverallPlacement(s.Seat())) });
            kv.push_back({ "al", s.HumanAlive() ? "1" : "0" });
            kv.push_back({ "rd", Num(g.Round()) });
            kv.push_back({ "tm", g.TimedOut() ? "1" : "0" });
            std::string order;
            for (int p = 0; p < kPlayers; ++p)
                order += (p == 0 ? "" : ",") + Num(s.OverallPlacement(p));
            kv.push_back({ "pl", order });
            // The payout (0 until the glue fills it).
            kv.push_back({ "rc", Num(s.PayoutCoins()) });
            kv.push_back({ "rp", Num(s.PayoutPacks()) });
            return KvEncode(kv);
        }

        std::string EncodeBgAlly(Session const& s, int seat)
        {
            if (!s.IsLivingAlly(seat))
                return std::string();
            Game const& g = s.GetGame();
            PlayerShop const& ps = g.GetShop().State(seat);
            KvList kv;
            kv.push_back({ "se", Num(seat) });
            kv.push_back({ "nm", s.SeatName(seat) });
            kv.push_back({ "hero", ps.hero >= 0 ? std::string(HeroAt(ps.hero).id) : std::string() });
            kv.push_back({ "t", Num(ps.tier) });
            kv.push_back({ "g", Num(ps.gold) });
            kv.push_back({ "hp", Num(ps.health) });
            kv.push_back({ "arm", Num(ps.armor) });
            kv.push_back({ "bd", MinionList(ps.board.Items()) });
            kv.push_back({ "hd", MinionList(ps.hand.Items()) });
            // The hero power and what it costs now (the same shape as BG_STATE's power fields).
            HeroPowerDef const* const pw = g.GetShop().Power(seat);
            kv.push_back({ "pw", pw ? std::string(pw->id) : std::string() });
            kv.push_back({ "pwc", Num(pw ? HeroPowerCost(*pw, ps.power) : 0) });
            int counter = 0;
            if (pw)
            {
                if (pw->countdownTurns > 0)
                    counter = ps.power.powerCountdown;
                else if (pw->usesPerGame > 0)
                    counter = std::max(0, pw->usesPerGame - ps.power.Uses(pw->id));
            }
            kv.push_back({ "pwx", Num(counter) });
            return KvEncode(kv);
        }

        // ---------------------------------------------------------------------------------------------- frames

        void AppendBgActionReply(std::vector<std::string>& out, std::string const& reqId, Session const& s, ActionResult result,
            Action const* action)
        {
            if (result != ActionResult::Ok)
            {
                KvList kv;
                kv.push_back({ "code", ErrorCodeName(ErrorCode::Refused) });
                kv.push_back({ "msg", "That is not possible now." });
                kv.push_back({ "why", ResultName(result) });
                PushFrames(out, "ERR", IsValidReqId(reqId) ? reqId : std::string(kServerPushReqId), KvEncode(kv));
                return;
            }
            if (action && action->type == ActionType::Peek)
            {
                PushFrames(out, kOpBgAlly, reqId, EncodeBgAlly(s, action->slot));
                return;
            }
            if (s.HasFight())
                PushFrames(out, kOpBgCombat, kServerPushReqId, EncodeBgCombat(s));
            PushFrames(out, kOpBgState, reqId, EncodeBgState(s, true));
            if (s.Over())
                PushFrames(out, kOpBgEnd, kServerPushReqId, EncodeBgEnd(s));
        }

        void AppendBgSnapshot(std::vector<std::string>& out, std::string const& reqId, Session const& s)
        {
            PushFrames(out, kOpBgState, reqId, EncodeBgState(s));
            if (s.Over())
                PushFrames(out, kOpBgEnd, kServerPushReqId, EncodeBgEnd(s));
        }

        void AppendBgClose(std::vector<std::string>& out)
        {
            PushFrames(out, kOpBgClose, kServerPushReqId, std::string());
        }

        void AppendBgMalformed(std::vector<std::string>& out, std::string const& reqId)
        {
            KvList kv;
            kv.push_back({ "code", ErrorCodeName(ErrorCode::Malformed) });
            kv.push_back({ "msg", "Malformed request." });
            PushFrames(out, "ERR", IsValidReqId(reqId) ? reqId : std::string(kServerPushReqId), KvEncode(kv));
        }
    }
}
