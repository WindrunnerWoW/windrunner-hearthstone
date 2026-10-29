<#
 Builds and runs the standalone protocol, collection, deck and match unit tests (no CMake).
 The Hunter projection motion regression imports local core source with world/AI adapters;
 it does not start a server or access the database.
   - C++ : cl.exe (VS 2022 x64 dev environment) -> 02_build/hearthstone-check/tests/protocol_tests.exe
           and match_tests.exe (the engine and AI)
           bg_heroes_tests (hero powers) is part of the default run.
   - Lua : tests/lua_tests.lua via a Lua interpreter if one is found (LUA env var, PATH, or the
           lua52.exe of the companion-recruiter checks). Skipped with a note otherwise.
 Usage: powershell -File tests/build_tests.ps1 [-Out <dir>] [-Only <name>,...]
   -Out  : build directory (default 02_build/hearthstone-check/tests); give each parallel checkout its own.
   -Only : build and run only these programs (protocol_tests, hunter_projection_motion, match_tests, match_core_tests, ..., arena_tests, bg_combat_tests, bg_shop_tests, bg_effects_tests, bg_wire_tests, bg_weekly_tests, ladder_sim_tests, ladder_tests, match_encounter_tests, match_encounter_cards_tests, match_replay_tests, lua).
           arena_sim (draft AI simulation) runs only when named here.
#>
param(
    [string]$Out = 'C:\TurtleDev\02_build\hearthstone-check\tests',
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Module = Split-Path -Parent $Here
$ServerGame = Join-Path (Split-Path -Parent (Split-Path -Parent $Module)) 'src\game'
# powershell -File passes '-Only a,b' as one string: split it.
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
function Want([string]$name) { return ($Only.Count -eq 0) -or ($Only -contains $name) }
$cppExit = 0
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# Import the VS 2022 x64 build environment.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio 2022 with C++ tools not found' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

$src = @(
    "$Here\protocol_tests.cpp",
    "$Module\src\protocol\HsProtocol.cpp",
    "$Module\src\protocol\HsReassembler.cpp",
    "$Module\src\protocol\HsRateLimiter.cpp",
    "$Module\src\protocol\HsSession.cpp",
    "$Module\src\arena\HsArenaWire.cpp",
    "$Module\src\collection\HsOwnership.cpp",
    "$Module\src\collection\HsPacks.cpp",
    "$Module\src\collection\HsCollectionService.cpp",
    "$Module\src\collection\HsWallet.cpp",
    "$Module\src\decks\HsDecks.cpp",
    "$Module\src\decks\HsDeckService.cpp",
    "$Module\src\heroes\HsHeroPortraits.cpp"
)
# Match engine (pure C++; phases 07-08), shared by every test program.
$matchSrc = @(
    "$Module\src\match\HsMatch.cpp",
    "$Module\src\match\HsMatchCards.cpp",
    "$Module\src\match\HsMatchCardsBasic.cpp",
    "$Module\src\match\HsMatchCardsClassic.cpp",
    "$Module\src\match\HsMatchCardsClassicRest.cpp",
    "$Module\src\match\HsMatchCardsClassicEngine.cpp",
    "$Module\src\match\HsMatchCardsNaxx.cpp",
    "$Module\src\match\HsMatchAi.cpp",
    "$Module\src\match\HsMatchAiSearch.cpp",
    "$Module\src\match\HsMatchReplay.cpp"
)
# The match wire codec, the service and the opponent library / registry are tested through
# protocol_tests (which also reads data/sql/world/*.sql next to the tests directory).
$serviceSrc = @(
    "$Module\src\match\HsMatchWire.cpp",
    "$Module\src\match\HsMatchService.cpp",
    "$Module\src\match\HsMatchHistory.cpp",
    "$Module\src\match\HsEncounters.cpp",
    "$Module\src\match\HsOpponents.cpp"
)
if (Want 'protocol_tests') {
    $exe = "$Out\protocol_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\p" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\p\" "/Fe$exe" @src @matchSrc @serviceSrc
    if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }

    # The committed golden files must equal what the C++ code emits right now.
    $goldenOk = $true
    foreach ($pair in @(@('--emit-vectors', 'golden_vectors.txt'), @('--emit-transcript', 'transcript.txt'))) {
        $fresh = Join-Path $Out $pair[1]
        cmd /c "`"$exe`" $($pair[0]) > `"$fresh`""
        if ((Get-FileHash $fresh).Hash -ne (Get-FileHash (Join-Path $Here $pair[1])).Hash) {
            Write-Host "STALE: tests/$($pair[1]) differs from '$($pair[0])' output. Regenerate: copy $fresh over it."
            $goldenOk = $false
        }
    }

    & $exe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
    if (-not $goldenOk) { $cppExit = 1 }
}

if (Want 'hunter_projection_motion') {
    Write-Host "`n--- Hunter projection native motion regression ---"
    & python "$Here\test_hunter_projection_motion.py" --out "$Out\hunter_projection_motion"
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

if (Want 'match_tests') {
    $matchExe = "$Out\match_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\m" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\m\" "/Fe$matchExe" "$Here\match_tests.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "match_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- match engine tests ---"
    & $matchExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Draft arena rules and run service (pure C++; the card dataset headers of tests/).
if (Want 'arena_tests') {
    $aExe = "$Out\arena_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\arena_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\arena_tests\" "/Fe$aExe" "$Here\arena_tests.cpp" `
        "$Module\src\arena\HsArenaRules.cpp" "$Module\src\arena\HsArenaService.cpp" "$Module\src\arena\HsArenaDraftAi.cpp" `
        "$Module\src\arena\HsArenaRows.cpp" "$Module\src\arena\HsArenaEntry.cpp" "$Module\src\arena\HsArenaPayout.cpp" `
        "$Module\src\collection\HsOwnership.cpp" "$Module\src\collection\HsPacks.cpp" "$Module\src\decks\HsDecks.cpp" `
        "$Module\src\collection\HsCollectionService.cpp" "$Module\src\collection\HsWallet.cpp" `
        "$Module\src\protocol\HsProtocol.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "arena_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- arena_tests ---"
    & $aExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Battlegrounds core (src/battlegrounds: card table, types, effect registry, combat simulator). Pure C++, no
# match engine or card dataset header needed.
if (Want 'bg_combat_tests') {
    $bExe = "$Out\bg_combat_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_combat_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_combat_tests\" "/Fe$bExe" "$Here\bg_combat_tests.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp"
    if ($LASTEXITCODE -ne 0) { throw "bg_combat_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_combat_tests ---"
    & $bExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Battlegrounds minion effects: one source file per batch (Beast, Mech, Murloc, Demon, Dragon, Elemental,
# Pirate, Naga, Quilboar, Neutral, Heroes), with one tests/bg_effects_<tribe>_tests.inc file each (Heroes has its tokens
# covered by the hero tests).
$bgBatches = @(
    "$Module\src\battlegrounds\HsBgEffectsBeast.cpp", "$Module\src\battlegrounds\HsBgEffectsMech.cpp",
    "$Module\src\battlegrounds\HsBgEffectsMurloc.cpp", "$Module\src\battlegrounds\HsBgEffectsDemon.cpp",
    "$Module\src\battlegrounds\HsBgEffectsDragon.cpp", "$Module\src\battlegrounds\HsBgEffectsElemental.cpp",
    "$Module\src\battlegrounds\HsBgEffectsPirate.cpp",
    "$Module\src\battlegrounds\HsBgEffectsNaga.cpp",
    "$Module\src\battlegrounds\HsBgEffectsQuilboar.cpp",
    "$Module\src\battlegrounds\HsBgEffectsNeutral.cpp",
    "$Module\src\battlegrounds\HsBgEffectsHeroes.cpp"
)

# Battlegrounds shop, pool and game loop (src/battlegrounds: HsBgPool, HsBgShop, HsBgGame on top of the core).
if (Want 'bg_shop_tests') {
    $sExe = "$Out\bg_shop_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_shop_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_shop_tests\" "/Fe$sExe" "$Here\bg_shop_tests.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" "$Module\src\battlegrounds\HsBgRewardsShop.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_shop_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_shop_tests ---"
    & $sExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Battlegrounds session and WRBG wire format (HsBgSession, HsBgWire, the real NPC policy and the WRHS codec).
if (Want 'bg_wire_tests') {
    $wExe = "$Out\bg_wire_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_wire_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_wire_tests\" "/Fe$wExe" "$Here\bg_wire_tests.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgAi.cpp" "$Module\src\battlegrounds\HsBgSession.cpp" "$Module\src\battlegrounds\HsBgWire.cpp" `
        "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgNames.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" "$Module\src\battlegrounds\HsBgRewardsShop.cpp" "$Module\src\protocol\HsProtocol.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_wire_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_wire_tests ---"
    & $wExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# NPC policy (HsBgAi) and the simulation of thousands of games. Runs only when named (-Only bg_sim);
# the game count is BG_SIM_GAMES (default 2000), the hero-pick games BG_SIM_HERO_GAMES (default 400), and the report
# goes to $Out\bg_sim_report.md.
if ($Only -contains 'bg_sim') {
    $mExe = "$Out\bg_sim.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_sim" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_sim\" "/Fe$mExe" "$Here\bg_sim.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgAi.cpp" "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" "$Module\src\battlegrounds\HsBgRewardsShop.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_sim compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_sim ---"
    $simGames = if ($env:BG_SIM_GAMES) { $env:BG_SIM_GAMES } else { '2000' }
    $heroGames = if ($env:BG_SIM_HERO_GAMES) { $env:BG_SIM_HERO_GAMES } else { '400' }
    & $mExe $simGames "$Out\bg_sim_report.md" $heroGames
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Policy A/B (shipped policy against a tuned one, sides swapped). Runs only when named (-Only bg_ab); the games and the
# settings come from BG_AB_GAMES and BG_AB_ARGS (for example "tribeBonus=10 lockRound=2").
if ($Only -contains 'bg_ab') {
    $aExe = "$Out\bg_ab.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_ab" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_ab\" "/Fe$aExe" "$Here\bg_ab.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgAi.cpp" "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" "$Module\src\battlegrounds\HsBgRewardsShop.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_ab compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_ab ---"
    $abGames = if ($env:BG_AB_GAMES) { $env:BG_AB_GAMES } else { '2000' }
    $abArgs = if ($env:BG_AB_ARGS) { @($env:BG_AB_ARGS -split ' ') } else { @() }
    & $aExe $abGames @abArgs
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Tests of the real minion effects (the six batch files) and of the core API they use. The six
# tests/bg_effects_*_tests.inc files are included by bg_effects_tests.cpp, so they are not compiled on their own.
if (Want 'bg_effects_tests') {
    $eExe = "$Out\bg_effects_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_effects_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_effects_tests\" "/Fe$eExe" "$Here\bg_effects_tests.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" "$Module\src\battlegrounds\HsBgRewardsShop.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_effects_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_effects_tests ---"
    & $eExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# The hero table, the armor tiers and the 18 hero powers (one test per hero + the registry check).
if (Want 'bg_heroes_tests') {
    $hExe = "$Out\bg_heroes_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_heroes_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_heroes_tests\" "/Fe$hExe" "$Here\bg_heroes_tests.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" "$Module\src\battlegrounds\HsBgRewardsShop.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_heroes_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_heroes_tests ---"
    & $hExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# The quest registry, the counting rules (one test per quest) and the turn-4 offer (HsBgQuests).
if (Want 'bg_quests_tests') {
    $qExe = "$Out\bg_quests_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_quests_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_quests_tests\" "/Fe$qExe" "$Here\bg_quests_tests.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" "$Module\src\battlegrounds\HsBgRewardsShop.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_quests_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_quests_tests ---"
    & $qExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# The 23 rewards (one test per reward) and the Wheel of Yogg-Saron (HsBgRewards).
if (Want 'bg_rewards_tests') {
    $rExe = "$Out\bg_rewards_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg_rewards_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\bg_rewards_tests\" "/Fe$rExe" "$Here\bg_rewards_tests.cpp" `
        "$Module\src\battlegrounds\HsBgTypes.cpp" "$Module\src\battlegrounds\HsBgEffects.cpp" "$Module\src\battlegrounds\HsBgCombat.cpp" `
        "$Module\src\battlegrounds\HsBgPool.cpp" "$Module\src\battlegrounds\HsBgShop.cpp" "$Module\src\battlegrounds\HsBgGame.cpp" `
        "$Module\src\battlegrounds\HsBgHeroes.cpp" "$Module\src\battlegrounds\HsBgQuests.cpp" "$Module\src\battlegrounds\HsBgRewards.cpp" `
        "$Module\src\battlegrounds\HsBgRewardsShop.cpp" @bgBatches
    if ($LASTEXITCODE -ne 0) { throw "bg_rewards_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- bg_rewards_tests ---"
    & $rExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Headless AI-versus-AI runner (src/ladder/HsLadderSim.*) with a short benchmark over the library decks of
# world migration 0002. A longer timed run: ladder_sim_tests.exe <tests dir> --bench <games> [threads].
if (Want 'ladder_sim_tests') {
    $lExe = "$Out\ladder_sim_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\ladder_sim_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\ladder_sim_tests\" "/Fe$lExe" "$Here\ladder_sim_tests.cpp" `
        "$Module\src\ladder\HsLadderSim.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "ladder_sim_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- ladder_sim_tests ---"
    & $lExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Ladder rules (src/ladder/HsLadderRules.*): stars, hidden rating, roster, ladder start, NPC rounds, player
# result. Mostly a fake fast game function; a few real games with the library decks and finder names of world SQL.
# The chair offers (HsLadderOffers.*), their rows and the world data of the chairs (HsLadderWorld.*, world SQL 0049).
if (Want 'ladder_tests') {
    $ltExe = "$Out\ladder_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\ladder_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\ladder_tests\" "/Fe$ltExe" "$Here\ladder_tests.cpp" `
        "$Module\src\ladder\HsLadderRules.cpp" "$Module\src\ladder\HsLadderRows.cpp" "$Module\src\ladder\HsLadderSim.cpp" `
        "$Module\src\ladder\HsLadderOffers.cpp" "$Module\src\ladder\HsLadderWorld.cpp" "$Module\src\ladder\HsLadderPlay.cpp" "$Module\src\ladder\HsLadderDuel.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "ladder_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- ladder_tests ---"
    & $ltExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Draft AI simulation: AI-drafted decks play Hard vs Hard. Slow, so only with an explicit
# "-Only arena_sim" (never part of the default run). Games: env HS_ARENA_SIM_GAMES (default 200).
if ($Only -contains 'arena_sim') {
    $sExe = "$Out\arena_sim.exe"
    New-Item -ItemType Directory -Force -Path "$Out\arena_sim" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\arena_sim\" "/Fe$sExe" "$Here\arena_sim.cpp" `
        "$Module\src\arena\HsArenaRules.cpp" "$Module\src\arena\HsArenaDraftAi.cpp" `
        "$Module\src\collection\HsOwnership.cpp" "$Module\src\collection\HsPacks.cpp" "$Module\src\decks\HsDecks.cpp" `
        "$Module\src\protocol\HsProtocol.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "arena_sim compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- arena_sim ---"
    $games = if ($env:HS_ARENA_SIM_GAMES) { $env:HS_ARENA_SIM_GAMES } else { '200' }
    & $sExe $Here $games
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Engine test programs (each has its own main), built only when present.
foreach ($t in @('match_core_tests', 'match_cards_basic_tests', 'match_cards_classic_tests', 'match_cards_rest_tests',
                 'match_cards_engine_tests', 'match_cards_naxx_tests', 'match_ai_tests')) {
    if (-not (Test-Path "$Here\$t.cpp")) { continue }
    if (-not (Want $t)) { continue }
    $tExe = "$Out\$t.exe"
    New-Item -ItemType Directory -Force -Path "$Out\$t" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\$t\" "/Fe$tExe" "$Here\$t.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "$t compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- $t ---"
    & $tExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Boss encounters (src/match/HsEncounters.*): the data tables and the engine's start state. Needs only the
# encounter source next to the engine (HsMatchService.h is read for MatchStartParams, nothing of it is linked).
if ((Test-Path "$Here\match_encounter_tests.cpp") -and (Want 'match_encounter_tests')) {
    $eExe = "$Out\match_encounter_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\match_encounter_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\match_encounter_tests\" "/Fe$eExe" "$Here\match_encounter_tests.cpp" "$Module\src\match\HsEncounters.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "match_encounter_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- match_encounter_tests ---"
    & $eExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Encounter-only cards and boss hero powers and the full encounter play-outs (Hard AI vs Hard AI).
if ((Test-Path "$Here\match_encounter_cards_tests.cpp") -and (Want 'match_encounter_cards_tests')) {
    $ecExe = "$Out\match_encounter_cards_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\match_encounter_cards_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\match_encounter_cards_tests\" "/Fe$ecExe" "$Here\match_encounter_cards_tests.cpp" "$Module\src\match\HsEncounters.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "match_encounter_cards_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- match_encounter_cards_tests ---"
    & $ecExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Durable duels: replay equals uninterrupted play (needs the match service, so it links the service sources).
if ((Test-Path "$Here\match_replay_tests.cpp") -and (Want 'match_replay_tests')) {
    $rExe = "$Out\match_replay_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\match_replay_tests" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\match_replay_tests\" "/Fe$rExe" "$Here\match_replay_tests.cpp" @matchSrc @serviceSrc `
        "$Module\src\decks\HsDecks.cpp" "$Module\src\collection\HsOwnership.cpp" "$Module\src\protocol\HsProtocol.cpp"
    if ($LASTEXITCODE -ne 0) { throw "match_replay_tests compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- match_replay_tests ---"
    & $rExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

# Compatibility report: the committed data/cards/compat_report.json must equal the fresh report, and
# no standard card may be incomplete.
if (Want 'compat_report') {
    $cExe = "$Out\compat_report.exe"
    New-Item -ItemType Directory -Force -Path "$Out\compat_report" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/Fo$Out\compat_report\" "/Fe$cExe" "$Here\compat_report.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "compat_report compile failed ($LASTEXITCODE)" }
    Write-Host "`n--- compat_report ---"
    $fresh = Join-Path $Out 'compat_report.json'
    cmd /c "`"$cExe`" `"$Here`" --emit > `"$fresh`""
    $committed = Join-Path $Module 'data\cards\compat_report.json'
    # Compared without line endings: the program writes CRLF on Windows, a checkout may hold LF.
    $norm = { param($p) (Get-Content -Raw $p) -replace "`r`n", "`n" }
    if (-not (Test-Path $committed) -or (& $norm $fresh) -cne (& $norm $committed)) {
        Write-Host "STALE: data/cards/compat_report.json differs from the fresh report. Regenerate: copy $fresh over it."
        $cppExit = 1
    }
    & $cExe $Here
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

if (Want 'weekly_quest_credit') {
    & python "$Here\test_weekly_quest_credit.py" --out "$Out\weekly-credit"
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

if (Want 'weekly_tally_tests') {
    $wExe = "$Out\weekly_tally_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\weekly-tally" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/I$ServerGame" "/Fo$Out\weekly-tally\" "/Fe$wExe" `
        "$Here\weekly_tally_tests.cpp" "$Module\src\quests\HsWeeklyQuests.cpp" @matchSrc
    if ($LASTEXITCODE -ne 0) { throw "weekly_tally_tests compile failed ($LASTEXITCODE)" }
    & $wExe
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

if (Want 'bg_weekly_tests') {
    $wExe = "$Out\bg_weekly_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\bg-weekly" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/I$ServerGame" "/Fo$Out\bg-weekly\" "/Fe$wExe" `
        "$Here\bg_weekly_tests.cpp" "$Module\src\quests\HsBgWeeklyQuests.cpp"
    if ($LASTEXITCODE -ne 0) { throw "bg_weekly_tests compile failed ($LASTEXITCODE)" }
    & $wExe
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

if (Want 'weekly_quest_tests') {
    $wExe = "$Out\weekly_quest_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\weekly" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- /bigobj "/I$ServerGame" "/Fo$Out\weekly\" "/Fe$wExe" `
        "$Here\weekly_quest_tests.cpp" "$Module\src\quests\HsWeeklyQuests.cpp"
    if ($LASTEXITCODE -ne 0) { throw "weekly_quest_tests compile failed ($LASTEXITCODE)" }
    & $wExe
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
    & python "$Module\tools\heroes\gen_hero_portraits.py" --check
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
    & python "$Module\tools\heroes\test_gen_hero_portraits.py"
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

if (Want 'hero_portrait_tests') {
    $hExe = "$Out\hero_portrait_tests.exe"
    New-Item -ItemType Directory -Force -Path "$Out\heroes" | Out-Null
    & cl.exe /nologo /EHsc /std:c++17 /O2 /W4 /WX /permissive- "/Fo$Out\heroes\" "/Fe$hExe" `
        "$Here\hero_portrait_tests.cpp" "$Module\src\heroes\HsHeroPortraits.cpp"
    if ($LASTEXITCODE -ne 0) { throw "hero_portrait_tests compile failed ($LASTEXITCODE)" }
    & $hExe "$Module\data\heroes\portraits.json"
    if ($LASTEXITCODE -ne 0) { $cppExit = 1 }
}

$luaExit = 0
$lua = $env:LUA
if (-not $lua) { $cmd = Get-Command lua -ErrorAction SilentlyContinue; if ($cmd) { $lua = $cmd.Source } }
if (-not $lua -and (Test-Path 'C:\TurtleDev\02_build\companion-recruiter-review-check\lua52.exe')) {
    $lua = 'C:\TurtleDev\02_build\companion-recruiter-review-check\lua52.exe'
}
if (-not (Want 'lua')) {
    Write-Host "`nLua tests not selected."
} elseif ($lua -and (Test-Path "$Here\lua_tests.lua")) {
    Write-Host "`n--- Lua tests ($lua) ---"
    Push-Location $Module
    & $lua "tests\lua_tests.lua"
    $luaExit = $LASTEXITCODE
    Pop-Location
} else {
    Write-Host "`nNo Lua interpreter found (set LUA=...); Lua tests skipped."
}

if ($cppExit -ne 0 -or $luaExit -ne 0) { exit 1 }
Write-Host "`nALL TESTS PASSED"
