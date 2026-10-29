#include "HsDeckStore.h"

#include "Database/DatabaseEnv.h"
#include "Log.h"

#include "HsCollectionStore.h"

#include <string>
#include <utility>

namespace
{
    std::string Escaped(std::string s, std::size_t maxLen)
    {
        if (s.size() > maxLen)
            s.resize(maxLen);
        CharacterDatabase.escape_string(s);
        return s;
    }

    // Runs the queued statements synchronously inside BEGIN/COMMIT (rolls back
    // on the first failing statement), or rolls back if something was not
    // queued. Same as in HsCollectionStore.cpp.
    bool FinishTransaction(bool queued, char const* what, uint32_t accountId)
    {
        if (!queued)
        {
            CharacterDatabase.RollbackTransaction();
            sLog.outError("Hearthstone: could not queue the %s of account %u", what, accountId);
            return false;
        }
        if (!CharacterDatabase.CommitTransactionDirect())
        {
            sLog.outError("Hearthstone: commit failed for the %s of account %u; its outcome is unknown, the account is read from the database again", what, accountId);
            return false;
        }
        return true;
    }

    // Queues the deck row of one change (inside a transaction).
    bool QueueDeckRow(uint32_t accountId, Hs::DeckCommit const& c)
    {
        Hs::Deck const& d = c.deck;
        std::string const name = Escaped(d.name, Hs::kMaxDeckNameBytes);
        std::string const cls = Escaped(d.deckClass, 16);
        std::string const cards = Escaped(Hs::EncodeDeckCards(d.cards), Hs::kMaxDeckCardsBytes);
        std::string const origin = Escaped(d.origin, 16);
        uint32 const count = d.CardCount();

        switch (c.action)
        {
            case Hs::DeckAction::Create:
                // A plain INSERT: the primary key fails the whole transaction if
                // the id was ever used (ids are never reused).
                return CharacterDatabase.PExecute(
                    "INSERT INTO `hearthstone_deck` (`account_id`, `deck_id`, `name`, `deck_class`, `cards`, "
                    "`card_count`, `rev`, `origin`, `deleted`) VALUES (%u, %u, '%s', '%s', '%s', %u, %u, '%s', 0)",
                    accountId, d.id, name.c_str(), cls.c_str(), cards.c_str(), count, d.rev, origin.c_str());
            case Hs::DeckAction::Update:
                return CharacterDatabase.PExecute(
                    "UPDATE `hearthstone_deck` SET `name` = '%s', `cards` = '%s', `card_count` = %u, `rev` = %u "
                    "WHERE `account_id` = %u AND `deck_id` = %u AND `deleted` = 0",
                    name.c_str(), cards.c_str(), count, d.rev, accountId, d.id);
            case Hs::DeckAction::Delete:
                // Soft delete: the row stays for audit and recovery.
                return CharacterDatabase.PExecute(
                    "UPDATE `hearthstone_deck` SET `deleted` = 1, `rev` = %u "
                    "WHERE `account_id` = %u AND `deck_id` = %u AND `deleted` = 0",
                    d.rev, accountId, d.id);
        }
        return false;
    }
}

bool HsDeckDbStorage::LoadBook(uint32_t accountId, Hs::DeckBook& out)
{
    // One row of scalar subqueries (see HsCollectionDbStorage::Load): null
    // means a failed query, the counts tell whether a null later is one too.
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COALESCE(MAX(`deck_id`), 0) FROM `hearthstone_deck` WHERE `account_id` = %u), "
        "(SELECT COUNT(*) FROM `hearthstone_deck` WHERE `account_id` = %u AND `deleted` = 0), "
        "(SELECT COUNT(*) FROM `hearthstone_account_class` WHERE `account_id` = %u)",
        accountId, accountId, accountId));
    // Read separately so a missing migration 0005 only costs the starter rows (logged by the table check).
    std::unique_ptr<QueryResult> starterHead(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_account_starter` WHERE `account_id` = %u)", accountId));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot read the decks of account %u "
            "(character migration 0003_hearthstone_decks.sql not applied, or database error)", accountId);
        return false;
    }
    Field* counts = head->Fetch();
    Hs::DeckBook book;
    book.maxDeckId = counts[0].GetUInt32();
    uint64 const deckRows = counts[1].GetUInt64();
    uint64 const classRows = counts[2].GetUInt64();

    if (deckRows > 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT `deck_id`, `name`, `deck_class`, `cards`, `rev`, `origin` FROM `hearthstone_deck` "
            "WHERE `account_id` = %u AND `deleted` = 0 ORDER BY `deck_id`", accountId));
        if (!rows)
        {
            sLog.outError("Hearthstone: cannot read the deck rows of account %u", accountId);
            return false;
        }
        do
        {
            Field* f = rows->Fetch();
            Hs::Deck deck;
            deck.id = f[0].GetUInt32();
            deck.name = f[1].GetCppString();
            deck.deckClass = f[2].GetCppString();
            if (!Hs::DecodeDeckCards(f[3].GetCppString(), deck.cards))
            {
                // Listed as invalid with no cards until the player saves it again.
                deck.damaged = true;
                deck.cards.clear();
                sLog.outError("Hearthstone: deck %u of account %u has an unreadable card list", deck.id, accountId);
            }
            deck.rev = f[4].GetUInt32();
            deck.origin = f[5].GetCppString();
            book.decks[deck.id] = deck;
        } while (rows->NextRow());
    }

    if (classRows > 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT `class`, `source` FROM `hearthstone_account_class` WHERE `account_id` = %u", accountId));
        if (!rows)
        {
            sLog.outError("Hearthstone: cannot read the class unlocks of account %u", accountId);
            return false;
        }
        do
        {
            Field* f = rows->Fetch();
            std::string const cls = f[0].GetCppString();
            if (Hs::IsDeckClass(cls))
                book.unlocks[cls] = f[1].GetCppString();
            else
                sLog.outError("Hearthstone: skipped the unknown class unlock '%s' of account %u", cls.c_str(), accountId);
        } while (rows->NextRow());
    }

    if (!starterHead)
    {
        // The decks stay usable; only starter awards are refused until the table can be read.
        book.startersKnown = false;
        sLog.outError("Hearthstone: cannot read the starter awards of account %u "
            "(character migration 0005_hearthstone_starters.sql not applied, or database error)", accountId);
    }
    else if (starterHead->Fetch()[0].GetUInt64() > 0)
    {
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT `class`, `deck_id` FROM `hearthstone_account_starter` WHERE `account_id` = %u", accountId));
        if (!rows)
        {
            sLog.outError("Hearthstone: cannot read the starter award rows of account %u", accountId);
            return false;
        }
        do
        {
            Field* f = rows->Fetch();
            std::string const cls = f[0].GetCppString();
            if (Hs::IsDeckClass(cls))
                book.starters[cls] = f[1].GetUInt32();
            else
                sLog.outError("Hearthstone: skipped the unknown starter award '%s' of account %u", cls.c_str(), accountId);
        } while (rows->NextRow());
    }

    out = std::move(book);
    return true;
}

bool HsDeckDbStorage::FindDeckOp(uint32_t accountId, std::string const& keyRaw, bool& found, Hs::DeckOp& out)
{
    std::string const key = Escaped(keyRaw, Hs::kMaxDeckKeyLen);
    std::unique_ptr<QueryResult> head(CharacterDatabase.PQuery(
        "SELECT (SELECT COUNT(*) FROM `hearthstone_deck_op` WHERE `account_id` = %u AND `op_key` = '%s')",
        accountId, key.c_str()));
    if (!head)
    {
        sLog.outError("Hearthstone: cannot look up deck key %s of account %u", key.c_str(), accountId);
        return false;
    }
    found = false;
    if (head->Fetch()[0].GetUInt64() == 0)
        return true;

    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT `op`, `deck_id`, `rev` FROM `hearthstone_deck_op` WHERE `account_id` = %u AND `op_key` = '%s'",
        accountId, key.c_str()));
    if (!row)
    {
        sLog.outError("Hearthstone: cannot read deck key %s of account %u", key.c_str(), accountId);
        return false;
    }
    Field* f = row->Fetch();
    out.key = keyRaw;
    out.op = f[0].GetCppString();
    out.deckId = f[1].GetUInt32();
    out.rev = f[2].GetUInt32();
    found = true;
    return true;
}

bool HsDeckDbStorage::CommitDeck(uint32_t accountId, Hs::DeckCommit const& c)
{
    // Ids and counts are numbers; every text is validated by the service and
    // escaped anyway.
    char const* const action = Hs::DeckActionName(c.action);
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for deck %u of account %u", c.deck.id, accountId);
        return false;
    }

    bool queued = QueueDeckRow(accountId, c);
    if (!c.op.key.empty())
    {
        // The primary key (account_id, op_key) fails this statement, and so the
        // whole transaction, if the key was already used.
        std::string const key = Escaped(c.op.key, Hs::kMaxDeckKeyLen);
        std::string const op = Escaped(c.op.op, 8);
        queued = queued && CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_deck_op` (`account_id`, `op_key`, `op`, `deck_id`, `rev`) "
            "VALUES (%u, '%s', '%s', %u, %u)", accountId, key.c_str(), op.c_str(), c.op.deckId, c.op.rev);
    }
    std::string const reason = Escaped(c.reason, 32);
    std::string const source = Escaped(c.source, 64);
    queued = queued && CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_deck_ledger` (`account_id`, `deck_id`, `action`, `rev_after`, `card_count`, "
        "`reason`, `source`) VALUES (%u, %u, '%s', %u, %u, '%s', '%s')",
        accountId, c.deck.id, action, c.deck.rev, c.deck.CardCount(), reason.c_str(), source.c_str());

    if (!FinishTransaction(queued, "deck change", accountId))
    {
        sLog.outError("Hearthstone: the %s of deck %u (rev %u) of account %u was not written", action, c.deck.id,
            c.deck.rev, accountId);
        return false;
    }
    return true;
}

bool HsDeckDbStorage::CommitClass(uint32_t accountId, Hs::ClassCommit const& c)
{
    std::string const cls = Escaped(c.cls, 16);
    std::string const reason = Escaped(c.reason, 32);
    std::string const source = Escaped(c.source, 64);
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for the classes of account %u", accountId);
        return false;
    }
    bool queued;
    if (c.unlock)
        queued = CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_account_class` (`account_id`, `class`, `source`) VALUES (%u, '%s', '%s')",
            accountId, cls.c_str(), source.c_str());
    else
        queued = CharacterDatabase.PExecute(
            "DELETE FROM `hearthstone_account_class` WHERE `account_id` = %u AND `class` = '%s'",
            accountId, cls.c_str());
    queued = queued && CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_class_ledger` (`account_id`, `class`, `action`, `reason`, `source`) "
        "VALUES (%u, '%s', '%s', '%s', '%s')",
        accountId, cls.c_str(), c.unlock ? "unlock" : "lock", reason.c_str(), source.c_str());
    return FinishTransaction(queued, "class access change", accountId);
}

bool HsDeckDbStorage::CommitStarter(uint32_t accountId, Hs::StarterCommit const& c)
{
    std::string const cls = Escaped(c.cls, 16);
    std::string const source = Escaped(c.source, 64);
    if (!CharacterDatabase.BeginTransaction())
    {
        sLog.outError("Hearthstone: BeginTransaction failed for the %s starter award of account %u", cls.c_str(), accountId);
        return false;
    }
    bool queued = true;
    if (c.withDeck)
    {
        queued = QueueDeckRow(accountId, c.deck);
        std::string const reason = Escaped(c.deck.reason, 32);
        std::string const deckSource = Escaped(c.deck.source, 64);
        queued = queued && CharacterDatabase.PExecute(
            "INSERT INTO `hearthstone_deck_ledger` (`account_id`, `deck_id`, `action`, `rev_after`, `card_count`, "
            "`reason`, `source`) VALUES (%u, %u, '%s', %u, %u, '%s', '%s')",
            accountId, c.deck.deck.id, Hs::DeckActionName(c.deck.action), c.deck.deck.rev, c.deck.deck.CardCount(),
            reason.c_str(), deckSource.c_str());
    }
    // A plain INSERT: the primary key fails the whole transaction for a class that was already awarded.
    queued = queued && CharacterDatabase.PExecute(
        "INSERT INTO `hearthstone_account_starter` (`account_id`, `class`, `deck_id`, `source`) VALUES (%u, '%s', %u, '%s')",
        accountId, cls.c_str(), c.withDeck ? c.deck.deck.id : 0u, source.c_str());
    return FinishTransaction(queued, "starter award", accountId);
}

void CheckHsDeckTables()
{
    struct Table { char const* name; char const* migration; };
    Table const tables[] = {
        { "hearthstone_deck", "0003_hearthstone_decks.sql" },
        { "hearthstone_deck_op", "0003_hearthstone_decks.sql" },
        { "hearthstone_deck_ledger", "0003_hearthstone_decks.sql" },
        { "hearthstone_account_class", "0003_hearthstone_decks.sql" },
        { "hearthstone_class_ledger", "0003_hearthstone_decks.sql" },
        { "hearthstone_account_starter", "0005_hearthstone_starters.sql" },
    };
    for (Table const& table : tables)
    {
        std::unique_ptr<QueryResult> found(CharacterDatabase.PQuery("SHOW TABLES LIKE '%s'", table.name));
        if (!found)
            sLog.outError("Hearthstone: table %s is missing in the character database "
                "(migration %s not applied? check Database.AutoUpdate.*)", table.name, table.migration);
    }
}

Hs::DeckService& HsDecks()
{
    static HsDeckDbStorage storage;
    static Hs::DeckService service(storage, HsCollections());
    return service;
}
