# mod-hearthstone wire protocol, version 1

The protocol version is a single integer (`1`). Any incompatible change bumps it; the server
answers a client of another version with `ERR VERSION`.

Implementations: `src/protocol/*` (C++, pure) and `addon/Hearthstone/HsProtocol.lua`
(Lua 5.0). `tests/golden_vectors.txt` and `tests/transcript.txt` are produced by the C++
code and checked by the Lua tests, so both sides stay byte-identical.

## Transport

| Direction | Call | What the core sees |
|---|---|---|
| client -> server | `SendAddonMessage("WRHS", frame, "GUILD")` | `CMSG_MESSAGECHAT`, type `CHAT_MSG_GUILD`, lang `LANG_ADDON`, text `WRHS<TAB>frame` |
| server -> client | `Player::SendAddonMessage("WRHS", frame)` | `SMSG_MESSAGECHAT`, `CHAT_MSG_GUILD`, `LANG_ADDON`; client event `CHAT_MSG_ADDON` (prefix, message, "GUILD", sender = the player's own name) |

- The `GUILD` distribution is used because the core rejects addon language on WHISPER/SAY/YELL and
  PARTY/RAID need a group. It works for unguilded players (verify on the real client, see README).
- The server consumes every `LANG_ADDON` line starting with `WRHS<TAB>` (`OnBeforeSendChatMessage` +
  `CanUseGroupChat` returning false) so it is never relayed to a guild or group. Other messages are untouched.
- The addon accepts a `CHAT_MSG_ADDON` line only if prefix is `WRHS`, channel is `GUILD` and the sender
  is the player itself (guild mates cannot spoof server replies).
- `AddonChannel = 1` must be set in `mangosd.conf`, otherwise the core drops all addon messages.

## Frame

```
WRHS <TAB> v1 <TAB> OP <TAB> reqId <TAB> part <TAB> total <TAB> payload
```

The text after `WRHS<TAB>` is the *frame*; `prefix + TAB + frame` must be **<= 240 bytes** (the client
limit is 255). Fields:

| Field | Rules |
|---|---|
| `v1` | `v` + version number, canonical decimal (no leading zeros, up to 9 digits; a longer or non-canonical one is malformed). Checked first, before the number of fields is looked at: any other number gives `ERR VERSION` (reqId echoed if a valid third field exists, else `0`), whatever follows. |
| `OP` | `[A-Z][A-Z0-9_]*`, 1..12 chars, must not contain a forbidden sequence (see below). |
| `reqId` | 1..8 chars `[0-9a-z]`, chosen by the client, echoed in every reply. `0` is reserved for server pushes and errors without a request. |
| `part`, `total` | canonical decimal, `1 <= part <= total`. A single-frame message is `1` `1`. Up to 3 digits parse; a `total` above `Hearthstone.Protocol.MaxInboundParts` (default 16) gives `ERR TOO_LARGE`. |
| `payload` | Everything after the fifth TAB. Escaped text (below); a raw TAB, `\|`, `:`, `_`, control byte or byte >= 0x80 in the payload makes the frame malformed. |

## Escaping

The core scans addon text for `LFT`, `TW_*`, `TWT_*`, `GM_ADDON`, `MR:`, `TR:` and the
characters `|`, CR, LF, NUL are unsafe in chat text. The payload is therefore escaped; the
encoded payload never contains `| LF CR TAB NUL : _`, any byte outside `0x20..0x7e`, or the substring `LFT`.

| Raw | Encoded | Raw | Encoded |
|---|---|---|---|
| `~` | `~~` | `:` | `~c` |
| `\|` | `~p` | `_` | `~u` |
| LF | `~n` | `L` directly followed by `F` | `~l` |
| CR | `~r` | any other byte < 0x20, 0x7f, >= 0x80 | `~xHH` (lowercase hex) |
| TAB | `~t` | | |

Decoding accepts upper or lower case hex and rejects a dangling `~` or unknown escape.
Escaping `_` and `:` removes every core trigger except `LFT`, which the `~l` rule breaks. Header fields
are checked against the same list when a frame is encoded. Chunk boundaries do not need care: each frame is
escaped on its own, and decoding is context free.

### key=value payloads

Most payloads are `key=value;key=value`. Keys are `[A-Za-z0-9_]+`. Values are first escaped with
`~~` (`~`), `~s` (`;`), `~q` (`=`) and then the whole payload goes through the wire escape above
(so `;` inside a value costs `~~s` on the wire, which is fine). Duplicate keys, empty segments and
bad escapes are malformed.

## Chunking and reassembly

- A payload that does not fit into one frame is split into `total` frames (raw payload split at the
  point where the *escaped* chunk would exceed the 240 byte limit). `part` counts from 1. Maximum 99 parts.
- Receivers reassemble by `(sender, reqId)`. Parts may arrive in any order.
- Server side limits (`mod-hearthstone.conf`): `MaxInboundMessageBytes` (reassembled, decoded bytes),
  `MaxInboundParts`, `ReassemblyTimeoutMs` (from the first part), `MaxPendingRequests` (per player).
- A duplicate part is ignored. A request whose `total` or `OP` changes between parts is dropped with
  `ERR MALFORMED`; exceeding the byte bound is dropped with `ERR TOO_LARGE`; a `total` above
  `MaxInboundParts` gives `ERR TOO_LARGE`; too many unfinished requests give `ERR BUSY`; the timeout sweep
  gives `ERR TIMEOUT`.
- Each `reqId` is answered with at most one error: after a multi-part request was rejected or dropped
  (`ERR BUSY`, `TOO_LARGE`, `MALFORMED`, `TIMEOUT`) its remaining parts are ignored silently.
  **A client that retries must use a fresh `reqId`**: re-sending the same multi-part `reqId` gets no reply
  until 16 other multi-part requests have completed or been dropped. The addon always uses fresh ids.
- The last 16 finished / dropped multi-part `reqId`s are remembered, so late duplicate parts of an
  already handled request are ignored instead of being handled again. A `total` above
  `MaxInboundParts` is such a drop too: only its first part gets `ERR TOO_LARGE`. Single-frame requests never
  touch this list, so a client may reuse a `reqId` for those. A successful `HELLO` clears the list
  (a `/reload`ed addon restarts its counters).
- The addon paces its own sending (one frame per 0.12 s) to stay below the server rate limit and
  applies the same bounds to server replies (32 KiB; 99 parts, 15 s, 8 pending).

## Rate limit

Token bucket per player, `RateLimit.PerSecond` refill, `RateLimit.Burst` size; one token per inbound
frame. An empty bucket drops the frame; at most one `ERR RATE` (reqId `0`) is sent per second.

## Handshake and operations

Before `HELLO` succeeded only `HELLO` and `PING` are accepted; everything else gets `ERR NOT_READY`.

| Op | Dir | Payload | Notes |
|---|---|---|---|
| `HELLO` | C->S | `protocol=1;addon=<addonVersion>;dataset=<datasetId or ->` | `protocol` required. Another number gives `ERR VERSION` and clears the handshake. Optional `ladderui=1`: the client shows the ladder chair window (see "NPC ladder"). |
| `WELCOME` | S->C | `protocol=1;server=<moduleVersion>;dataset=<datasetId>;hash=<16 hex or ->;caps=solo,chunk,packs,decks,wallet,match;maxframe=240;debug=0\|1` | `debug` is `1` only if `Hearthstone.Debug` is on **and** this account may use the debug ops (security >= 2). A trailing `intro=0\|1` says whether a character of the account turned in the introduction's card quest (919703 / 919713); the server always sends it to a player; the addon treats a missing key as `0`. `hash` is the first 16 hex chars of `content_hash` of `data/cards/cards.normalized.json`. `caps` ends with `,arena` when the draft arena is usable (tables, card data and matches ready); without `match` it is never listed. `,heroes` follows when the hero portraits are usable (character migration 0021). |
| `PING` | C->S | any, <= 128 decoded bytes | Allowed before `HELLO`. |
| `PONG` | S->C | the same payload | |
| `ERR` | S->C | `code=<CODE>;msg=<text>[;why=<code>]` | `msg` is for humans and may change. `why` only on `REFUSED` answers to deck and match ops. |
| `ECHO` | C->S | `n=<1..40>` **or** `data=<text>` | Debug only, see below. |
| `ECHO` | S->C | for `n=`: `n=<k>;len=<k*100>;sum=<8 hex>;data=<pattern>`; for `data=`: `len=<n>;sum=<8 hex>` | `sum` is Adler-32 of the data (`%04x%04x` of high, low half). Pattern: byte i is `ALPHABET[(i*7 + i/62) mod 62]`, `ALPHABET = 0-9A-Za-z`. |
| `COLL_GET` | C->S | empty (ignored) | The account's card collection. See "Collection" below. |
| `COLL` | S->C | `ver=<version>;n=<entries>;cards=<id>.<total>.<protected>,...` | Reply to `COLL_GET`, chunked as needed. `ERR UNAVAILABLE` instead if it cannot be read. |
| `COLL_CHANGED` | S->C | `ver=<version>` | Push, reqId `0`, always a single frame. |
| `PACK_GET` | C->S | empty (ignored) | The account's unopened packs and latest openings. See "Packs" below. |
| `PACKS` | S->C | `type=classic;stock=<n>;opened=<m>;recent=<opening>,...` | Reply to `PACK_GET`. |
| `PACK_OPEN` | C->S | `key=<12..16 chars [0-9a-z]>` | Opens one pack. Idempotent per key. |
| `PACK_OPENED` | S->C | `id=<seq>;key=<key>;replay=0\|1;stock=<n>;ver=<version>;new=<mask>;cards=<id>,<id>,<id>,<id>,<id>` | Reply to `PACK_OPEN`. |
| `PACK_CHANGED` | S->C | `stock=<n>` | Push, reqId `0`, single frame: the pack stock changed outside `PACK_OPEN`. |
| `DECK_LIST` | C->S | empty (ignored) | The account's decks and class access. See "Decks" below. |
| `DECKS` | S->C | `cap=<max>;n=<k>;start=<CLASS>;unlocked=<CLASS,...>;d<i>=...;m<i>=...;c<i>=...;s<i>=...` | Reply to `DECK_LIST`, chunked as needed. |
| `DECK_SAVE` | C->S | `key=<key>;id=<0\|deckId>;rev=<0\|base rev>;class=<CLASS>;name=<name>;cards=<id>.<count>,...` | Creates (`id=0`) or updates a deck. Idempotent per key. |
| `DECK_SAVED` | S->C | `key=<key>;id=<deckId>;rev=<rev>;replay=0\|1;state=<state>` | Reply to `DECK_SAVE`. |
| `DECK_DELETE` | C->S | `key=<key>;id=<deckId>;rev=<rev>` | Deletes a deck. Idempotent per key. |
| `DECK_DELETED` | S->C | `key=<key>;id=<deckId>;replay=0\|1` | Reply to `DECK_DELETE`. |
| `DECK_CHANGED` | S->C | `why=gm\|class\|starter` | Push, reqId `0`, single frame: decks or class access changed outside this client. |
| `GAME_GET` | C->S | empty (ignored) | The character's match, a resync. See "Matches" below. |
| `GAME_ACT` | C->S | `mid=<matchId>;seq=<seq>;a=<mull\|play\|atk\|hp\|end\|concede>[;s=][;t=][;p=][;r=]` | One action of the player. |
| `GAME_LEAVE` | C->S | `mid=<matchId>` | Forgets a finished match. |
| `GAME` | S->C | `mid=...;seq=...;ph=...;...;ev=...` or `ph=none` | Reply to the three ops above (same reqId), or a push (reqId `0`, chunked as needed). |
| `GAME_PAUSE` | C->S | `mid=<matchId>` | Saves and suspends a running ARENA duel and returns the player to WoW control. Answered with `GAME ph=none`. Ordinary matches refuse with `why=ordinary`. See "Draft arena". |
| `ARENA_GET` | C->S | empty (ignored) | The account's arena run. Answered with `ARENA`. |
| `LADDER_GET` | C->S | empty | Refresh the window at the chair the player used. Answered with `LADDER`. |
| `LADDER_PLAY` | C->S | `npc=<entry>;deck=<id>` | Confirm an offered opponent and owned playable deck; begin the existing walk-up. |
| `LADDER_RES` | C->S | `duel=<id>` | Resume this account's paused ranked duel at this chair. |
| `LADDER_GIVE` | C->S | `duel=<id>` | Surrender this paused ranked duel after the addon confirmation (a loss). |
| `LADDER` | S->C | see "Ladder opponent window" | Reply to a ladder request, chunked as needed. |
| `LADDER_OPEN` | S->C | same as `LADDER` | Chair push, reqId `0`: open the opponent window. |
| `LADDER_CLOSE` | S->C | empty | Push, reqId `0`: no longer seated within reach of the chair; close the window. |
| `ARENA_CLASS` | C->S | `key=<key>;class=<CLASS>[;run=<id>]` | Chooses one of the three offered classes. Idempotent per key. |
| `ARENA_PICK` | C->S | `key=<key>;no=<1..30>;id=<cardId>[;run=<id>]` | Picks one card of the open offer. Idempotent per key. |
| `ARENA_CLAIM` | C->S | `key=<key>;id=<cardId>[;run=<id>]` | Chooses the special card of a ten-win run. Idempotent per key. |
| `ARENA_DUEL` | C->S | `key=<key>[;run=<id>]` | The Play button. Starts the next duel (`st=ready`) or resumes the paused one, only in reach of the host that opened the window. Idempotent per key. |
| `ARENA_RETIRE` | C->S | `key=<key>[;run=<id>]` | The Retire button. Ends the run and settles its prize, only in reach of the host that opened the window. Idempotent per key. |
| `ARENA` | S->C | see "Draft arena" | Reply to the six ops above. |
| `ARENA_UPD` | S->C | the same payload as `ARENA` | Push, reqId `0`: the run changed. |
| `ARENA_OPEN` | S->C | the same payload as `ARENA` | Push, reqId `0`: the player chose "continue drafting" at the host; open the arena screen. |
| `ARENA_CLOSE` | S->C | empty | Push, reqId `0`: the player walked away from the host that opened the arena screen; close it (Play and Retire stop working). |
| `WALLET_GET` | C->S | empty (ignored) | Coins, dust, dust values, pack prices, crafting access. See "Wallet and crafting" below. |
| `WALLET` | S->C | `coins=;dust=;craft=0\|1[;npc=];scrap=<c>.<r>.<e>.<l>;make=<c>.<r>.<e>.<l>;packcoins=` | Reply to `WALLET_GET`. |
| `WALLET_UPD` | S->C | `coins=;dust=;why=match\|buy\|gm[;gain=]` | Push, reqId `0`, single frame: coins or dust changed outside `CRAFT_*`. |
| `HERO_GET` | C->S | empty (ignored) | The account's hero portrait state. Answered with `HEROES`. See "Hero portraits" below. |
| `HERO_SELECT` | C->S | `class=<CLASS>;id=<id\|default>` | Chooses the portrait of a class (cosmetic). Answered with `HEROES`. Not keyed: selecting the same thing again is a plain success. |
| `HERO_UNLOCK` | C->S | `key=<key>;id=<id>` | Spends one Legend #1 credit on a portrait. Idempotent per key. Answered with `HEROES`. |
| `HEROES` | S->C | `credits=<n>;own=<id,...>;sel=<CLASS>.<id>,...` | Reply to the three ops above. `own` lists the unlocked non-default portraits, `sel` the classes whose choice is not the default. |
| `HERO_CHANGED` | S->C | `why=legend1\|gm` | Push, reqId `0`, single frame: a credit or a portrait changed outside this client. The addon sends `HERO_GET`. |
| `CRAFT_ACCESS` | S->C | `open=0\|1[;npc=<name>]` | Push, reqId `0`, single frame: a crafting NPC was opened or left. |
| `INTRO_DONE` | S->C | empty | Push, reqId `0`: the account just turned in the introduction's card quest (919703 / 919713); the addon shows its micro menu button. |
| `CRAFT_MAKE` | C->S | `key=<key>;card=<id>` | Crafts one copy at the open crafting NPC. Idempotent per key. |
| `CRAFT_SCRAP` | C->S | `key=<key>;card=<id>;n=<1..99>` | Scraps unprotected copies at the open crafting NPC. Idempotent per key. |
| `CRAFT_EXTRAS` | C->S | `key=<key>` | Scraps every extra copy of the collection at the open crafting NPC (all cards in one commit). Idempotent per key. |
| `CRAFTED` | S->C | `key=;replay=0\|1;kind=craft\|scrap\|extras;card=;n=;delta=;dust=;ver=;total=;prot=` | Reply to `CRAFT_MAKE` / `CRAFT_SCRAP` / `CRAFT_EXTRAS`. |

Error codes: `VERSION`, `MALFORMED`, `TOO_LARGE`, `RATE`, `UNKNOWN_OP`, `NOT_IMPLEMENTED`,
`DISABLED`, `TIMEOUT`, `BUSY`, `NOT_READY`, `UNAVAILABLE` (the collection or the packs could not be
loaded or written, for example a database failure or card data missing on the server; retrying later may work),
`NO_STOCK`, `REFUSED` (a business rule rejected the
request, for example a card of the rolled pack is already at the 999 copy limit; nothing was changed. Deck
answers add `why=<code>`, see "Decks". Match answers too, see "Matches". Craft answers too, see
"Wallet and crafting").

Version mismatch: the server answers with `ERR VERSION` whose text tells the player to update the addon
(or the server). The addon prints it in red. A server reply with an unknown frame version makes the addon
print the same advice.

`Hearthstone.Enable = 0`: the module answers only `HELLO`, with `ERR DISABLED`; everything else from
`WRHS` is silently consumed.

### Debug operations

`ECHO` works only when `Hearthstone.Debug = 1` **and** the account security level is >= 2 (moderator).
Otherwise it gets `ERR NOT_IMPLEMENTED`. It exercises chunked replies (`n=`) and multi-part requests
(`data=`) and is used by the protocol tests; the addon does not expose it.

### Collection

Card ownership is account-wide and lives only on the server. The addon never sends counts, and no op
accepts them: `COLL_GET` ignores its payload, and every other `COLL_*` op is reserved.

- `COLL_GET` (C->S, empty payload) is answered with one `COLL` message (same reqId), chunked like any
  reply, or with `ERR UNAVAILABLE`. Before `HELLO` it gets `ERR NOT_READY` like every other op.
- `COLL` payload: `ver=<version>;n=<entries>;cards=<entry>,<entry>,...`
  - `ver`: the account's collection version (decimal, 0 for an account that never owned a card). The server
    bumps it with every committed change.
  - `n`: the number of entries in `cards`.
  - each entry is `<cardId>.<total>.<protected>`: the dataset card id (for example `CS2_022`, not the dbf id;
    `[A-Za-z0-9_]`, up to 32 characters), the owned copies (1..999) and how many of them are protected
    (0..total; protected copies can never be scrapped, D21). Only cards with total > 0 are listed, sorted by id in
    byte order (so `A1` < `A_` < `B` < `b`). No entries gives `n=0;cards=` (empty value).
  - Example: `ver=7;n=3;cards=CS2_022.2.1,EX1_062.1.1,EX1_116.1.0` (on the wire `_` is `~u`).
  - Size: an entry is the id plus at most 8 bytes. The current dataset has 412 ownable cards (378 standard, 4 promo, 30 Naxxramas) with ids of up to 8
    characters, so even all of them at 999 copies is about 7 KB (about 38 frames), well below the addon's 16 KiB
    reply bound and the 99 frame limit.
- `COLL_CHANGED` (S->C push, reqId `0`, one frame): `ver=<version>`. Sent after a committed change (for example a
  GM grant) to the account's character if it is in the world and has completed `HELLO`. The addon then sends
  `COLL_GET` (at most one outstanding request). It is never sent for an uncommitted change.
- `ERR UNAVAILABLE`: the collection could not be read (database failure, missing migration) or the server could not
  load the card rules of its dataset at startup. The addon shows the collection as unavailable (the catalog stays usable).

### Packs

One pack type exists: `classic`, the combined Basic + Classic booster (five cards, at least one Rare or better, D07).
Unopened packs, the Legendary bad-luck counter and every opening are stored per account on the server. The addon never
sends card ids or counts for packs either; the server rolls the cards.

- `PACK_GET` (C->S, empty payload) is answered with one `PACKS` message (same reqId) or `ERR UNAVAILABLE`.
- `PACKS` payload: `type=classic;stock=<n>;opened=<m>;recent=<opening>,<opening>,...`
  - `stock`: unopened packs (0..100000). `opened`: packs this account ever opened (the `seq` of the newest opening).
  - `recent`: the newest openings first, at most 5 (empty value when none). Each is
    `<seq>.<mask>.<id1>.<id2>.<id3>.<id4>.<id5>`: the per-account opening number (1, 2, ...), the new-card mask
    (below) and the five dataset card ids in slot order.
  - Example: `type=classic;stock=2;opened=7;recent=7.10000.EX1_116.CS2_022.CS2_022.EX1_011.CS2_189`.
  - The Legendary bad-luck counter is not sent (like the original game, it is hidden; `.hs pack show` shows it).
- `PACK_OPEN` (C->S): `key=<key>`, 12..16 characters `[0-9a-z]`, chosen by the client, fresh for every pack the
  player means to open. It is the idempotency key, **not** the reqId (reqIds restart after `/reload`):
  - The server stores the key with the opening, unique per account, forever. A `PACK_OPEN` whose key the account
    already used returns that stored opening with `replay=1` and changes nothing (no pack is consumed, no card is granted).
  - So after a lost reply or `ERR UNAVAILABLE` the client retries with the **same key** (and a new reqId). A retry can
    never open a second pack, even across relog or a server restart.
  - Answered with `PACK_OPENED` (same reqId), `ERR NO_STOCK` (no unopened pack; nothing changed), `ERR MALFORMED`
    (missing or bad key), `ERR REFUSED` or `ERR UNAVAILABLE` (nothing changed, or, if the database outcome is unknown,
    a retry with the same key tells).
- `PACK_OPENED` payload: `id=<seq>;key=<key>;replay=0|1;stock=<n>;ver=<version>;new=<mask>;cards=<id>,<id>,<id>,<id>,<id>`
  - `id`: the opening number (`seq` of `PACKS.recent`). `key`: the request's key. `stock`: unopened packs now.
  - `ver`: the collection version right after this opening committed (the addon refetches the collection when its own
    version differs). With `replay=0` the server also pushes `COLL_CHANGED ver=<ver>` right after this reply.
  - `new`: five `0`/`1` digits in slot order; `1` means the account owned no copy of that card before this pack and no
    earlier slot of the same pack holds it ("NEW" in the reveal).
  - `cards`: exactly five dataset card ids in slot order. Duplicates are allowed. The cards are granted unprotected
    (D21: booster copies can be scrapped later).
- `PACK_CHANGED` (S->C push, reqId `0`, one frame): `stock=<n>`, sent after a committed stock change that did not come
  from this client's `PACK_OPEN` (for example a GM grant) to the account's handshaken character.

Pack contents (server side, configurable in `mod-hearthstone.conf`, community-estimated 2014 values, not official):
each of the five slots rolls a rarity by weight (Common 716, Rare 228, Epic 44, Legendary 11, in tenths of a percent)
and then a card uniformly among the standard collectible cards of that rarity. Basic (`FREE`) cards are in the Common
pool. Promo (`reward_only`), unreleased and non-collectible cards never appear. If the account has opened
`LegendaryPity - 1` packs in a row without a Legendary (default 40), the lowest-rarity slot becomes a Legendary. Then, if
all five are Common, the last slot becomes a Rare. The counter is reset by any pack with a Legendary and is written in
the same transaction as the opening.

### Decks

Decks are account-wide: every character of an account sees the same decks. Which of them a character may play depends
on the character (class access, below). Drafts can be saved: a saved deck need not be complete, it carries the reasons
it is incomplete instead. Ownership is never sent by the client: the server checks it against the account's collection.

Classes: `DRUID HUNTER MAGE PALADIN PRIEST ROGUE SHAMAN WARLOCK WARRIOR` (upper case on the wire). `NEUTRAL` cards fit
every deck; cards of any other class (for example `DREAM`) fit none. A character's starting class is the Hearthstone
class of the same name as its WoW class (warrior, paladin, hunter, rogue, priest, shaman, mage, warlock, druid). The
classes a character may play are its starting class plus the account's unlocks (`unlocked`). Owning cards or a deck of
a class never unlocks it. Decks of any class can be created and edited, also for a class the character cannot play.

- `DECK_LIST` (C->S, empty payload) is answered with one `DECKS` message (same reqId) or `ERR UNAVAILABLE`.
- `DECKS` payload: `cap=<max>;n=<k>;start=<CLASS>;unlocked=<CLASS,...>` followed, for i = 1..n (decks ordered by id),
  by `d<i>=<id>.<rev>.<CLASS>.<origin>;m<i>=<name>;c<i>=<cards>;s<i>=<state>`.
  - `cap`: the most saved decks the account may hold (`Hearthstone.Deck.MaxSaved`, default and maximum 20).
  - `start`: the asking character's starting class, `-` if it has none. `unlocked`: the account's unlocked classes in
    byte order, comma separated, empty value when none.
  - `d<i>`: deck id (per account, never reused), revision (1 after the create, +1 per change), class and origin
    (`player`; `gm_fixture` for GM development decks; adds `starter`). An origin may contain `_` (`~u` on the wire).
  - `m<i>`: the deck name (1..24 characters of UTF-8, may contain `;`, `=`, spaces, non-ASCII; value-escaped like any value).
  - `c<i>`: the cards, `<cardId>.<count>,...` sorted by id in byte order; empty value for a deck without cards.
  - `s<i>`: the deck's state for the asking character (below).
  - Example: `cap=20;n=2;start=MAGE;unlocked=WARRIOR;d1=1.1.MAGE.gm_fixture;m1=Dev Mage;c1=CS2_022.2,...;s1=ok;`
    `d2=4.3.PRIEST.player;m2=Holy~sToad;c2=CS2_168.2,CS2_172.1;s2=size,owned,locked` (decoded payload before the
    wire escape; the second name is `Holy;Toad`. `tests/transcript.txt` has a non-ASCII name on the wire).
  - Size: the worst case (29 decks of 30 distinct 8-character ids, 64-byte non-ASCII names, 10-digit ids and revisions)
    is 13922 decoded bytes in 97 frames. 30 such decks would not fit into 99 frames, so an account never holds more than 29 live decks: the cap (at most 20)
    plus room for starter decks awarded past it.
- `state`: `ok`, or a comma list of the codes present, in this order:
  - `size`: fewer than 30 cards.
  - `owned`: the account owns fewer copies (protected + unprotected) of some card than the deck uses. One owned copy
    serves every deck.
  - `invalid`: the stored deck breaks a hard rule now (for example the card data changed), or its card list is damaged.
  - `locked`: the asking character cannot play the deck's class (not a completeness problem; another character may).
  - A deck is complete without `size`, `owned` and `invalid`, playable by the character when also not `locked`. Nothing
    is match-ready only when the match engine also supports every card. That is not part of the state; the
    match start revalidates everything on the server and the opponent's deck menu lists only match-ready decks.
- `DECK_SAVE` (C->S): `key=<key>;id=<0|deckId>;rev=<0|base rev>;class=<CLASS>;name=<name>;cards=<cards>`
  - `key`: 12..16 characters `[0-9a-z]`, fresh for every change the player means to make (the idempotency key, as for
    `PACK_OPEN`). `id=0` creates a deck (`rev` must be `0`); otherwise `rev` is the revision the edit is based on.
  - `name` is trimmed of leading and trailing spaces. `cards` as in `DECKS` (any order; counts 1..3 digits).
  - Answered with `DECK_SAVED` (same reqId): `key=<key>;id=<deckId>;rev=<new rev>;replay=0|1;state=<state>`. The state is
    the saved deck's state for the asking character.
  - A key the account already used for a save of the same deck (or for the create that made it) replays: `replay=1`,
    the stored id and revision, the deck's state now, or `gone` if it was deleted since. Nothing changes. After a lost
    reply or `ERR UNAVAILABLE` the client retries with the **same key** (and a new reqId), even across relog or a
    server restart; a retry never creates a second deck.
  - `ERR MALFORMED`: a missing field, a bad key, `id`/`rev` not decimal (at most 9 digits), `rev` not `0` for a new
    deck, or an unparsable card list (empty entry, id not `[A-Za-z0-9_]{1,32}`, count not 1..3 digits, an id twice).
  - `ERR REFUSED` with `why=` (nothing was written):

    | why | rule |
    |---|---|
    | `class` | the class is not one of the nine, or differs from the stored deck's class (fixed at creation) |
    | `name` | after trimming not 1..24 characters, more than 64 bytes, not valid UTF-8 (overlong forms, surrogates and characters above U+FFFF are refused too), a byte below 0x20, 0x7f or a pipe |
    | `card` | unknown id (ids are exact), not collectible, or neither `standard` nor `reward_only` |
    | `cardclass` | a card of neither the deck's class nor `NEUTRAL` |
    | `copies` | a count of 0, more than 2, or more than 1 of a Legendary |
    | `size` | more than 30 cards |
    | `cap` | creating a deck while the account holds `cap` decks |
    | `missing` | the deck does not exist (never did, or was deleted) |
    | `conflict` | `rev` is not the deck's current revision (changed elsewhere: refetch) |
    | `key` | the key was already used for another operation or deck |

    The card rules are checked in the order `card`, `cardclass`, `copies`, `size` (each for every card first).
  - `ERR UNAVAILABLE`: nothing changed, or the database outcome is unknown; retry with the same key.
- `DECK_DELETE` (C->S): `key=<key>;id=<deckId>;rev=<rev>` -> `DECK_DELETED`: `key=<key>;id=<deckId>;replay=0|1`. Same
  key rules (`replay=1` for the same delete again; `why=key` for a key used by a save or another deck), `why=missing` and
  `why=conflict`, `ERR MALFORMED` (also for `id=0`) and `ERR UNAVAILABLE`. The delete is soft on the server; the id is
  never used again.
- `DECK_CHANGED` (S->C push, reqId `0`, one frame): `why=gm` (a GM created or deleted a deck of the account) or
  `why=class` (a GM changed the account's class access) or `why=starter`. Sent to the account's handshaken character after the change
  committed; the addon refetches `DECK_LIST`. A client's own `DECK_SAVE` / `DECK_DELETE` gets no push.
- GM card grants also push `COLL_CHANGED`.

### Matches

A match is played against a server-controlled opponent (an NPC; never another player or a PlayerBot). It **starts only
through the opponent's gossip menu** on the server ("Play a game of Hearthstone", then a deck): there is no op to start
a match, and no remote or anytime play. The server is the judge of every action. The character's side is `m` (mine) on
the wire, the opponent's `o`. One match per character exists at a time (running, or finished until it is left).

- `GAME_GET` (C->S, payload ignored) -> `GAME` (same reqId). A resync: the whole state, no events (`evs=0;ev=`), and the
  events mark moves to the newest event. Without a match: `GAME ph=none`. The addon sends it after every `WELCOME`
  that lists `match`, so `/reload` restores the match window (or shows the result of a match that ended meanwhile).
- `GAME_ACT` (C->S): `mid=<matchId>;seq=<seq>;a=<action>[;s=<source>][;t=<target>][;p=<position>][;r=<id>,<id>,...]`
  - `mull` needs `r` (the offered card ids to put back; an empty value keeps every card), `play` needs `s` (a hand card;
    `t` defaults to 0 = no target, `p` is a zero-based insertion gap, 0 = leftmost and the current board count =
    rightmost (wire range 0..7, default rightmost)), `atk` needs `s` (the
    attacking minion or hero) and `t`, `hp` takes an optional `t` (the server knows the hero power), `end` and
    `concede` take nothing more. Ids are decimal (1..10 digits); `s`, `t`, `p`, `r` are validated whenever present.
    Any other key, a duplicate key or a bad value is `ERR MALFORMED`.
  - `seq` must equal the match's current `seq` (from the last `GAME`), else `ERR REFUSED why=stale`: the client refetches
    with `GAME_GET` and never resends the action. `concede` needs only the right `mid` (`seq` may be left out).
  - Answered with `GAME` (same reqId) carrying the events since the last `GAME` sent to this character, or `ERR REFUSED`
    with `why=`: `match` (no match with this id), `over` (the match is over), `stale`, or an engine code: `phase`,
    `turn`, `source`, `mana`, `target`, `board`, `attack`, `taunt`, `used`, `requirement`, `mulligan`, `position`,
    `unsupported`. Nothing changes on an error. A repeated action is refused (for example `source` once its card is gone).
- `GAME_LEAVE` (C->S): `mid=<matchId>` -> `GAME ph=none`, also when there is nothing to leave or the id is another one
  (then nothing is dropped). `ERR REFUSED why=active` while that match still runs (surrendering is `a=concede`).
- `GAME` push (S->C, reqId `0`, chunked like any reply): after the match started, after every action of the opponent
  (one at a time, `Hearthstone.Match.AiActionDelayMs` apart), and after the match was interrupted. Only to a handshaken
  character in the world. Pushes carry reqId `0` all the time, so the client never treats one as a duplicate of an
  earlier one (its reassembler does not remember reqId `0`).
- `GAME` payload (decoded; keys in this order, an empty value when there is nothing):
  - `mid=<id>;seq=<n>;ph=mull|play|over;turn=<n>;act=m|o|-;res=-|win|loss|draw|int;why=<code>`. `act` is `-` during the
    mulligan and when over. `res`/`why` are from the viewer's side; `int` = interrupted, no winner. `why`: `-` while
    running, `hero` (a hero died; both: `draw`), `concede`, `turns` (90 turns: draw), `stuck` (Help > Stuck), `teleport`
    (teleport or map change), `death` (the character died), `npc` (the opponent creature left the world or died), `gm`
    (`.hs match end`, or matches switched off), `error`.
  - `mn=<my name>;on=<opponent name>`.
  - per side, `m` = the viewer, `o` = the opponent: `mh` / `oh` hero `<id>.<cardId>.<health>.<maxHealth>.<armor>.<attack>.<flags>`;
    `mp` / `op` hero power `<id>.<cardId>.<cost>.<used 0|1>` (a passive boss hero power, Noth's Raise Dead, and the automatic ones, Polarity Shift / Frost Breath /
    Frost Blast, are always sent as used `1`: they are never an action; the automatic ones show up as an ordinary `hp` event at
    the start of the boss's turn; Kel'Thuzad's change to Chains arrives as a `trig` event with the new power's card id, and the
    hero power field carries the new card from then on); `mw` / `ow` weapon `<id>.<cardId>.<attack>.<durability>`
    (empty: none); `mm` / `om` `<mana>.<maxMana>`; `mk` / `ok` `<handCount>.<deckCount>.<fatigue>`; `mb` / `ob` minions
    left to right `<id>.<cardId>.<attack>.<health>.<maxHealth>.<flags>`; only `mc` (the viewer's hand, in hand order)
    `<id>.<cardId>.<cost>.<attack>.<health>`. Attack and health are current values (buffs and auras included).
  - flags: letters in the order `t` Taunt, `c` Charge, `d` Divine Shield, `w` Windfury, `a` can attack now; `-` when none.
  - `mul=<id>,...`: the viewer's offered cards while its mulligan is pending (else empty).
  - `lg=<entry>,...`: the viewer's legal actions now, entries `<k><source>.<t1>/<t2>/...` with `k` = `p` play, `a`
    attack, `h` hero power; targets ascending, `0` = no target. Order: plays in hand order, attacks (hero first, then
    minions left to right), the hero power. Empty when it is not the viewer's turn. End turn and concede are not listed.
  - `evs=<seq of the first event in ev, 0 when none>;ev=<event>,...`: at most the 40 newest events (consecutive seqs),
    each `<type>.<side m|o|->.<source>.<target>.<amount>.<cardId or ->`. Types: `mull` (amount = cards replaced),
    `turn` (amount = turn number), `draw`, `burn` (drawn into a full hand: destroyed, card revealed), `fat` (fatigue
    damage), `play`, `sum` (amount = the 1-based board position), `atk`, `dmg`, `heal`, `arm`, `pop` (Divine Shield lost), `die` (amount = the 1-based position it had), `eq`, `wbrk`, `tf`
    (transformed), `buff`, `hp` (hero power used), `mana` (The Coin), `end`.
  - Hidden information: the opponent's hand is only a count, a `draw` of the opponent has no card id, and neither deck
    is sent (only its count). Entity ids of deck cards follow the shuffled order, so the id of a hidden draw does not
    tell the card either.
  - The canonical example (with the new keys) is in "Matches" below; this older one is
    no longer produced:

    `mid=7;seq=12;ph=play;turn=3;act=m;res=-;why=-;mn=Jonas;on=Innkeeper Allison;mh=1.HERO_08.27.30.0.0.-;mp=2.CS2_034.2.0;mw=;mm=2.2;mk=4.24.0;mb=40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t;mc=12.CS2_029.4.0.0,15.CS2_168.1.2.1,18.CS2_025.2.0.0,21.GAME_005.0.0.0;oh=3.HERO_01.30.30.2.0.-;op=4.CS2_102.2.1;ow=33.CS2_106.3.1;om=0.2;ok=5.23.0;ob=44.CS2_124.3.1.1.c;mul=;lg=p15.0,p18.0,p21.0,a40.3/44,h2.1/3/40/41/44;evs=31;ev=turn.m.0.0.3.-,draw.m.0.18.0.CS2_025`
  - Size: the worst case (10 hand cards, 7 minions a side with every flag, every target legal, 40 long
    events) is about 5.5 KB decoded, 27 frames.
- Lifecycle: logout, disconnect or a server restart ends an unfinished match without a result message (nothing is kept;
  a reconnect cannot revive it). Human turns are untimed (except while a Nozdormu is in play, see "Matches").

### Matches: rules and AI

The match ops cover the full rules (secrets, Choose One, pending picks, freeze, silence, overload,
combo ...) and the difficulties. The protocol stays v1 and every cap and limit is unchanged: the new keys
are additions to `GAME`, `GAME_ACT` gets two additions, and nothing changes meaning (an old addon would
ignore the new keys). Everything below is on top of "Matches".

- `GAME` payload: keys in this order (new keys marked `+`; an empty value when there is nothing):
  `mid;seq;ph;turn;act;res;why;mn;on;+dif;mh;mp;mw;mm;mk;mb;mc;+mo;+ms;+mx;oh;op;ow;om;ok;ob;+os;+ox;mul;+pk;+opk;lg;evs;ev`
  - `dif=normal|hard|boss`: the opponent's difficulty (the player's choice at the gossip menu; `boss` for a boss
    encounter, which plays the Hard AI). The same for the whole match, also after it ended.
  - `dur=1`: the match is a ranked ladder duel. Closing its window is a pause
    (`GAME_PAUSE`), never a surrender; the server resumes it at a ladder chair. Absent for every other match.
  - Hero flags (the last field of `mh` / `oh`): letters in the order `f` frozen, `i` immune, `a` can attack; `-` none.
  - Minion flags (the last field of an entry of `mb` / `ob`): letters in the order `t` Taunt, `c` Charge, `d` Divine
    Shield, `w` Windfury, `s` Stealth, `f` frozen, `x` silenced, `p` Poisonous, `e` enraged, `i` immune, `n` can't
    attack, `a` can attack now; `-` none (example: `sf` a frozen minion with Stealth, `we` an enraged Windfury minion).
    Attack and health stay the current values with buffs and auras. Stealth of an enemy minion is sent as it is (the
    opponent's minions are public on the board); targeting rules are in `lg`.
  - `mo=<handId>.<option1>/<option2>,...`: the Choose One options (card ids) of the viewer's hand cards, in hand order;
    empty when the hand has no such card.
  - `ms=<id>.<cardId>,...`: the viewer's own secrets in play order. `os=<id>,...`: the opponent's secrets, **ids only**
    (the card stays hidden until it is revealed by a `rev` event).
  - `mx` / `ox` = `<cardsPlayedThisTurn>.<overloadLocked>.<overloadOwed>` of the viewer / the opponent: Combo is active
    when the cards played this turn are above 0 on the side's own turn; `overloadLocked` crystals are unavailable this
    turn, `overloadOwed` will be locked next turn.
  - `pk=<id>.<cardId>,...`: the viewer's pending pick options (Tracking: the top three cards of the deck), empty when
    no pick is pending. While `pk` is not empty only `a=pick` (and `concede`) is accepted and `lg` is empty.
    `opk=1` while the opponent has a pending pick (its options are never sent), else `0`.
  - `lg` entries are `<k><source>[:<choice>].<t1>/<t2>/...`. A Choose One card has **one entry per playable option**,
    in option order after its hand position: `p24:1.40/41` and `p24:2.0` (option 1 targets minion 40 or 41, option 2
    needs no target). Cards without a choice have no `:<choice>` part. Attacks and the hero power never have one.
    Order: plays in hand order (options 1, 2), attacks (hero first, then minions left to right), the hero power.
  - New event codes (`ev`): `sil` (a minion was silenced), `frz` (a character was frozen), `sec` (a secret was put
    into play; the card id is hidden for the opponent's), `rev` (a secret triggered and is revealed; the source is what
    triggered it), `trig` (an ability of the source triggered, card id = the source's card), `dr` (a deathrattle
    resolves), `disc` (a card was discarded, card id revealed), `bnc` (a minion returned to a hand), `ctl` (a minion
    changed controller), `ovl` (overload; amount = crystals), `gen` (a card was generated into a hand; the card id is
    hidden for the opponent's), `dest` (destroyed by an effect), `cnt` (a spell was countered), `pick` (a pick was
    made; the card id is hidden for the opponent's), `crys` (mana crystals changed; amount may be negative), `stl`
    (Stealth was lost). The codes are unchanged.
  - Corruption's applied delayed-destruction enchantment emits `buff` with amount `0`, source = the played
    spell entity and target = the resolved minion. A countered spell emits no mark; a redirected spell marks
    its actual target. The later turn-start `dest` and `die` remain separate events with no early destruction.
- `GAME_ACT` additions:
  - `c=<1|2>`: the Choose One option of a card, **only with `a=play`** (`mid=7;seq=12;a=play;s=24;c=2;t=40`). The value
    must be 1 or 2; `c` with any other action, a value other than 1 / 2, or a duplicate `c` is `ERR MALFORMED`. A
    Choose One card without `c` is refused (`why=choice`), as is `c` on a card with no choice. The option's script
    decides whether a target is needed; the legal choices and targets are in `lg`.
  - `a=pick;s=<option id>` answers a pending pick (the id is one of `pk`); `seq` is required (unlike `concede`) and
    `t`, `p`, `r` are validated but ignored. The server maps it to the engine's `Choose` action.
  - New refusal codes `why=`: `choice` (Choose One option missing or invalid), `pending` (a pick is pending: only
    `pick` or `concede` now; or `pick` without a pending pick), `secret` (that secret is already active on your side).
- The AI acts when it is the active side or when it has a pending pick, and waits while the human has one. Every AI
  decision is deterministic from the match seed and `seq`. The AI's profile follows `dif`: Normal for `normal`, Hard for
  `hard` and `boss`.
- Canonical example (decoded; `tests/protocol_tests.cpp` checks it byte for byte, the addon tests must parse exactly
  this). It keeps the values and adds a stealthy frozen minion of the viewer (`sf`), an enraged Windfury enemy
  minion (`we`), a Choose One card in hand (`mo`, `p24:1.`, `p24:2.`), one own secret and two enemy secrets, the cards
  played this turn, the opponent's overload and no pending pick:

  `mid=7;seq=12;ph=play;turn=3;act=m;res=-;why=-;mn=Jonas;on=Innkeeper Allison;dif=normal;mh=1.HERO_08.27.30.0.0.-;mp=2.CS2_034.2.0;mw=;mm=2.2;mk=5.23.0;mb=40.CS2_172.3.2.2.a,41.CS2_179.3.5.5.t,42.EX1_010.2.1.1.sf;mc=12.CS2_029.4.0.0,15.CS2_168.1.2.1,18.CS2_025.2.0.0,21.GAME_005.0.0.0,24.EX1_154.2.0.0;mo=24.EX1_154a/EX1_154b;ms=26.EX1_289;mx=1.0.0;oh=3.HERO_01.30.30.2.0.-;op=4.CS2_102.2.1;ow=33.CS2_106.3.1;om=0.2;ok=5.23.0;ob=44.CS2_124.3.1.1.c,45.EX1_412.4.2.3.we;os=36,37;ox=0.1.2;mul=;pk=;opk=0;tmr=0;lg=p15.0,p18.0,p21.0,p24:1.40/41/42/44/45,p24:2.40/41/42/44/45,a40.3/44/45,h2.1/3/40/41/42/44/45;evs=31;ev=turn.m.0.0.3.-,draw.m.0.18.0.CS2_025`
- Size: the worst case (10 hand cards that all are Choose One with both options and every target legal,
  7 minions a side with all twelve flags, 5 secrets a side, a three card pick, 40 long events, long names) is 7623 bytes
  decoded, **37 frames** (the shape without the new keys: 5505 bytes, 27 frames). The limit is 99 frames
  (`kMaxPartsHardLimit`), so no cap changes. `TestGameWorstCaseFits` checks both.
- Opponents and difficulty are server configuration, not protocol: the gossip menu shows "Normal" / "Hard" (only the
  difficulties the opponent has a ready deck for; skipped when there is just one, and for bosses), then the deck menu.
  See the module README "Matches".

### Matches: full card pool

Protocol v1 and every cap are unchanged. One key is added to `GAME`; the canonical example above now
carries `tmr=0` right after `opk` (the Lua tests and `tests/protocol_tests.cpp` check that exact string).

- `GAME` payload: key order `...;mul;pk;opk;tmr;lg;evs;ev`.
  - `tmr=<seconds>`: the Nozdormu turn timer (EX1_560, "Players only have 15 seconds to take their turns."). `0` = no
    timer. While at least one Nozdormu that is not silenced is on either board and the match is in its main phase, it is
    the seconds left of the human's turn, **rounded up** (at least `1` while the timer runs; the countdown is
    relative to the server clock at the moment the payload was built). The client counts down locally from the value of
    the newest `GAME` and repaints on every snapshot.
  - Rule (server): the deadline is 15 s after the later of the turn's start and the moment the limit became active during
    that turn. When it passes, the server ends the active side's turn as if that side had sent `end` (a pending pick
    is answered with its first option first); the result reaches the client as a normal `GAME` push (reqId `0`) with
    the next turn. Only the human's turns are timed (the AI paces its actions; on its turns `tmr` is `0`). When the limit goes away mid-turn (Nozdormu silenced
    or dead) `tmr` is `0` again and no turn is cut off. The server checks the deadline on its update tick, so a turn can
    run slightly longer than 15 s (a tick), never shorter.
  - The key is always sent; the addon refuses a payload without it.
- No new event codes. An event code the addon does not know is shown as a generic log line.

### Matches: event slots

Protocol v1, every cap, every key and the 40-event window are unchanged. Two existing event types now carry
information in their `amount` field (the fifth field of `<type>.<side>.<source>.<target>.<amount>.<cardId>`), which was
`0` for them before:

- `sum` (a minion entered a board): `amount` = the **1-based** position the minion has on its side's board right after it
  entered, counted from the left (1 = leftmost, at most 7). A minion played at a chosen position, a summoned token, a
  Deathrattle summon (which takes the dead minion's place), a Mirror Entity copy (rightmost) and Alarm-o-Bot's
  replacement all report where they actually are.
- `die` (a minion died or, like Lord Jaraxxus after his battlecry, left play): `amount` = the **1-based** position it had on
  its side's board **before the death step removed anything**. When several minions die in one death step (one
  Flamestrike, one attack that kills both combatants) every `die` of that step refers to the same board snapshot, in
  event order: the positions are not adjusted for the minions removed before it in the list. A second death step (the
  Deathrattles of the first one killed more minions) refers to the board as it was after the first step's removals and
  summons.
- `0` still means "unknown"; a consumer must treat it that way (an event from an older server, or a site that cannot tell).
  `sum` of a minion that did not enter a board at all (a full board) is never emitted, as before.

A client that ignores `amount` for these two types (every older addon, including the log text of `Match.EventText`)
is unaffected; a client can use the slots to place a transient summon or death animation without reading the board
snapshot. The position is **one-based**; subtract one for a zero-based slot. No golden file contains such events; `tests/golden_vectors.txt` and `tests/transcript.txt` are unchanged.

### Wallet and crafting

**Hearthstone Coins** are a real item (`919601`, stacks to 250; world migration 0006; the owner named the currency,
2026-10-03): a match against an NPC puts them into the character's bags (by mail when the bags are full). **Dust** is an
account-wide balance on the server (D20), from scrapping cards, spent on crafting cards at a crafting NPC. Packs are bought
in the real vendor window of a pack vendor (seeded: the Hearthstone Masters Ezra Chatterton 919501 and Ibelin Redmoore
919502): the listing `919602` costs gold like any vendor item, the listing `919603` costs `Hearthstone.Pack.PriceCoins` coin
items (the module answers `CMSG_BUY_ITEM` for it and gives a `919602`). Right-clicking a `919602` adds it to the account's
unopened packs (`PACK_CHANGED`). None of this has an op: the vendor window is the client's own (D07: no anywhere shop).

- `WALLET_GET` (C->S, empty payload) is answered with `WALLET` (same reqId) or `ERR UNAVAILABLE`.
- `WALLET` payload: `coins=<n>;dust=<n>;craft=0|1[;npc=<name>];scrap=<c>.<r>.<e>.<l>;make=<c>.<r>.<e>.<l>;packcoins=<n>`
  - `coins`: the coin items in the character's bags now (not the bank; the addon recounts its bags on `BAG_UPDATE`).
    `dust`: 0..1000000000. `craft=1` while the character has a crafting NPC open and stands within 15 yards of it (then
    `npc` is its name). `scrap` / `make`: the dust a scrapped copy yields / a crafted copy costs per rarity
    Common.Rare.Epic.Legendary (Basic cards use Common), each 1..1000000; defaults `5.20.100.400` / `40.100.400.1600`.
    `packcoins`: the coins one pack costs at a vendor.
  - Example: `coins=120;dust=45;craft=1;npc=Ezra Chatterton;scrap=5.20.100.400;make=40.100.400.1600;packcoins=10`.
- `WALLET_UPD` (S->C push, reqId `0`, one frame): `coins=<n>;dust=<n>;why=<match|buy|gm>[;gain=<n>]`, the coin items in
  the bags and the dust after a change that did not come from this client's `CRAFT_*`. `why=match` carries `gain`, the
  coins the match paid (the addon tells the player); `buy`: a pack bought for coins; `gm`: a GM changed the dust.
  (`WALLET_CHANGED` would exceed the 12 character op limit.)
- `CRAFT_ACCESS` (S->C push, reqId `0`, one frame): `open=1;npc=<name>` when the player chose "Craft and scrap cards"
  at a crafting NPC (the addon opens the collection in crafting mode), `open=0` when the player left it (more than 15
  yards, another map, dead, logout; checked every half second).
- `CRAFT_MAKE` (C->S): `key=<key>;card=<id>`; `CRAFT_SCRAP` (C->S): `key=<key>;card=<id>;n=<1..99>`. The key follows the
  `PACK_OPEN` rules (12..16 characters `[0-9a-z]`, fresh per action, stored with the outcome forever in
  `hearthstone_craft_op`): a key the account already used returns the stored outcome with `replay=1` and changes
  nothing (also when the card or the kind differ). Answered with `CRAFTED` (same reqId; after a commit followed by the
  push `COLL_CHANGED ver=<ver>`), `ERR MALFORMED` (missing / bad key, card or count), `ERR REFUSED why=<code>` (nothing
  changed) or `ERR UNAVAILABLE` (nothing changed, or the outcome is unknown: retry with the same key).
  - `why` codes: `npc` (no crafting NPC open or too far from it), `card` (unknown, not collectible, not standard: the
    four promo cards can neither be crafted nor scrapped), `dust` (not enough dust), `limit` (craft: the account already
    owns 2 copies, 1 of a Legendary, the most a deck uses; scrap: the dust would exceed the maximum), `copies` (scrap:
    fewer unprotected copies than `n`; starter and quest copies are protected, D21), `count` (scrap `n` outside 1..99).
  - A craft adds one **unprotected** copy (it can be scrapped again). A scrap removes unprotected copies only.
- `CRAFT_EXTRAS` (C->S, session 14): `key=<key>` (the same key rules). Scraps **every extra copy of the collection** in one
  transaction (the "Disenchant Extra Cards" button of the craft panel). A copy is extra when the account owns more of the
  card than a deck can use (more than 2, more than 1 of a Legendary) and the copy is not protected; protected copies
  (starter and quest copies, D21) and cards that cannot be scrapped (not collectible or reward_only) are never touched, so
  no saved deck loses a card. The same `why` codes apply, plus `nothing` (no extra copy at all) and `limit` (the dust would
  exceed the maximum). Answered like `CRAFT_SCRAP` with `CRAFTED kind=extras;card=all;n=<copies scrapped over all
  cards>;delta=<dust gained>;dust=<dust after>;ver=<collection version after>;total=0;prot=0`, followed by
  `COLL_CHANGED` (the addon reloads the collection). The craft op row is stored as a scrap of the card id `all`; one card
  ledger row per card and one wallet ledger row (reason `scrap`) are written in the same transaction. At most 60000 copies
  per op.
  - Example: `key=abcdef777777;replay=0;kind=extras;card=all;n=12;delta=240;dust=640;ver=13;total=0;prot=0`.
- `CRAFTED` payload: `key=<key>;replay=0|1;kind=craft|scrap;card=<id>;n=<copies>;delta=<dust change, negative for a
  craft>;dust=<dust after>;ver=<collection version after>;total=<copies after>;prot=<protected copies after>`.
  - Example: `key=abcdef123456;replay=0;kind=craft;card=EX1_116;n=1;delta=-1600;dust=400;ver=5;total=1;prot=0`.
  - The card row, the collection version, the wallet, both ledger rows and the `hearthstone_craft_op` row are one
    transaction.

Match coins (D22, configurable): a normally completed loss pays `Hearthstone.Coins.Loss` (1) against any opponent, a win
or a draw pays by difficulty (`WinNormal` 3, `WinHard` 4, `WinBoss` 8; the session 11 values divided by 10 because a coin
stack holds 250). A surrender and an interrupted match pay 0. The coins are created by the world update after the match
ended and pushed as `WALLET_UPD why=match;gain=<n>`.

### Draft arena

A solo draft mode: the account pays an entry (or plays its one free introduction run), drafts a deck of 30 cards (one of
three offered classes, then thirty one-of-three card offers) and plays Hard AI duels until one loss or ten wins. The run
belongs to the **account**: any character can continue it. Rules, prizes and storage are in `src/arena/`;
this section is the wire contract. The capability is `arena` (WELCOME `caps`); an addon that does not see it must not send
the ops below.

What the addon can and cannot do:

- It can **view** the run (`ARENA_GET`), **choose the class** and **pick cards** anywhere in the world (no host needed), and
  **claim** the special card. Every one of these is validated on the server against the stored run; the addon sends only
  ids and numbers, never state.
- It can **not** start a run. Entry happens only at the host NPC (Ezra Chatterton / Ibelin Redmoore) through the gossip
  menu, validated there (entry, funds, daily limit).
- It can **start or resume a duel** (`ARENA_DUEL`) and **retire** (`ARENA_RETIRE`) only **at the host**: the server
  remembers the host whose gossip opened the arena screen (`ARENA_OPEN`: "Continue drafting", "Look at my deck", a new
  entry) and accepts the two ops only while the character can interact with that creature (alive, interaction range);
  otherwise `why=host`. When the character leaves that range (checked about every 0.4 s, not during a match) the server
  forgets the host and pushes `ARENA_CLOSE`. The gossip options "Start my next duel" / "Resume my paused duel" / "Retire"
  stay and do the same. A duel is played with the ordinary `GAME` / `GAME_ACT` ops; the server pushes the first `GAME`.
- **Closing the duel window is a pause, never a surrender.** The addon sends `GAME_PAUSE mid=<id>` for an arena duel (a
  `GAME_LEAVE` of a running arena duel is treated the same way by the server). Surrender is only `GAME_ACT a=concede`,
  which for an arena duel counts as a loss (one loss ends the run), so the addon needs a separate, confirmed button for it.
  A `GAME` whose `ph` is `none` after the pause means: no match is running now; the run is `paused` and is resumed at the
  host. An ordinary match refuses `GAME_PAUSE` with `why=ordinary`.

`ARENA` payload (reply to `ARENA_GET`, `ARENA_CLASS`, `ARENA_PICK`, `ARENA_CLAIM`; the same keys in the pushes
`ARENA_UPD` and `ARENA_OPEN`), keys always in this order, an empty value when there is nothing:

| Key | Value |
|---|---|
| `st` | `none` (no run; the other keys are zero or empty), `class` (the class offer is open), `draft` (a card offer is open), `ready` (30 cards drafted: the next duel can start at the host), `duel` (a duel runs), `paused` (a saved duel: resume at the host), `finished` / `retired` (the run ended; the prize is being settled), `settled` (done; a special-card choice may still be open), `repair` (saved with other game data: only a game master can help) |
| `run` | the run id (0 without a run). Pass it as `run=` to make a request refuse with `why=stale` when the run changed meanwhile |
| `en` | `1` while the arena is usable (it always accepts new entries); `0` only when the arena could not load |
| `pay` | how the run was entered: `free`, `gold`, `coins`, `practice` (no cost, no prize) |
| `cls` | the drafted class (`MAGE` ...), empty before the choice |
| `w`, `d`, `l` | wins (a draw counts as a win), how many of the wins were draws, losses |
| `duel` | duels started so far |
| `no` | the number of the open offer: `0` = the class offer, `1`..`30` = card offers; empty when no offer is open. This is the `no=` of `ARENA_PICK` |
| `of` | the open offer: three class names (when `no=0`) or three card ids, comma separated, in the order to show |
| `pk` | card picks made so far (0..30) |
| `dk` | the drafted cards `<cardId>.<copies>,...` sorted by id, for the picked list and the mana curve (there is no copy cap) |
| `pz` | `<packs>.<coins>.<special 0\|1>`: the prize the run pays if it ended now; for an ended run the fixed prize. `special=1`: a choice of one missing special card comes with it |
| `tb` | `<basePacks>.<bonusCoins>`: the terms the run snapshotted at entry (for the prize table: 0 wins nothing; 1..2 one pack; 3..5 the base packs; 6..9 the base packs plus the bonus coins; 10 the same plus a special card, or one more pack when all four sets are complete) |
| `spc` | `0\|1`: a special-card choice is open (`st=settled`); claim with `ARENA_CLAIM` |
| `sp` | the special cards that may be claimed now, `<cardId>.<copies the claim grants>,...` (the missing sets); empty when none or when `spc=0`. With `spc=1` and `sp` empty every set is complete: claiming any of the four card ids turns into one extra pack |
| `sc` | the claimed special card, empty before the claim |
| `live` | `0\|1`: the duel of the run is running on the server now |
| `mine` | `0\|1`: ... and the asking character plays it |
| `rp` | `0\|1`: the request was applied before (idempotent replay); always `0` in pushes |

Requests (single frames; `key` is an idempotency key of 12..16 characters `[0-9a-z]`, fresh for every step the player means
to make; `run` is optional, `0` or absent = the current run; unknown, duplicate or missing keys are `ERR MALFORMED`):

- `ARENA_GET`: payload ignored. Answered with `ARENA`, or `ERR UNAVAILABLE` when the arena is not usable.
- `ARENA_CLASS key=;class=`: `class` must be one of the three in `of` while `st=class`. After it the state is `draft` with the
  first card offer (`no=1`).
- `ARENA_PICK key=;no=;id=`: `no` is the number of the open offer (`ARENA_PICK` for another number is `why=stale`), `id` one
  of the three ids in `of`. After pick 30 the state is `ready`; otherwise the next offer is open. Offers are stored before they
  are shown, so a relog or a retry never rerolls.
- `ARENA_CLAIM key=;id=`: only `st=settled` with `spc=1`. `id` is a special card; the missing copies of its set are granted to
  the account's collection once.
- `ARENA_DUEL key=`: `st=ready` starts the next duel, `st=paused` resumes the paused one (the same checks as the gossip:
  addon handshake, alive, not in combat, no group member in combat, no match running). The reply is `ARENA` (`st=duel`);
  the `GAME` push of the duel follows. Other states are `why=bad_state`.
- `ARENA_RETIRE key=`: any state before the end of the run except a duel that is running (`why=running`). The run ends with
  its wins and is settled like the gossip's retirement; the reply shows `st=settled` (or `retired` until the settlement).
- Answers: `ARENA` (the state after the step; `rp=1` for a replay), `ERR REFUSED` with `why=` (nothing changed) or `ERR
  UNAVAILABLE` (nothing changed or the outcome is unknown: retry with the **same key**).

Refusal codes (`why=` of `ERR REFUSED`; `msg` is a sentence for the player and may change):

| why | meaning |
|---|---|
| `no_run` | the account has no run |
| `stale` | `run=` is not the current run, or `no=` is not the open offer |
| `bad_state` | not possible in the current state (for example a pick while `st=ready`) |
| `bad_class` | not one of the offered classes |
| `not_offered` | the card is not in the open offer |
| `picked` | that offer was answered with another card already |
| `bad_card`, `deck` | the card cannot be drafted, or the finished deck is not legal (server log) |
| `not_special`, `not_missing` | `ARENA_CLAIM`: not a special card, or all copies are owned |
| `repair` | the run needs a game master |
| `unpaid` | the entry fee of the run is not paid yet |
| `key` | the key was already used for a different step |
| `overflow` | `ARENA_CLAIM`: the grant would exceed the copy limit |
| `host` | `ARENA_DUEL` / `ARENA_RETIRE`: the character is not (any more) in reach of the host that opened the arena screen |
| `running`, `world`, `busy`, `paused` | `ARENA_DUEL` / `ARENA_RETIRE`: a match is running, the character cannot play now (dead, combat; `msg` says why), another character of the account plays the duel, or the run needs a resume instead |

`ERR UNAVAILABLE` (retry with the same key): the arena could not read or save, or its card data or tables are missing (`why`
is not sent).

Idempotency: the key is stored in `hearthstone_arena_op` (`account_id`, `op_key`, `result`) after the step was applied. A
known key with the same step answers `ARENA` with `rp=1` and the current state and changes nothing; a known key with another
step is `why=key`. The steps are idempotent on their own as well (the same class, pick or claim again is a replay), so a
crash between the step and the key row is harmless.

Pushes (reqId `0`, chunked like any reply, only to the account's character in the world that finished `HELLO`):

- `ARENA_UPD`: the stored run changed (an entry, a pick, a duel started, paused, resumed or ended, the end of the run, the
  settlement, a claim, a retirement by the host or a game master). Sent shortly after the change (the server collects the
  writes of a world tick), also for the client's own requests: the state is identical to the reply, so the addon simply
  replaces its model. The introduction quest turn-in also triggers one. Replace the model; do not diff.
- `ARENA_OPEN`: the player chose "Continue drafting" / "Look at my deck" (or just started a run) at the host. Open the arena
  screen with this state; Play and Retire work from now on.
- `ARENA_CLOSE` (empty payload): the character walked away from that host (or it is gone). Close the arena screen if it was
  opened by `ARENA_OPEN`; Play and Retire do not work any more (a screen opened by the player elsewhere is not affected).
- `GAME` pushes of a started or resumed duel are the ordinary ones. Every interruption of an arena duel (logout, teleport,
  death, the host leaving the world, a game master) **suspends** it: a `GAME ph=none` push, the run becomes `paused`.

Typical flow for the addon: `WELCOME caps` has `arena` -> `ARENA_GET` -> render by `st` -> on `ARENA_UPD` / `ARENA_OPEN` replace
the model. `st=class` or `st=draft` shows the offer (`no`, `of`), the picked list (`dk`) and a mana curve built from the card
database; `st=ready` / `paused` shows Play (lit only after `ARENA_OPEN`, until `ARENA_CLOSE`); `st=duel` is the match window; `st=finished` / `retired` /
`settled` shows `w`, `l` and `pz`; `spc=1` shows the special-card choice from `sp`.

### Reserved (reply `ERR NOT_IMPLEMENTED`)

`COLL_*` except `COLL_GET`, `PACK_*` except `PACK_GET` / `PACK_OPEN`, `DECK_*` except `DECK_LIST` / `DECK_SAVE` /
`DECK_DELETE`, `GAME_*` except `GAME_GET` / `GAME_ACT` / `GAME_LEAVE` / `GAME_PAUSE`, `WALLET_*` except `WALLET_GET`, `CRAFT_*` except
`CRAFT_MAKE` / `CRAFT_SCRAP` / `CRAFT_EXTRAS`, `HERO_*` except `HERO_GET` / `HERO_SELECT` / `HERO_UNLOCK`, `ARENA_*` except `ARENA_GET` / `ARENA_CLASS` / `ARENA_PICK` / `ARENA_CLAIM` /
`ARENA_DUEL` / `ARENA_RETIRE`, `LADDER_*` except `LADDER_GET` / `LADDER_PLAY` / `LADDER_RES` / `LADDER_GIVE`
(`LADDER_OPEN` and `LADDER_CLOSE` are server pushes only),
`NPC_START`. They are reserved for future use. Design
constraint: a match only starts from a **validated NPC gossip interaction** (the server checks the current
gossip GUID and that the player can interact with the creature). There is no remote / anytime play; `NPC_START` could at
most request what the gossip flow already allows.

### Hero portraits (reward track)

Alternative hero portraits are cosmetic: the hero power, health and rules of a class never change. The catalog (80
portraits, `data/heroes/portraits.json`) and its default per class are in the addon (`HeroPortraits.lua`) and the server
(`HsHeroRows.inc`), both generated. Every Legend #1 reach of a league pays one credit; a credit unlocks one portrait.

- `HERO_GET` (empty payload) -> `HEROES`, or `ERR UNAVAILABLE` (tables not readable).
- `HEROES` payload: `credits=<n>;own=<id,...>;sel=<CLASS>.<id>,...` (ids up to 32 characters of `[A-Za-z0-9_]`, sorted).
  `credits` is the number of unspent credits. A stored choice that is not owned, or of another class, is left out.
- `HERO_SELECT`: `class=<CLASS>;id=<id|default>`. The id must be the class default, `default`, or an owned portrait of
  that class. Refusals: `ERR REFUSED why=class` (unknown class, or the portrait belongs to another class), `why=unknown`
  (not in the catalog), `why=locked` (not owned). A malformed payload is `MALFORMED`.
- `HERO_UNLOCK`: `key=<12..16 chars [0-9a-z]>;id=<id>`. One transaction claims the oldest unclaimed credit (storing the key)
  and inserts the portrait. The same key again returns the current `HEROES` and changes nothing. Refusals:
  `why=nocredit`, `why=owned`, `why=unknown` (not in the catalog, or a class default).
- `HERO_CHANGED` (S->C push, reqId `0`): `why=legend1` (a credit was earned) or `why=gm`.
- Storage: character migration `0021_hearthstone_hero_portraits.sql`. Credits are keyed by (account, league), so a repeated
  Legend #1 result adds nothing.

### Idempotency of mutating ops

Client-initiated mutations carry a client-chosen idempotency key in the payload (`PACK_OPEN key=`, `DECK_SAVE key=`,
`DECK_DELETE key=`, `CRAFT_MAKE key=`, `CRAFT_SCRAP key=`, `CRAFT_EXTRAS key=`, `HERO_UNLOCK key=`, `ARENA_CLASS key=`, `ARENA_PICK key=`, `ARENA_CLAIM key=`, `ARENA_DUEL key=`, `ARENA_RETIRE key=`), not the reqId. The server stores the key durably with the committed result, in the same
transaction, so a retry after a lost reply returns the original result and never applies twice. Later mutating ops
follow the same pattern.

## Ladder opponent window

`HELLO` adds optional `ladderui=1`. Only clients advertising it receive the addon window; older clients keep the chair's
gossip pages. No protocol version or message-size limits change. Views use the usual key/value escaping and chunking.

- `st=offers;rank=<0..25>;stars=<0..5>;maxstars=<0..5>;legendpos=<position>;wins=<n>;losses=<n>;draws=<n>;n=<0..3>;dn=<deck count>;note=<text>`.
  Rank 0 is Legend. Each rival has `o<i>id`, `o<i>name`, `o<i>class`, `o<i>faction`, `o<i>rank`, `o<i>stars`,
  `o<i>wins`, `o<i>losses`, `o<i>draws` (the player's record against this NPC). Each ready owned deck has
  `d<i>id`, `d<i>name`. All indices start at 1. Library deck names and AI difficulty are not exposed.
- `st=paused;duel=<id>;npc=<entry>;name=<opponent>;class=<CLASS>` replaces the offers and deck list with Resume and
  Surrender. The client preserves the duel id as a decimal string; both actions check it against the current stored duel.
- `st=waiting[;name=<opponent>]` disables selection while the opponent walks up or a result is queued. The open addon
  polls `LADDER_GET` every three seconds until the board opens or offers are available again.

The rank badge accepts all 25 planned ranks now. `maxstars` is the server's actual `MaxStars(rank)`; the client lays out
two, three, four or five star slots and paints exactly `stars` gold. Gameplay uses 25 ranks with the original stars per rank (2/3/4/5 stars, 2026-10-16).
`legendpos` is the player's actual 1-based position in the shared ladder order, only populated for Legend, otherwise 0.
Legend displays that number inside the orange gem and has no stars. The server also sends these standing fields with
paused/waiting views; brief play acknowledgements without them retain the client's last known standing. `maxstars`
is always sent with the standing.

Each action is checked against the character's remembered chair use, map, distance, alive/seated state, quest unlock,
current offer set, current playable owned decks, table claim and account duel state. Requests run outside the session
registry lock. The table claim and durable duel checks prevent duplicate starts; a stale surrender never targets a new
duel. The addon sends one action at a time, does not automatically retry mutations and never reopens for a late reply.
Closing the selection window sends no surrender. Closing the running match retains the existing `GAME_PAUSE` behavior.

## Battlegrounds

The Warsong Battlegrounds use their OWN chat prefix, `WRBG`, and a separate per-character session (its own rate limit and
reassembly), so the `WRHS` path is completely unchanged. They are always on; only a failed effect registry (see the
server log) turns them off, and then all `WRBG` traffic is swallowed and no game can start. The frames, escaping and
chunking are the same as `WRHS` (only the prefix differs); `WRBG` is not part of any `HELLO` capability and needs the
character's `WRHS` handshake to have completed. A game is opened by a host NPC (the Silverwing Sentinel / Warsong
Outrider of world migration 0059) or, for a game master, with `.hs bg start`; it is ended with `.hs bg leave`. Logout
or a map change end it too (server pushes `BG_CLOSE`).

Client -> server:

- `BG_GET` — no payload: resend the state (and the result when the game is over).
- `BG_ACT act=<name>;...` — one action, keys by `act`:
  `buy i=<offer>`; `sell z=b|h i=<index>` (board or hand); `play i=<hand slot> s=<board slot> [u=<target uid>]`;
  `cast i=<hand slot> [u=<target uid>]` (cast a spell, 15E batch 3);
  `move f=<from> t=<to>`; `refresh`; `upgrade`; `end`; `freeze on=0|1`; `discover i=<option>`;
  `hero i=<choice>` (pick a hero during the hero phase, 15C); `power [u=<target uid>] [u2=<second uid>] [o=<offer or seat>]` (use the hero power; `u2` is Vol'jin's second minion, `o` is Lord Barov's seat);
  `send i=<hand slot> to=<ally seat>` (send a hand minion to a living ally, 15C);
  `quest i=<choice>` (pick a quest + reward pair during the quest phase, 15D);
  `peek to=<ally seat>` (view a living ally's field; the answer is a `BG_ALLY`, see below).

Server -> client:

- `BG_STATE me=..;rd=..;ph=..;al=..;en=..;g=..;t=..;uc=..;bc=..;fz=..;hp=..;cap=..;hc=..;hero=..;arm=..;mhp=..;pw=..;pwc=..;pwx=..;pwu=..;pwt=..;snd=..;gift=..;whl=..;of=..;hd=..;tm=..;bd=..;op=..;tb=..;te=..;rf=..;pl=..;nm=..;qo=..;q=..;qp=..;qr=..;rw=..;qa=..;qb=..;rt=..;win=..`
  is the human's view after every action (reply, same reqId) and after the start (push, reqId 0). `me` is the human's
  seat, `rd` the round, `ph` the phase (`p` hero pick / `q` quest pick / `s` shop / `o` over), `al` the human is alive,
  `en` the turn is ended, `g`/`t` gold and tavern tier, `uc` the upgrade cost, `bc` what the next tavern minion costs
  (15E batch 5e: Friends and Family Discount, Millhouse), `fz` frozen, `hp` health, `cap` the
  15-damage cap is on. `of`, `hd`, `bd`, `op` are the tavern offers, the hand, the board and an open Discover (the
  options; a prize Discover is spell cards, 15E batch 5e), `tm` the uids of temporary Spellcraft cards in the hand (15E batch 3), `tb` the human's faction tribes, `te` the other faction's (15E: each faction rolls its own five), `rf`
  what the human's next Refresh costs (0 when discounted), `win` the winning faction (`-1` while running). `pl` is the lobby, one
  `health.tier.alive.eliminationRound.lastOpponent.heroId.armor.questId.rewardId` per seat (public in Battlegrounds),
  seat 0 first (the ids may be empty).
  15C: `hc` the hero choices offered during the pick (comma-separated hero ids), `hero` the picked hero id, `arm` armor,
  `mhp` max health, `pw`/`pwc` the hero power id and cost, `pwx` the small counter on the power button (Captain
  Eudora's digs left; 0 when the power has none), `pwu` usable (0/1), `pwt` its target kind (`n` none, `f` a
  friendly minion, `o` a tavern offer, `s` a player's seat, `m` two minions of the board or the tavern), `snd` the send of this turn is used, `gift` the card ids allies sent during the
  last action (comma-separated, oldest first; empty when none, and always empty in the answer to `BG_GET`, so a
  refetch does not announce a gift twice); `whl` the outcomes of the Wheel of Yogg-Saron spun during the last action (one digit 0-5 each,
  in `Mysterybox, Hand of Fate, Curse of Flesh, Mindflayer Goggles, Devouring Hunger, Rod of Roasting` order, oldest first; empty like `gift`); `nm` the seat names (8, `|`-separated, the human's own empty; at most 64
  bytes each, never containing `|`).
  15D: `qo` the three offered quest + reward pairs while the quest pick is open, each
  `questId:rewardId:typeA:typeB:rewardType`, pairs joined by `|` (empty otherwise); the types are the minion types the
  pair names as tribe bits (`typeA`/`typeB` Unlikely Duo's two, `rewardType` The Friends Along the Way's one; `0` =
  none), drawn with the offer so the pick can show them (the addon also accepts a bare `questId:rewardId`). `q` the
  human's taken quest id (empty when none); `qp` its progress; `qr` the number it needs; `rw` the human's active reward
  id (empty until the quest is done); `qa`/`qb` the taken quest's two types and `rt` the reward's type (tribe bits, `0`
  = none).
  Also required: `ofz` and `oft`, one digit per tavern offer (`1` = Frozen, `1` = a Silas Darkmoon ticket); `pws` the
  turn is skipped (0/1); `pwh` Maiev's held offer as `cardId.turnsLeft` (empty when none); `pwe` Faelin's held Tier
  minions (at most 3 card ids, comma-separated); `sec` the human's Secrets in play order (at most 5 card ids). Optional:
  `opw` a hero-power choice instead of minion options (power ids, comma-separated), `pwn` the power's current
  "Now: ..." line (at most 40 bytes), `rk` the live placing per seat (8 values, comma-separated; without it the addon
  works the placing out itself).
  `nd` names where the human's hand cards, board minions and affordable tavern offers would help an ally, keyed by the card's `uid`:
  one entry `<uid>.<seat><reasons>` per ally (joined by `,`, best first, at most three allies per card). The reasons
  are `T` (completes a triple), `R` (fits the ally's tribe) and `U` (a clear upgrade or a free slot); several may apply.
  The key is empty outside the shop phase and once the send of the turn is used (`snd=1`). An older addon ignores it.
  `rk` is the live overall placing of every seat (eight comma separated numbers, 1 best, the server's
  `OverallPlacement`); an older server sends none and the addon works the placing out from `pl`.
  `svh` and `svb` are what each hand card and each board minion sells for (Gold, comma separated, one per entry in the
  same order as `hd` / `bd`; a spell is 0). They are optional keys: an older server sends none and the addon assumes 1
  Gold for a minion.
- `BG_ALLY se=..;nm=..;hero=..;t=..;g=..;hp=..;arm=..;bd=..;hd=..;pw=..;pwc=..;pwx=..` — the answer to a
  `peek to=<seat>`: the ally's seat, name, hero, tier, gold, health and armor, its board and hand (the same minion
  format as `BG_STATE`) and its hero power id, cost and counter. Only a living ally of the human's faction can be
  peeked; any other seat is refused with `NotAlly`, so an enemy board is never shown.
- `BG_COMBAT ...` — pushed before the `BG_STATE` of the next round: the human's fight (`rd`, `ms` the human's side, `opp`
  the opposing seat or `-1` for a ghost, `w` the winning side, `dm` the damage, `fs` the first side, `to`/`stl` a
  timeout/stall, `s0`/`s1` the start boards and `v0`/`v1` the survivors). With `tr=0` it carries the animation: `cd` is a
  card id table and `ev` the events `type.side.actor.target.amount.amount2.cardIndex.golden.position.keywords`. A log that
  would not fit the frame limit is sent without events (`tr=1`): the start boards and the survivors only, so the addon can
  jump to the result.
- `BG_END win=..;myf=..;won=..;pf=..;po=..;al=..;rd=..;tm=..;pl=..;rc=..;rp=..` — pushed after the last `BG_STATE`: the
  winning faction, the human's faction and whether it won, the placement within the faction (`pf`, 1-4) and in the whole
  lobby (`po`, 1-8), whether the human finished alive, the round, a timeout, all eight overall placements, and the payout
  (`rc` coins, `rp` packs; 0 when nothing is paid).
- `BG_CLOSE` — pushed (empty): the game was ended on the server (leave, logout, map change).
- `ERR` — a refused action: `code=Refused;why=<reason>` with nothing changed; also `MALFORMED`, `RATE`, `TOO_LARGE`,
  `NOT_READY` (no game open) and the size/limit errors of the shared framing.

Minions are written `id.g.atk.hp.kw.uid` (`g` = golden 0/1, `kw` the keyword bit set including auras, `uid` the
match-local id), lists join them with `,`. A spell in the hand uses the same entry with `atk=hp=kw=0`; the addon's
`BgCardDB.lua` entry has `spell=1`. Card ids are the stable ids of the card table, never table indexes. The addon
reads names, text, tier, tribes and the golden art link from `addon/Hearthstone/Battlegrounds/BgCardDB.lua`.

## Versioning and compatibility

- `WELCOME.protocol` and `v<N>` in every frame name the version; both sides refuse to talk across versions.
- New optional key/value fields and new ops can be added without a bump (unknown keys are ignored,
  unknown ops give `UNKNOWN_OP`). Changing framing, escaping or the meaning of a field needs a bump.
- `caps` lists optional features (`solo`, `chunk`; added `packs`, `decks`, `match`, which
  `Hearthstone.Match.Enable = 0` leaves out, `wallet`, `arena`, `heroes`, listed only when character migration 0021 is applied).
