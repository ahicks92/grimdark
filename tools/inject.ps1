# Build, then load grimdark.dll into Grim Dawn.
#   .\tools\inject.ps1 -Launch    build, then start the game UNFOCUSED with the DLL injected before it initializes,
#                                 speech + game audio muted (pass -Speak to hear it), then wait for the dev server
#   .\tools\inject.ps1            build, then (re)inject into the running game: ejects the old DLL first so the
#                                 build can overwrite it -- this is the hot-reload loop
#   .\tools\inject.ps1 -Eject     unload only
#   -NoBuild                      skip the build step
param([switch]$Launch, [switch]$Eject, [switch]$NoBuild, [switch]$Speak, [int]$Port = 8791,
      [string]$GameExe = "")
$root = Split-Path $PSScriptRoot -Parent
# The install folder: GRIMDARK_GAME_DIR, else Steam's library lookup (tools/gamepath.py, as gdlaunch does it).
if (-not $GameExe) { $GameExe = Join-Path (& uv run --no-project python "$root\tools\gamepath.py") "x64\Grim Dawn.exe" }
$dll = "$root\build\ninja\grimdark.dll"
$inj = "$root\build\ninja\gdinject.exe"
$running = [bool](Get-Process -Name "Grim Dawn" -ErrorAction SilentlyContinue)
if ($Eject) { & $inj --eject $dll; exit $LASTEXITCODE }
if ($running -and -not $Launch -and (Test-Path $inj)) { & $inj --eject $dll | Out-Null; Start-Sleep -Milliseconds 500 }
if (-not $NoBuild) {
  & cmd /c "$root\tools\build.cmd"
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
if ($Launch) {
  if ($running) { Write-Host "Grim Dawn is already running; close it first (or use plain inject)."; exit 1 }
  # The engine stops ticking while unfocused when inactiveUpdateRate is 0; the dev loop needs it ticking.
  $opt = "$env:USERPROFILE\Documents\My Games\Grim Dawn\Settings\options.txt"
  if (Test-Path $opt) {
    $txt = Get-Content $opt -Raw
    if ($txt -match 'inactiveUpdateRate\s*=\s*0\b') {
      $txt = $txt -replace 'inactiveUpdateRate\s*=\s*0\b', 'inactiveUpdateRate        = 30'
      Set-Content -Path $opt -Value $txt -Encoding ascii -NoNewline
      Write-Host "options.txt: inactiveUpdateRate 0 -> 30 (keeps the game ticking while unfocused)"
    }
  }
  if ($Speak) { Remove-Item Env:\GRIMDARK_MUTE -ErrorAction SilentlyContinue } else { $env:GRIMDARK_MUTE = '1' }
  $env:GRIMDARK_PORT = "$Port"
  $env:GRIMDARK_NOFOCUS = '1'
  & $inj --launch $GameExe $dll
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  Write-Host "waiting for the dev server on 127.0.0.1:$Port ..."
  $deadline = (Get-Date).AddSeconds(45)
  while ((Get-Date) -lt $deadline) {
    try { $r = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/health" -TimeoutSec 2 -UseBasicParsing; Write-Host $r.Content; exit 0 } catch {}
    $gp = Get-Process -Name "Grim Dawn" -ErrorAction SilentlyContinue
    if (-not $gp) { Write-Host "game exited before the dev server came up; see %LOCALAPPDATA%\Grimdark\grimdark.log"; exit 1 }
    Start-Sleep -Seconds 1
  }
  # A crashed game sits in its crash-reporter dialog forever; do not leave those around.
  Write-Host "timed out waiting for /health; killing the instance (check grimdark.log / tools/stacks.py next time)"
  Get-Process -Name "Grim Dawn" -ErrorAction SilentlyContinue | Stop-Process -Force
  exit 1
}
& $inj $dll
exit $LASTEXITCODE
