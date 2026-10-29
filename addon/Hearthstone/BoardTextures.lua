-- Placeholder: no board textures listed.
-- tools/deploy/deploy-addon.ps1 writes the real version of this file into the client's addon folder,
-- listing exactly the pieces it copied to Textures\Board\ (size in pixels, SetTexCoord right/bottom and the
-- anchors from data/art/board_textures.manifest.json, made by tools/art/slice_board.py). It is never
-- written back into the repository. The match window works with this placeholder: every board piece has
-- a native WoW fallback. Use: local p = HS_BoardTextures.pieces["gem_health"]; if p then
-- tex:SetTexture(p.path); tex:SetTexCoord(0, p.right, 0, p.bottom) end
-- Lua 5.0 (WoW 1.12).

HS_BoardTextures = {
    generated = false,
    pieces = {},
}
