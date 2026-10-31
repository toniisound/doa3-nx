# Play DOA3 on the local Release build: pwsh tools/play.ps1 [-Build] [-VSync]
param([switch]$Build, [switch]$VSync)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$buildDir = Join-Path $root 'build\recomp-program'
$exe = Join-Path $buildDir 'Release\recomp_program_runner.exe'
if ($Build) {
    cmake --build $buildDir --config Release --target recomp_program_runner
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
if (-not (Test-Path $exe)) {
    throw "No Release runner at $exe. Configure build\recomp-program (docs/bring-up.md), then pass -Build."
}

# A hard-linked view of the extracted disc keeps the game's cache and saves
# (.recomp-storage) out of the source disc. The first boots fill the cache.
# The build records the disc it built from; copies cover other drives.
$pathFile = Join-Path $root 'private\disc-path.txt'
$source = if (Test-Path $pathFile) { (Get-Content $pathFile -TotalCount 1).Trim() }
          else { Join-Path $root 'private\imported-disc\disc' }
$source = (Resolve-Path -LiteralPath $source).Path.TrimEnd('\')
# The build records the installed version, its play folder and XBE name;
# a build older than that is USA 3.0 in play-disc.
$installed = try { Get-Content (Join-Path $root 'private\build.json') -Raw -ErrorAction Stop | ConvertFrom-Json } catch { $null }
$disc = Join-Path $root ('private\' + $(if ($installed) { $installed.play_disc } else { 'play-disc' }))
$xbeName = if ($installed) { $installed.xbe } else { 'default.xbe' }
if (-not (Test-Path $disc)) {
    # Fill a staging folder so an interrupted copy is never taken as complete.
    $staging = "$disc.tmp"
    if (Test-Path $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    Get-ChildItem -LiteralPath $source -File -Recurse | ForEach-Object {
        $file = $_.FullName
        $relative = $file.Substring($source.Length + 1)
        if ($relative -like '.recomp-storage\*') { return }
        $target = Join-Path $staging $relative
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        try { New-Item -ItemType HardLink -Path $target -Target $file -ErrorAction Stop | Out-Null }
        catch { Copy-Item -LiteralPath $file -Destination $target }
    }
    Rename-Item -LiteralPath $staging -NewName (Split-Path $disc -Leaf)
}

# The swimsuit switch: DOAXBV swimsuits built from the player's DOAXBV disc
# (an .iso or its extracted folder). On unless the launcher turned it off.
$swimsuits = $true
try { $saved = Get-Content (Join-Path $root 'private\launcher.json') -Raw -ErrorAction Stop | ConvertFrom-Json
      if ($null -ne $saved.swimsuits) { $swimsuits = [bool]$saved.swimsuits } } catch {}
if ($swimsuits -and -not $env:RECOMP_DOAXBV_DISC) {
    $doaxbvFile = Join-Path $root 'private\doaxbv-path.txt'
    $remembered = if (Test-Path $doaxbvFile) { (Get-Content $doaxbvFile -TotalCount 1).Trim() }
    if ($remembered -and (Test-Path -LiteralPath $remembered)) { $env:RECOMP_DOAXBV_DISC = $remembered }
    else {
        # None chosen: a DOAXBV .iso or extracted folder (datahdd.afs and datadvd.afs) in private\,
        # or the disc of a doaxbv-re checkout beside this one. Remembered for the launcher.
        $private = Join-Path $root 'private'
        $found = Get-ChildItem $private -Filter *.iso -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'xtreme|doax|beach' } | Select-Object -First 1 -ExpandProperty FullName
        if (-not $found) {
            $sister = Join-Path (Split-Path $root) 'doaxbv-re\private\disc'
            $found = "$private\*\datahdd.afs", "$private\*\*\datahdd.afs", "$sister\datahdd.afs", "$sister\*\datahdd.afs" |
                ForEach-Object { Get-ChildItem $_ -File -ErrorAction SilentlyContinue } |
                Where-Object { Test-Path (Join-Path $_.DirectoryName 'datadvd.afs') } |
                Select-Object -First 1 -ExpandProperty DirectoryName
        }
        if ($found) { $env:RECOMP_DOAXBV_DISC = $found; [IO.File]::WriteAllText($doaxbvFile, $found) }
    }
}
$packed = Join-Path $root 'recipes\doaxbv-suits.pack.gz'
if (-not (Test-Path $packed)) { $packed = Join-Path $root 'private\doaxbv-suits.pack.gz' }
$pack = Join-Path $root 'private\doaxbv-suits.pack'
if ($swimsuits -and $env:RECOMP_DOAXBV_DISC -and (Test-Path $packed)) {
    if (-not (Test-Path $pack) -or (Get-Item $pack).LastWriteTime -lt (Get-Item $packed).LastWriteTime) {
        $in = [IO.File]::OpenRead($packed); $out = [IO.File]::Create("$pack.tmp")
        $gz = New-Object IO.Compression.GZipStream($in, [IO.Compression.CompressionMode]::Decompress)
        $gz.CopyTo($out); $gz.Dispose(); $out.Dispose(); $in.Dispose()
        Move-Item -Force "$pack.tmp" $pack
    }
    $env:RECOMP_COSTUME_PACK = $pack
    if ($saved -and $saved.swimsuit_start_favorite) { $env:RECOMP_COSTUME_START_FAVORITE = '1' }
}

# Test-harness settings would speed the game up or stop it after a timeout.
Remove-Item Env:RECOMP_UNPACED, Env:RECOMP_WATCHDOG_MS -ErrorAction SilentlyContinue
$logs = Join-Path $root 'private\play-logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$log = Join-Path $logs ((Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
# Win32_Process.Create detaches the runner from this shell's job; a launcher or
# agent shell that exits otherwise sends the game window a close request.
# It starts from the WMI service's environment, so pass ours (the launcher's
# RECOMP_* settings) explicitly.
$vars = [string[]](Get-ChildItem Env: | ForEach-Object { "$($_.Name)=$($_.Value)" })
$startup = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ EnvironmentVariables = $vars }
$cmd = 'cmd /c ""{0}" --xbe "{1}"{2} 2>"{3}" >"{3}.out""' -f $exe, (Join-Path $disc $xbeName), $(if ($VSync) { ' --vsync' } else { '' }), $log
$res = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = $root; ProcessStartupInformation = $startup }
if ($res.ReturnValue) { throw "Could not start the runner (Win32_Process.Create returned $($res.ReturnValue))." }
"pid=$($res.ProcessId) log=$log"
# Detached, so the exit code is lost; a runner gone within seconds failed to start.
Start-Sleep -Seconds 5
if (-not (Get-Process -Id $res.ProcessId -ErrorAction SilentlyContinue)) {
    Get-Content -LiteralPath $log -Tail 20 -ErrorAction SilentlyContinue
    Write-Error "The runner exited during startup; see $log" -ErrorAction Continue
    exit 1
}
