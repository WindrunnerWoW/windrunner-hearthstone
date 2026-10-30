<#
.SYNOPSIS
  Deploys the Hearthstone addon and its card textures to a WoW 1.12-family client.

.DESCRIPTION
  1. Mirrors addon/Hearthstone into <ClientDir>\Interface\AddOns\Hearthstone, with its Textures\ folder going to <ClientDir>\Interface\Hearthstone\Textures (copies new and changed files,
     removes files in that addon folder that are not part of the deployment; nothing outside it is touched).
  2. Copies card textures from art/out/Cards/<id>.blp (Blizzard art, gitignored, made by `python tools/art/setup_art.py`) into
     <addon>\Textures\Cards\ - either the prototype set or all of them.
  3. Writes <addon>\CardTextures.lua into the CLIENT copy only, listing exactly the textures it copied
     (browse order and the per-card bottom texcoord from data/art/textures.manifest.json).
     The placeholder in the repository is not modified.

  -Cards All        every texture in art/out/Cards (about 490 files, 83 MB) (default; the collection window
                    shows the whole catalog and needs them all - cards without a texture fall back to text)
  -Cards Prototype  EX1_116 CS2_029 CS2_106 EX1_306 CS2_034 EX1_145 EX1_506a EX1_614t EX1_131 EX1_320
  -Cards None       addon only, no textures (removes Textures\Cards in the client copy)

  4. Copies the board art pieces from art/out/Board/<name>.blp (made by tools/art/slice_board.py from the
     owner's UI sheets, committed) into <addon>\Textures\Board\ and writes <addon>\BoardTextures.lua into
     the CLIENT copy only (HS_BoardTextures: path, w, h, right, bottom, anchors per piece, from
     data/art/board_textures.manifest.json). Without pieces in art/out/Board the repository placeholder stays.

  -Board All        every piece in art/out/Board that is in the manifest (default, about 1 MB)
  -Board None       no board art (removes Textures\Board in the client copy)

  5. Copies the oval minion portraits from art/out/Minions/<id>.blp (made by tools/art/minion_portraits.py,
     gitignored) into <addon>\Textures\Minions\ and lists them in HS_MinionTextures at the end of the generated
     CardTextures.lua (client copy only; the match window uses them inside the round minion frames and falls back
     to a rectangular crop of the card texture for a card without one). Only ids in data/art/minions.manifest.json.

  -Minions All      every portrait in art/out/Minions that is in the manifest (default, about 6 MB)
  -Minions None     no portraits (removes Textures\Minions in the client copy)

  5b. Likewise the round hero power portraits from art/out/Powers/<id>.blp (tools/art/power_portraits.py) into
      <addon>\Textures\Powers\, listed in HS_PowerTextures of the generated CardTextures.lua (ids in
      data/art/powers.manifest.json). -Powers All (default) / None.

  5c. Likewise the alternative hero portraits from art/out/Heroes/<id>.blp (tools/art/hero_skins.py) into
      <addon>\Textures\Heroes\, listed in HS_HeroTextures of the generated CardTextures.lua (id -> texcoord bottom,
      from data/art/hero_skins.manifest.json). -Heroes All (default) / None.

  5d. The Warsong Battlegrounds oval portraits from art/out/BgMinions/<id>.blp (tools/art/bg_minions.py) into
      <addon>\Textures\BgMinions\, and writes <addon>\Battlegrounds\BgTextures.lua into the CLIENT copy only
      (HS_BgTextures, the ids it copied). The same step copies the hero portraits art/out/BgHeroes/<heroId>.blp and the
      round hero power portraits art/out/BgHeroes/powers/<powerId>.blp (tools/art/bg_heroes.py) into
      <addon>\Textures\BgHeroes\ and <addon>\Textures\BgHeroes\Powers\, listed in HS_BgTextures.heroes / .powers,
      the full minion cards (and golden twins) art/out/BgMinions/cards/<id>.blp into <addon>\Textures\BgMinions\Cards\,
      listed in HS_BgTextures.cards,
      and the round quest and reward portraits art/out/BgQuests/<id>.blp (tools/art/bg_quests.py) into
      <addon>\Textures\BgQuests\, listed in HS_BgTextures.quests / .rewards.
      The window's tiles show them and fall back to the text tile otherwise;
      the gold ring and the coin / tier pip are committed textures (Textures\Bg\, tools/art/bg_textures.py).
      -BgMinions All (default) / None.

  6. Copies the collection art pieces from art/out/Collection/<name>.blp (made by tools/art/slice_collection.py from
     the owner's collection sheet and book background; committed except the Blizzard-derived hero_*.blp strips, which
     setup_art.py makes) into <addon>\Textures\Collection\ and writes
     <addon>\CollectionTextures.lua into the CLIENT copy only (HS_CollectionTextures: path, w, h, right, bottom per
     piece, from data/art/collection_textures.manifest.json). Without pieces in art/out/Collection the repository
     placeholder stays and the collection window uses its native look.

  -Collection All   every piece in art/out/Collection that is in the manifest (default, about 2.5 MB)
  -Collection None  no collection art (removes Textures\Collection in the client copy)

  7. Copies the UI icons from art/out/UI/<name>.blp (made by tools/art/ui_icons.py, committed; the micro menu card
     back) into <addon>\Textures\UI\. Without them the addon uses a stock icon.

  -WhatIf / -DryRun compute and print the plan without changing anything.
  Restart the WoW client afterwards: texture files added while it runs are not picked up.

.EXAMPLE
  powershell -File tools/deploy/deploy-addon.ps1 -WhatIf
  powershell -File tools/deploy/deploy-addon.ps1
  powershell -File tools/deploy/deploy-addon.ps1 -Cards Prototype
  powershell -File tools/deploy/deploy-addon.ps1 -ClientDir D:/Games/TurtleWoW
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    # Root folder of the client (the one that contains WoW.exe and Interface\).
    [string]$ClientDir = 'C:/TurtleDev/04_client/game',
    [ValidateSet('Prototype', 'All', 'None')]
    [string]$Cards = 'All',
    [ValidateSet('All', 'None')]
    [string]$Board = 'All',
    [ValidateSet('All', 'None')]
    [string]$Minions = 'All',
    [ValidateSet('All', 'None')]
    [string]$BgMinions = 'All',
    [ValidateSet('All', 'None')]
    [string]$Powers = 'All',
    [ValidateSet('All', 'None')]
    [string]$Heroes = 'All',
    [ValidateSet('All', 'None')]
    [string]$Collection = 'All',
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$Prototype = @('EX1_116', 'CS2_029', 'CS2_106', 'EX1_306', 'CS2_034',
               'EX1_145', 'EX1_506a', 'EX1_614t', 'EX1_131', 'EX1_320')

$Module   = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$AddonSrc = [IO.Path]::GetFullPath((Join-Path $Module 'addon\Hearthstone'))
$ArtDir   = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\Cards'))
$Manifest = Join-Path $Module 'data\art\textures.manifest.json'
$BoardDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\Board'))
$BoardManifest = Join-Path $Module 'data\art\board_textures.manifest.json'
$MinionDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\Minions'))
$MinionManifest = Join-Path $Module 'data\art\minions.manifest.json'
$BgMinionDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\BgMinions'))
$BgHeroDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\BgHeroes'))
$BgPowerDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\BgHeroes\powers'))
$BgPowerCardDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\BgHeroes\powercards'))
$BgCardDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\BgMinions\cards'))
$BgQuestDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\BgQuests'))
$BgRewardCardDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\BgQuests\cards'))
$BgHeroManifest = Join-Path $Module 'data\art\bg_heroes.manifest.json'
$BgQuestManifest = Join-Path $Module 'data\art\bg_quests.manifest.json'
$BgMinionManifest = Join-Path $Module 'data\art\bg_minions.manifest.json'
$PowerDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\Powers'))
$PowerManifest = Join-Path $Module 'data\art\powers.manifest.json'
$HeroDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\Heroes'))
$HeroManifest = Join-Path $Module 'data\art\hero_skins.manifest.json'
$CollectionDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\Collection'))
$CollectionManifest = Join-Path $Module 'data\art\collection_textures.manifest.json'
$UiDir = [IO.Path]::GetFullPath((Join-Path $Module 'art\out\UI'))
$Client   = [IO.Path]::GetFullPath($ClientDir)
$Target   = [IO.Path]::GetFullPath((Join-Path $Client 'Interface\AddOns\Hearthstone'))
$TexRoot  = [IO.Path]::GetFullPath((Join-Path $Client 'Interface\Hearthstone'))   # Textures\* go here, not into the addon folder
function Get-Dest([string]$rel) { if ($rel -like 'Textures\*') { Join-Path $TexRoot $rel } else { Join-Path $Target $rel } }

# ---- sanity checks (this script deletes files in $Target, so be strict about what it is)
if (-not (Test-Path -LiteralPath $AddonSrc -PathType Container)) { throw "addon source not found: $AddonSrc" }
if (-not (Test-Path -LiteralPath $Client -PathType Container)) { throw "client directory not found: $Client (use -ClientDir)" }
if ((Split-Path -Leaf $Target) -ne 'Hearthstone' -or (Split-Path -Leaf (Split-Path -Parent $Target)) -ne 'AddOns') {
    throw "refusing to deploy to unexpected target: $Target"
}
if ($Target.TrimEnd('\') -eq $AddonSrc.TrimEnd('\')) { throw 'target and source are the same folder' }
if ($Target.StartsWith($Module, [StringComparison]::OrdinalIgnoreCase)) { throw "target is inside the module repository: $Target" }

# Effect textures are shipped source assets, so validate the full set before planning any
# mirror changes. The regular addon mirror below then copies all ten as one complete set.
$EffectManifest = Join-Path $Module 'data\art\effect_textures.manifest.json'
$EffectNames = @('fire_core', 'soft_glow', 'spark', 'shockwave', 'neutral_core', 'neutral_glow', 'neutral_spark', 'neutral_ring', 'frost_shard', 'slash_arc', 'lightning_arc', 'smoke_puff', 'portal_ring', 'shield_shell')
if (-not (Test-Path -LiteralPath $EffectManifest -PathType Leaf)) { throw "effect texture manifest missing: $EffectManifest (run tools/art/effect_textures.py)" }
$effectEntries = (Get-Content -LiteralPath $EffectManifest -Raw | ConvertFrom-Json).entries
foreach ($name in $EffectNames) {
    $effectPath = Join-Path $AddonSrc "Textures\Effects\$name.blp"
    if (-not (Test-Path -LiteralPath $effectPath -PathType Leaf)) { throw "required effect texture missing: $effectPath (run tools/art/effect_textures.py)" }
    $recordProperty = $effectEntries.PSObject.Properties[$name]
    if (-not $recordProperty) { throw "effect texture $name is absent from $EffectManifest" }
    $record = $recordProperty.Value
    $expectedFile = "addon/Hearthstone/Textures/Effects/$name.blp"
    if ($record.file -ne $expectedFile) { throw "unexpected manifest path for $name in $EffectManifest" }
    $info = Get-Item -LiteralPath $effectPath
    if ($info.Length -ne [long]$record.bytes) { throw "effect texture size mismatch for $name; regenerate with tools/art/effect_textures.py" }
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::OpenRead($effectPath)
        try { $digest = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
        finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
    if ($digest -ne [string]$record.sha256) { throw "effect texture hash mismatch for $name; regenerate with tools/art/effect_textures.py" }
    $header = [IO.File]::ReadAllBytes($effectPath)
    if ($header.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($header, 0, 4) -ne 'BLP2') { throw "effect texture is not a BLP2 file: $effectPath" }
}

$apply = -not $DryRun
if ($apply -and -not $PSCmdlet.ShouldProcess($Target, "Mirror addon and copy card textures ($Cards) and board art ($Board)")) { $apply = $false }
if (-not $apply) { Write-Host 'DRY RUN: nothing will be changed.' }

# ---- select the cards
$entries = @{}
if ($Cards -ne 'None') {
    $m = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    foreach ($p in $m.entries.PSObject.Properties) { $entries[$p.Name] = $p.Value }
}
$ids = @()
if ($Cards -eq 'Prototype') {
    foreach ($id in $Prototype) {
        if (-not (Test-Path -LiteralPath (Join-Path $ArtDir "$id.blp"))) { Write-Warning "no texture art/out/Cards/$id.blp (run python tools/art/setup_art.py), skipped"; continue }
        if (-not $entries.ContainsKey($id)) { Write-Warning "$id is not in textures.manifest.json, skipped"; continue }
        $ids += $id
    }
} elseif ($Cards -eq 'All') {
    if (-not (Test-Path -LiteralPath $ArtDir -PathType Container)) { throw "no textures in $ArtDir (run python tools/art/setup_art.py first, or pass -Cards None)" }
    $names = @(Get-ChildItem -LiteralPath $ArtDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName })
    foreach ($id in $names) {
        if (-not $entries.ContainsKey($id)) { Write-Warning "$id.blp is not in textures.manifest.json, skipped"; continue }
        $ids += $id
    }
    [Array]::Sort($ids, [StringComparer]::Ordinal)
}

# ---- CardTextures.lua for the client copy
$inv = [Globalization.CultureInfo]::InvariantCulture
$sb = New-Object Text.StringBuilder
[void]$sb.Append("-- GENERATED by tools/deploy/deploy-addon.ps1 (-Cards $Cards) from data/art/textures.manifest.json.`n")
[void]$sb.Append("-- Do not edit. Lists the textures copied to Textures\Cards\ (browse order and bottom texcoord).`n")
[void]$sb.Append("-- Lua 5.0 (WoW 1.12).`n`n")
[void]$sb.Append("HS_CardTextures = {`n    generated = true,`n    mode = `"$Cards`",`n    count = $($ids.Count),`n    order = {`n")
for ($i = 0; $i -lt $ids.Count; $i += 6) {
    $chunk = $ids[$i..([Math]::Min($i + 5, $ids.Count - 1))] | ForEach-Object { "`"$_`"" }
    [void]$sb.Append('        ' + ($chunk -join ', ') + ",`n")
}
[void]$sb.Append("    },`n    bottom = {`n")
foreach ($id in $ids) {
    $tc = $entries[$id].texcoord
    if ($tc.left -ne 0 -or $tc.right -ne 1 -or $tc.top -ne 0) {
        Write-Warning "$id has unexpected texcoords (l=$($tc.left) r=$($tc.right) t=$($tc.top)); the addon only uses the bottom value"
    }
    [void]$sb.Append("        [`"$id`"] = " + ([double]$tc.bottom).ToString('R', $inv) + ",`n")
}
[void]$sb.Append("    },`n}`n")

# ---- oval minion portraits: the ids in art/out/Minions that the manifest knows (HS_MinionTextures, same file)
$minionIds = @()
if ($Minions -eq 'All' -and (Test-Path -LiteralPath $MinionDir -PathType Container)) {
    if (-not (Test-Path -LiteralPath $MinionManifest)) { throw "minion portraits present but $MinionManifest is missing (run tools/art/minion_portraits.py)" }
    $mm = (Get-Content -LiteralPath $MinionManifest -Raw | ConvertFrom-Json).entries
    $minionEntries = @{}
    foreach ($p in $mm.PSObject.Properties) { $minionEntries[$p.Name] = $p.Value }
    $minionIds = @(Get-ChildItem -LiteralPath $MinionDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object {
        if ($minionEntries.ContainsKey($_)) { $true } else { Write-Warning "$_.blp is not in minions.manifest.json, skipped"; $false } })
    [Array]::Sort($minionIds, [StringComparer]::Ordinal)
}
[void]$sb.Append("`n-- Oval minion portraits (Textures\Minions\<id>.blp, tools/art/minion_portraits.py): the ids that exist.`n")
[void]$sb.Append("HS_MinionTextures = {`n    generated = true,`n    count = $($minionIds.Count),`n    set = {`n")
for ($i = 0; $i -lt $minionIds.Count; $i += 4) {
    $chunk = $minionIds[$i..([Math]::Min($i + 3, $minionIds.Count - 1))] | ForEach-Object { "[`"$_`"] = true" }
    [void]$sb.Append('        ' + ($chunk -join ', ') + ",`n")
}
[void]$sb.Append("    },`n}`n")

# ---- round hero power portraits (HS_PowerTextures, same file)
$powerIds = @()
if ($Powers -eq 'All' -and (Test-Path -LiteralPath $PowerDir -PathType Container)) {
    if (-not (Test-Path -LiteralPath $PowerManifest)) { throw "power portraits present but $PowerManifest is missing (run tools/art/power_portraits.py)" }
    $pm = (Get-Content -LiteralPath $PowerManifest -Raw | ConvertFrom-Json).entries
    $powerEntries = @{}
    foreach ($p in $pm.PSObject.Properties) { $powerEntries[$p.Name] = $p.Value }
    $powerIds = @(Get-ChildItem -LiteralPath $PowerDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object {
        if ($powerEntries.ContainsKey($_)) { $true } else { Write-Warning "$_.blp is not in powers.manifest.json, skipped"; $false } })
    [Array]::Sort($powerIds, [StringComparer]::Ordinal)
}
[void]$sb.Append("`n-- Round hero power portraits (Textures\Powers\<id>.blp, tools/art/power_portraits.py): the ids that exist.`n")
[void]$sb.Append("HS_PowerTextures = {`n    generated = true,`n    count = $($powerIds.Count),`n    set = {`n")
for ($i = 0; $i -lt $powerIds.Count; $i += 4) {
    $chunk = $powerIds[$i..([Math]::Min($i + 3, $powerIds.Count - 1))] | ForEach-Object { "[`"$_`"] = true" }
    [void]$sb.Append('        ' + ($chunk -join ', ') + ",`n")
}
[void]$sb.Append("    },`n}`n")

# ---- alternative hero portraits (HS_HeroTextures, same file): id -> texcoord bottom of the padded render
$heroIds = @()
$heroEntries = @{}
if ($Heroes -eq 'All' -and (Test-Path -LiteralPath $HeroDir -PathType Container)) {
    if (-not (Test-Path -LiteralPath $HeroManifest)) { throw "hero portraits present but $HeroManifest is missing (run tools/art/hero_skins.py)" }
    $hm = (Get-Content -LiteralPath $HeroManifest -Raw | ConvertFrom-Json).entries
    foreach ($p in $hm.PSObject.Properties) { $heroEntries[$p.Name] = $p.Value }
    $heroIds = @(Get-ChildItem -LiteralPath $HeroDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object {
        if ($heroEntries.ContainsKey($_)) { $true } else { Write-Warning "$_.blp is not in hero_skins.manifest.json, skipped"; $false } })
    [Array]::Sort($heroIds, [StringComparer]::Ordinal)
}
[void]$sb.Append("`n-- Alternative hero portraits (Textures\Heroes\<id>.blp, tools/art/hero_skins.py): id -> texcoord bottom.`n")
[void]$sb.Append("HS_HeroTextures = {`n    generated = true,`n    count = $($heroIds.Count),`n    set = {`n")
foreach ($id in $heroIds) {
    [void]$sb.Append("        [`"$id`"] = " + ([double]$heroEntries[$id].texcoord.bottom).ToString('R', $inv) + ",`n")
}
[void]$sb.Append("    },`n}`n")
$luaBytes =(New-Object Text.UTF8Encoding($false)).GetBytes($sb.ToString())

# ---- Battlegrounds oval portraits: BgTextures.lua for the client copy (HS_BgTextures, global of BgTextures.lua)
$bgMinionIds = @()
$bgLua = $null
if ($BgMinions -eq 'All' -and (Test-Path -LiteralPath $BgMinionDir -PathType Container)) {
    if (-not (Test-Path -LiteralPath $BgMinionManifest)) { throw "Battlegrounds portraits present but $BgMinionManifest is missing (run tools/art/bg_minions.py)" }
    $bmm = (Get-Content -LiteralPath $BgMinionManifest -Raw | ConvertFrom-Json).entries
    $bgMinionEntries = @{}
    foreach ($p in $bmm.PSObject.Properties) { $bgMinionEntries[$p.Name] = $p.Value }
    $bgMinionIds = @(Get-ChildItem -LiteralPath $BgMinionDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object {
        if ($bgMinionEntries.ContainsKey($_)) { $true } else { Write-Warning "$_.blp is not in bg_minions.manifest.json, skipped"; $false } })
    [Array]::Sort($bgMinionIds, [StringComparer]::Ordinal)
}
# Bob's arch and the lobby's seat portraits (tools/art/bg_heroes.py): listed in HS_BgTextures.heroes
$bgHeroKeys = @()
if ($BgMinions -eq 'All' -and (Test-Path -LiteralPath $BgHeroDir -PathType Container) -and (Test-Path -LiteralPath $BgHeroManifest)) {
    $bhm = (Get-Content -LiteralPath $BgHeroManifest -Raw | ConvertFrom-Json).entries
    $bgHeroEntries = @{}
    foreach ($p in $bhm.PSObject.Properties) { $bgHeroEntries[$p.Name] = $p.Value }
    $bgHeroKeys = @(Get-ChildItem -LiteralPath $BgHeroDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object { $bgHeroEntries.ContainsKey($_) })
    [Array]::Sort($bgHeroKeys, [StringComparer]::Ordinal)
}
# The curated heroes' hero powers (tools/art/bg_heroes.py): listed in HS_BgTextures.powers
$bgPowerKeys = @()
if ($BgMinions -eq 'All' -and (Test-Path -LiteralPath $BgPowerDir -PathType Container) -and (Test-Path -LiteralPath $BgHeroManifest)) {
    $bpm = (Get-Content -LiteralPath $BgHeroManifest -Raw | ConvertFrom-Json).powers
    $bgPowerEntries = @{}
    foreach ($p in $bpm.PSObject.Properties) { $bgPowerEntries[$p.Name] = $p.Value }
    $bgPowerKeys = @(Get-ChildItem -LiteralPath $BgPowerDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object { $bgPowerEntries.ContainsKey($_) })
    [Array]::Sort($bgPowerKeys, [StringComparer]::Ordinal)
}
# The same powers as full cards (tools/art/bg_heroes.py `powerCards`): listed in HS_BgTextures.powerCards
$bgPowerCardKeys = @()
if ($BgMinions -eq 'All' -and (Test-Path -LiteralPath $BgPowerCardDir -PathType Container) -and (Test-Path -LiteralPath $BgHeroManifest)) {
    $bpc = (Get-Content -LiteralPath $BgHeroManifest -Raw | ConvertFrom-Json).powerCards
    $bgPowerCardEntries = @{}
    if ($bpc) { foreach ($p in $bpc.PSObject.Properties) { $bgPowerCardEntries[$p.Name] = $p.Value } }
    $bgPowerCardKeys = @(Get-ChildItem -LiteralPath $BgPowerCardDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object { $bgPowerCardEntries.ContainsKey($_) })
    [Array]::Sort($bgPowerCardKeys, [StringComparer]::Ordinal)
}
# The minions and their golden twins as full cards (tools/art/bg_minions.py `cards`): listed in HS_BgTextures.cards
$bgCardKeys = @()
if ($BgMinions -eq 'All' -and (Test-Path -LiteralPath $BgCardDir -PathType Container) -and (Test-Path -LiteralPath $BgMinionManifest)) {
    $bmc = (Get-Content -LiteralPath $BgMinionManifest -Raw | ConvertFrom-Json).cards
    $bgCardEntries = @{}
    if ($bmc) { foreach ($p in $bmc.PSObject.Properties) { $bgCardEntries[$p.Name] = $p.Value } }
    $bgCardKeys = @(Get-ChildItem -LiteralPath $BgCardDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object { $bgCardEntries.ContainsKey($_) })
    [Array]::Sort($bgCardKeys, [StringComparer]::Ordinal)
}
# The in-pool 24.2 quests and rewards (tools/art/bg_quests.py): listed in HS_BgTextures.quests / .rewards
$bgQuestKeys = @()
$bgRewardKeys = @()
$bgRewardCardKeys = @()
$bgRewardBakedKeys = @()
if ($BgMinions -eq 'All' -and (Test-Path -LiteralPath $BgQuestDir -PathType Container) -and (Test-Path -LiteralPath $BgQuestManifest)) {
    $bqm = (Get-Content -LiteralPath $BgQuestManifest -Raw | ConvertFrom-Json).entries
    $bgQuestEntries = @{}
    foreach ($p in $bqm.PSObject.Properties) { $bgQuestEntries[$p.Name] = $p.Value }
    $bgQuestInstalled = @(Get-ChildItem -LiteralPath $BgQuestDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object {
        if ($bgQuestEntries.ContainsKey($_)) { $true } else { Write-Warning "$_.blp is not in bg_quests.manifest.json, skipped"; $false } })
    $bgQuestKeys = @($bgQuestInstalled | Where-Object { $bgQuestEntries[$_].group -eq 'quest' })
    $bgRewardKeys = @($bgQuestInstalled | Where-Object { $bgQuestEntries[$_].group -eq 'reward' })
    # the real reward cards (bg_quests.py `cards`)
    $bqc = (Get-Content -LiteralPath $BgQuestManifest -Raw | ConvertFrom-Json).cards
    $bgRewardCardEntries = @{}
    if ($bqc) { foreach ($p in $bqc.PSObject.Properties) { $bgRewardCardEntries[$p.Name] = $p.Value } }
    if (Test-Path -LiteralPath $BgRewardCardDir -PathType Container) {
        $bgRewardCardKeys = @(Get-ChildItem -LiteralPath $BgRewardCardDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object { $bgRewardCardEntries.ContainsKey($_) })
        [Array]::Sort($bgRewardCardKeys, [StringComparer]::Ordinal)
        $bgRewardBakedKeys = @($bgRewardCardKeys | Where-Object { $bgRewardCardEntries[$_].baked -eq $true })
    }
    [Array]::Sort($bgQuestKeys, [StringComparer]::Ordinal)
    [Array]::Sort($bgRewardKeys, [StringComparer]::Ordinal)
}
if ($bgMinionIds.Count -gt 0 -or $bgHeroKeys.Count -gt 0 -or $bgPowerKeys.Count -gt 0 -or $bgPowerCardKeys.Count -gt 0 -or $bgCardKeys.Count -gt 0 -or $bgQuestKeys.Count -gt 0 -or $bgRewardKeys.Count -gt 0) {
    $bs = New-Object Text.StringBuilder
    [void]$bs.Append("-- GENERATED by tools/deploy/deploy-addon.ps1 (-BgMinions $BgMinions) from data/art/bg_minions.manifest.json,`n")
    [void]$bs.Append("-- data/art/bg_heroes.manifest.json and data/art/bg_quests.manifest.json.`n")
    [void]$bs.Append("-- Do not edit. Lists the oval portraits copied to Textures\BgMinions\ (HS_BgTextures.set); the addon's`n")
    [void]$bs.Append("-- Battlegrounds tiles show them, and fall back to the text tile for an id that is not listed. A golden minion`n")
    [void]$bs.Append("-- uses its normal twin's portrait; the gold ring is Textures\Bg\golden_ring.blp.`n")
    [void]$bs.Append("-- Lua 5.0 (WoW 1.12).`n`n")
    [void]$bs.Append("HS_BgTextures = {`n    generated = true,`n    count = $($bgMinionIds.Count),`n    set = {`n")
    for ($i = 0; $i -lt $bgMinionIds.Count; $i += 4) {
        $chunk = $bgMinionIds[$i..([Math]::Min($i + 3, $bgMinionIds.Count - 1))] | ForEach-Object { "[`"$_`"] = true" }
        [void]$bs.Append('        ' + ($chunk -join ', ') + ",`n")
    }
    [void]$bs.Append("    },`n    cards = {          -- Textures\BgMinions\Cards\<cardId> full cards (256x512, card in the top 388 rows) of the hover, golden twins too`n")
    for ($i = 0; $i -lt $bgCardKeys.Count; $i += 4) {
        $chunk = $bgCardKeys[$i..([Math]::Min($i + 3, $bgCardKeys.Count - 1))] | ForEach-Object { "[`"$_`"] = true" }
        [void]$bs.Append('        ' + ($chunk -join ', ') + ",`n")
    }
    [void]$bs.Append("    },`n    heroes = {")
    [void]$bs.Append((($bgHeroKeys | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$bs.Append("},          -- Textures\BgHeroes\<heroId> (bob, TB_BaconShop_HERO_..): Bob's arch and the lobby portraits`n")
    [void]$bs.Append("    powers = {")
    [void]$bs.Append((($bgPowerKeys | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$bs.Append("},          -- Textures\BgHeroes\Powers\<powerId> round portraits shown in the hero power button`n")
    [void]$bs.Append("    powerCards = {")
    [void]$bs.Append((($bgPowerCardKeys | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$bs.Append("},          -- Textures\BgHeroes\PowerCards\<powerId> full hero power cards (256x512, card in the top 388 rows) of the hero pick's hover`n")
    [void]$bs.Append("    quests = {")
    [void]$bs.Append((($bgQuestKeys | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$bs.Append("},          -- Textures\BgQuests\<questId> round portraits of the in-pool 24.2 quests`n")
    [void]$bs.Append("    rewards = {")
    [void]$bs.Append((($bgRewardKeys | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$bs.Append("},          -- Textures\BgQuests\<rewardId> round portraits of the in-pool 24.2 rewards`n")
    [void]$bs.Append("    rewardBaked = {")
    [void]$bs.Append((($bgRewardBakedKeys | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$bs.Append("},          -- the rewardCards baked from the real frame (art but no words): the quest pick writes their name and text`n")
    [void]$bs.Append("    rewardCards = {")
    [void]$bs.Append((($bgRewardCardKeys | ForEach-Object { "`"$_`"" }) -join ', '))
    [void]$bs.Append("},          -- Textures\BgQuests\Cards\<rewardId> full reward cards (256x512, card in the top 388 rows) of the quest pick`n}`n")
    $bgLua = (New-Object Text.UTF8Encoding($false)).GetBytes($bs.ToString())
}

# ---- board art pieces and BoardTextures.lua for the client copy
$boardNames = @()
$boardLua = $null
if ($Board -eq 'All' -and (Test-Path -LiteralPath $BoardDir -PathType Container)) {
    if (-not (Test-Path -LiteralPath $BoardManifest)) { throw "board art present but $BoardManifest is missing (run tools/art/slice_board.py)" }
    $bm = (Get-Content -LiteralPath $BoardManifest -Raw | ConvertFrom-Json).pieces
    $boardEntries = @{}
    foreach ($p in $bm.PSObject.Properties) { $boardEntries[$p.Name] = $p.Value }
    $boardNames = @(Get-ChildItem -LiteralPath $BoardDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object {
        if ($boardEntries.ContainsKey($_)) { $true } else { Write-Warning "$_.blp is not in board_textures.manifest.json, skipped"; $false } })
    [Array]::Sort($boardNames, [StringComparer]::Ordinal)
    if ($boardNames.Count -gt 0) {
        $bsb = New-Object Text.StringBuilder
        [void]$bsb.Append("-- GENERATED by tools/deploy/deploy-addon.ps1 from data/art/board_textures.manifest.json.`n")
        [void]$bsb.Append("-- Do not edit. Lists the pieces copied to Textures\Board\: path, w/h (used pixels), right/bottom`n")
        [void]$bsb.Append("-- (SetTexCoord(0, right, 0, bottom)), anchors as fractions of the used image, aspect for stretched pieces.`n")
        [void]$bsb.Append("-- Lua 5.0 (WoW 1.12).`n`n")
        [void]$bsb.Append("HS_BoardTextures = {`n    generated = true,`n    pieces = {`n")
        foreach ($n in $boardNames) {
            $e = $boardEntries[$n]
            $line = "        $n = { path = `"Interface\\Hearthstone\\Textures\\Board\\$n`", w = $($e.w), h = $($e.h), right = " +
                ([double]$e.right).ToString('R', $inv) + ', bottom = ' + ([double]$e.bottom).ToString('R', $inv)
            if ($e.aspect) { $line += ', aspect = ' + ([double]$e.aspect).ToString('R', $inv) }
            if ($e.anchors) {
                $parts = @()
                foreach ($a in ($e.anchors.PSObject.Properties | Sort-Object Name)) {
                    $v = $a.Value
                    $parts += "$($a.Name) = { left = " + ([double]$v.left).ToString('R', $inv) + ', top = ' + ([double]$v.top).ToString('R', $inv) +
                        ', right = ' + ([double]$v.right).ToString('R', $inv) + ', bottom = ' + ([double]$v.bottom).ToString('R', $inv) + ' }'
                }
                $line += ', anchors = { ' + ($parts -join ', ') + ' }'
            }
            [void]$bsb.Append($line + " },`n")
        }
        [void]$bsb.Append("    },`n}`n")
        $boardLua = (New-Object Text.UTF8Encoding($false)).GetBytes($bsb.ToString())
    }
}

# ---- collection art pieces and CollectionTextures.lua for the client copy
$collNames = @()
$collLua = $null
if ($Collection -eq 'All' -and (Test-Path -LiteralPath $CollectionDir -PathType Container)) {
    if (-not (Test-Path -LiteralPath $CollectionManifest)) { throw "collection art present but $CollectionManifest is missing (run tools/art/slice_collection.py)" }
    $cm = (Get-Content -LiteralPath $CollectionManifest -Raw | ConvertFrom-Json).pieces
    $collEntries = @{}
    foreach ($p in $cm.PSObject.Properties) { $collEntries[$p.Name] = $p.Value }
    $collNames = @(Get-ChildItem -LiteralPath $CollectionDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName } | Where-Object {
        if ($collEntries.ContainsKey($_)) { $true } else { Write-Warning "$_.blp is not in collection_textures.manifest.json, skipped"; $false } })
    [Array]::Sort($collNames, [StringComparer]::Ordinal)
    # the 1.12 client does not load a texture side above 512 (it draws solid green): never deploy one
    foreach ($n in $collNames) {
        $ts = $collEntries[$n].texture_size
        if ($ts[0] -gt 512 -or $ts[1] -gt 512) { throw "$n.blp is $($ts[0])x$($ts[1]): the client does not load textures above 512 pixels (run tools/art/slice_collection.py again; old art/out/Collection files are removed by a full run)" }
    }
    if ($collNames.Count -gt 0) {
        $csb = New-Object Text.StringBuilder
        [void]$csb.Append("-- GENERATED by tools/deploy/deploy-addon.ps1 from data/art/collection_textures.manifest.json.`n")
        [void]$csb.Append("-- Do not edit. Lists the pieces copied to Textures\Collection\: path, w/h (used pixels), right/bottom`n")
        [void]$csb.Append("-- (SetTexCoord(0, right, 0, bottom)).`n")
        [void]$csb.Append("-- Lua 5.0 (WoW 1.12).`n`n")
        [void]$csb.Append("HS_CollectionTextures = {`n    generated = true,`n    pieces = {`n")
        foreach ($n in $collNames) {
            $e = $collEntries[$n]
            $line = "        $n = { path = `"Interface\\Hearthstone\\Textures\\Collection\\$n`", w = $($e.w), h = $($e.h), right = " +
                ([double]$e.right).ToString('R', $inv) + ', bottom = ' + ([double]$e.bottom).ToString('R', $inv)
            [void]$csb.Append($line + " },`n")
        }
        [void]$csb.Append("    },`n}`n")
        $collLua = (New-Object Text.UTF8Encoding($false)).GetBytes($csb.ToString())
    }
}

# ---- desired file set: relative path -> @{ Src = path or $null; Bytes = bytes or $null }
$want = @{}
Get-ChildItem -LiteralPath $AddonSrc -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($AddonSrc.Length).TrimStart('\')
    if ($rel -ieq 'CardTextures.lua') { return }          # replaced by the generated version
    if ($rel -ieq 'Battlegrounds\BgTextures.lua' -and $bgLua) { return }   # replaced by the generated version
    if ($rel -ieq 'BoardTextures.lua' -and $boardLua) { return }   # replaced by the generated version
    if ($rel -ieq 'CollectionTextures.lua' -and $collLua) { return }   # replaced by the generated version
    $want[$rel] = @{ Src = $_.FullName; Bytes = $null; Length = $_.Length }
}
$want['CardTextures.lua'] = @{ Src = $null; Bytes = $luaBytes; Length = $luaBytes.Length }
if ($bgLua) { $want['Battlegrounds\BgTextures.lua'] = @{ Src = $null; Bytes = $bgLua; Length = $bgLua.Length } }
if ($boardLua) { $want['BoardTextures.lua'] = @{ Src = $null; Bytes = $boardLua; Length = $boardLua.Length } }
if ($collLua) { $want['CollectionTextures.lua'] = @{ Src = $null; Bytes = $collLua; Length = $collLua.Length } }
foreach ($n in $collNames) {
    $src = Join-Path $CollectionDir "$n.blp"
    $want["Textures\Collection\$n.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}
$uiNames = @()
if (Test-Path -LiteralPath $UiDir -PathType Container) {
    $uiNames = @(Get-ChildItem -LiteralPath $UiDir -Filter '*.blp' -File | ForEach-Object { $_.BaseName })
}
foreach ($n in $uiNames) {
    $src = Join-Path $UiDir "$n.blp"
    $want["Textures\UI\$n.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}
foreach ($n in $boardNames) {
    $src = Join-Path $BoardDir "$n.blp"
    $want["Textures\Board\$n.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}
foreach ($id in $ids) {
    $src = Join-Path $ArtDir "$id.blp"
    $want["Textures\Cards\$id.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}
foreach ($id in $powerIds) {
    $src = Join-Path $PowerDir "$id.blp"
    $want["Textures\Powers\$id.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}
foreach ($id in $heroIds) {
    $src = Join-Path $HeroDir "$id.blp"
    $want["Textures\Heroes\$id.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}
foreach ($id in $minionIds) {
    $src = Join-Path $MinionDir "$id.blp"
    $want["Textures\Minions\$id.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}
foreach ($id in $bgMinionIds) {
    $src = Join-Path $BgMinionDir "$id.blp"
    $want["Textures\BgMinions\$id.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}

foreach ($k in $bgHeroKeys) {
    $src = Join-Path $BgHeroDir "$k.blp"
    $want["Textures\BgHeroes\$k.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}

foreach ($k in $bgPowerKeys) {
    $src = Join-Path $BgPowerDir "$k.blp"
    $want["Textures\BgHeroes\Powers\$k.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}

foreach ($k in $bgPowerCardKeys) {
    $src = Join-Path $BgPowerCardDir "$k.blp"
    $want["Textures\BgHeroes\PowerCards\$k.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}

foreach ($k in $bgCardKeys) {
    $src = Join-Path $BgCardDir "$k.blp"
    $want["Textures\BgMinions\Cards\$k.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}

foreach ($k in @($bgQuestKeys + $bgRewardKeys)) {
    $src = Join-Path $BgQuestDir "$k.blp"
    $want["Textures\BgQuests\$k.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}

foreach ($k in $bgRewardCardKeys) {
    $src = Join-Path $BgRewardCardDir "$k.blp"
    $want["Textures\BgQuests\Cards\$k.blp"] = @{ Src = $src; Bytes = $null; Length = (Get-Item -LiteralPath $src).Length }
}

function Get-Sha([string]$path, [byte[]]$bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        if ($bytes) { return [BitConverter]::ToString($sha.ComputeHash($bytes)) }
        $fs = [IO.File]::OpenRead($path)
        try { return [BitConverter]::ToString($sha.ComputeHash($fs)) } finally { $fs.Dispose() }
    } finally { $sha.Dispose() }
}

# ---- plan
$have = @{}
$legacy = @()   # textures an older deploy left inside the addon folder
if (Test-Path -LiteralPath $Target -PathType Container) {
    Get-ChildItem -LiteralPath $Target -Recurse -File | ForEach-Object {
        $r = $_.FullName.Substring($Target.Length).TrimStart('\')
        if ($r -like 'Textures\*') { $legacy += $r } else { $have[$r] = $_.FullName }
    }
}
if (Test-Path -LiteralPath (Join-Path $TexRoot 'Textures') -PathType Container) {
    Get-ChildItem -LiteralPath (Join-Path $TexRoot 'Textures') -Recurse -File | ForEach-Object {
        $have[$_.FullName.Substring($TexRoot.Length).TrimStart('\')] = $_.FullName
    }
}
$toCopy = @(); $unchanged = 0; $toRemove = @()
foreach ($rel in ($want.Keys | Sort-Object)) {
    $w = $want[$rel]
    $dst = Get-Dest $rel
    if ($have.ContainsKey($rel)) {
        $same = ((Get-Item -LiteralPath $dst).Length -eq $w.Length) -and ((Get-Sha $dst $null) -eq (Get-Sha $w.Src $w.Bytes))
        if ($same) { $unchanged++; continue }
    }
    $toCopy += $rel
}
foreach ($rel in ($have.Keys | Sort-Object)) { if (-not $want.ContainsKey($rel)) { $toRemove += $rel } }
foreach ($rel in $legacy) { $toRemove += "AddOns\Hearthstone\$rel" }

# ---- apply
$prefix = if ($apply) { '' } else { '[dry run] would ' }
Write-Host "Source : $AddonSrc"
Write-Host "Target : $Target"
Write-Host "Textures: $TexRoot\Textures"
Write-Host "Cards  : $Cards ($($ids.Count) textures)"
Write-Host "Board  : $Board ($($boardNames.Count) pieces)"
Write-Host "Minions: $Minions ($($minionIds.Count) oval portraits)"
Write-Host "Bg minions: $BgMinions ($($bgMinionIds.Count) Battlegrounds oval portraits, $($bgHeroKeys.Count) hero portraits, $($bgPowerKeys.Count) hero power portraits, $($bgCardKeys.Count) minion cards, $($bgQuestKeys.Count + $bgRewardKeys.Count) quest/reward portraits)"
Write-Host "Powers : $Powers ($($powerIds.Count) round portraits)"
Write-Host "Heroes : $Heroes ($($heroIds.Count) alternative portraits)"
Write-Host "Collection: $Collection ($($collNames.Count) pieces)"
Write-Host "UI icons: $($uiNames.Count)"
if ($apply) {
    foreach ($rel in $toRemove) {
        $victim = if ($rel -like 'AddOns\Hearthstone\*') { Join-Path $Client "Interface\$rel" } else { Get-Dest $rel }
        Remove-Item -LiteralPath $victim -Force
    }
    foreach ($rel in $toCopy) {
        $w = $want[$rel]
        $dst = Get-Dest $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        if ($w.Src) { Copy-Item -LiteralPath $w.Src -Destination $dst -Force }
        else { [IO.File]::WriteAllBytes($dst, $w.Bytes) }
    }
    # drop folders that became empty (deepest first), never the addon folder itself
    foreach ($root in @($Target, $TexRoot)) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
                if (-not (Get-ChildItem -LiteralPath $_.FullName -Force)) { Remove-Item -LiteralPath $_.FullName -Force }
            }
        }
    }
}
$detail = 30
foreach ($rel in $toRemove) { Write-Host "  ${prefix}remove   $rel" }
if ($toCopy.Count -le $detail) { foreach ($rel in $toCopy) { Write-Host "  ${prefix}copy     $rel" } }
else { Write-Host "  ${prefix}copy     $($toCopy.Count) files (list omitted, first: $($toCopy[0]))" }
Write-Host ("{0}: {1} copied/updated, {2} unchanged, {3} stale removed; CardTextures.lua lists {4} cards and {6} minion portraits, BoardTextures.lua {5} pieces, CollectionTextures.lua {7} pieces." -f `
    $(if ($apply) { 'Done' } else { 'Plan' }), $toCopy.Count, $unchanged, $toRemove.Count, $ids.Count, $boardNames.Count, $minionIds.Count, $collNames.Count)
if ($apply) {
    Write-Host 'Restart the WoW client (new texture files are not picked up while it runs), then click the Hearthstone button in the micro menu or type:  /hs collection'
}
