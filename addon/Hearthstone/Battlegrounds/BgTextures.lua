-- Placeholder: no Battlegrounds portraits listed.
-- The deploy script writes the real version of this file into the client's addon folder (tools/deploy/deploy-addon.ps1),
-- listing exactly the oval portraits it copied to Textures\BgMinions\ (the pool minions, their golden twins and the
-- tokens of the dataset, made by tools/art/minion_portraits.py). It is never written back into the repository.
-- The addon works with this placeholder: every Battlegrounds tile falls back to the text rendering.
-- The same generator lists the round portraits of the 18 curated heroes and their powers (tools/art/bg_heroes.py) and
-- of the 12 in-pool quests and 23 rewards (tools/art/bg_quests.py).
-- Lua 5.0 (WoW 1.12).

HS_BgTextures = {
    generated = false,
    count = 0,
    set = {},          -- [cardId] = true when Textures\BgMinions\<cardId>.blp is installed
    cards = {},        -- [cardId] = true when Textures\BgMinions\Cards\<cardId>.blp (the full 256x512 card, card in the top 388 rows) is installed
    heroes = {},       -- keys of the installed Textures\BgHeroes\<heroId>.blp (curated heroes and bob)
    powers = {},       -- keys of the installed Textures\BgHeroes\Powers\<powerId>.blp (round hero power portraits)
    powerCards = {},   -- keys of the installed Textures\BgHeroes\PowerCards\<powerId>.blp (full hero power cards)
    quests = {},       -- keys of the installed Textures\BgQuests\<questId>.blp (round quest portraits)
    rewards = {},      -- keys of the installed Textures\BgQuests\<rewardId>.blp (round reward portraits)
    rewardBaked = {},  -- the rewardCards whose art HearthstoneJSON has no render of: a real frame with the art, no words (the addon writes them)
    rewardCards = {},  -- keys of the installed Textures\BgQuests\Cards\<rewardId>.blp (the full 256x512 reward cards, card in the top 388 rows)
}
