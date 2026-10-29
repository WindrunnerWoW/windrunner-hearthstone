<#
 Builds tests/ai_ab.cpp (the AI A/B tool, needs HS_AI_TUNING) and runs it.
 Usage: powershell -File tests/ai_ab.ps1 [-Out <dir>] -- <games> <hard|normal> [A:key=value ...] [B:key=value ...]
   e.g. ai_ab.ps1 -Out D:\tmp 300 hard A:classBurst=0 A:resample=0
 Prints B's win rate against A over mirrored games (both seats, same decks). See the header of ai_ab.cpp for keys.
#>
param(
    [string]$Out = 'C:\TurtleDev\02_build\hearthstone-check\ai_ab',
    [Parameter(ValueFromRemainingArguments = $true)] [string[]]$ToolArgs
)
$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Module = Split-Path -Parent $Here
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio 2022 with C++ tools not found' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

$matchSrc = @(
    "$Module\src\match\HsMatch.cpp",
    "$Module\src\match\HsMatchCards.cpp",
    "$Module\src\match\HsMatchCardsBasic.cpp",
    "$Module\src\match\HsMatchCardsClassic.cpp",
    "$Module\src\match\HsMatchCardsClassicRest.cpp",
    "$Module\src\match\HsMatchCardsClassicEngine.cpp",
    "$Module\src\match\HsMatchCardsNaxx.cpp",
    "$Module\src\match\HsMatchAi.cpp",
    "$Module\src\match\HsMatchAiSearch.cpp"
)
$exe = "$Out\ai_ab.exe"
& cl.exe /nologo /EHsc /std:c++17 /O2 /W3 /permissive- /bigobj /DHS_AI_TUNING "/Fo$Out\" "/Fe$exe" "$Here\ai_ab.cpp" @matchSrc
if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }
& $exe @ToolArgs
