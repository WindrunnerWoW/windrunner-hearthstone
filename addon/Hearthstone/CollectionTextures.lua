-- Placeholder: no collection textures listed.
-- tools/deploy/deploy-addon.ps1 writes the real version of this file into the client's addon folder,
-- listing exactly the pieces it copied to Textures\Collection\ (size in pixels and the SetTexCoord
-- right / bottom values from data/art/collection_textures.manifest.json, made by
-- tools/art/slice_collection.py from the owner's collection sheet and book background). It is never
-- written back into the repository. The collection window works with this placeholder: every piece
-- has a native WoW fallback ("today's look"). Use:
--   local p = HS_CollectionTextures.pieces["tab_mage"]
--   if p then tex:SetTexture(p.path); tex:SetTexCoord(0, p.right, 0, p.bottom) end
-- Lua 5.0 (WoW 1.12).

HS_CollectionTextures = {
    generated = false,
    pieces = {},
}
