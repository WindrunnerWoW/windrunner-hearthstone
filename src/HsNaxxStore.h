/*
 * mod-hearthstone: database glue of the Curse of Naxxramas card item claims. One marker row per account and
 * card item (hearthstone_account_naxx, data/sql/character/0013_hearthstone_naxx.sql) records that the item was used;
 * the marker and the card grant commit in ONE character database transaction.
 */

#ifndef MOD_HEARTHSTONE_HS_NAXX_STORE_H
#define MOD_HEARTHSTONE_HS_NAXX_STORE_H

#include "HsCollectionStore.h"

#include <cstdint>
#include <set>

// The item entries of the account that have a claim marker. True with an empty set when the account has none; false
// (logged) if the table cannot be read.
bool HsNaxxLoadClaims(uint32_t accountId, std::set<uint32_t>& itemEntries);

// Grants the card of the mutation and inserts the claim marker of itemEntry in one character database transaction.
// True only if it committed. A duplicate marker (plain INSERT) fails the transaction, so the card is not granted twice.
bool HsNaxxCommitClaim(uint32_t accountId, Hs::Mutation const& mutation, uint32_t itemEntry, uint32_t characterGuid);

#endif
