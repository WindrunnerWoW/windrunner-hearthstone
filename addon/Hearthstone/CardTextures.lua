-- Placeholder: no card textures listed.
-- tools/deploy/deploy-addon.ps1 writes the real version of this file into the client's addon folder,
-- listing exactly the textures it copied to Textures\Cards\ (ids in browse order and their bottom
-- texcoord from data/art/textures.manifest.json). It is never written back into the repository.
-- The addon works with this placeholder: the card widget draws any card id with the default
-- texcoord. Lua 5.0 (WoW 1.12).

HS_CardTextures = {
    generated = false,
    count = 0,
    order = {},
    bottom = {},
}

-- Oval minion portraits (Textures\Minions\<id>.blp, made by tools/art/minion_portraits.py): the deploy script
-- lists the ids it copied here in the client copy. Placeholder: none, the match window crops the card texture.
HS_MinionTextures = {
    generated = false,
    count = 0,
    set = {},
}

-- Round hero power portraits (Textures\Powers\<id>.blp, made by tools/art/power_portraits.py): listed by the deploy
-- script in the client copy. Placeholder: none, the match window crops the card texture.
HS_PowerTextures = {
    generated = false,
    count = 0,
    set = {},
}
