param(
    [Parameter(Mandatory)][string]$OriginalStage,
    [Parameter(Mandatory)][string]$StagedInput,
    [Parameter(Mandatory)][string]$GamePath,
    [Parameter(Mandatory)][string]$InputSettings,
    [Parameter(Mandatory)][string]$BackupRoot,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Install only the user's separately downloaded original. No third-party payload
# is embedded in this repository. Keep the shared ASI loader and graphics proxy.
if (Get-Process witcher3 -ErrorAction SilentlyContinue) { throw 'Close The Witcher 3 first.' }
$stage = (Resolve-Path -LiteralPath $OriginalStage).Path
$game = (Resolve-Path -LiteralPath $GamePath).Path
$inputFile = (Resolve-Path -LiteralPath $InputSettings).Path
$stagedFile = (Resolve-Path -LiteralPath $StagedInput).Path
$backup = [IO.Path]::GetFullPath($BackupRoot)
if (Test-Path -LiteralPath $backup) { throw 'Use a new, empty backup directory.' }
if (!(Test-Path -LiteralPath (Join-Path $game 'bin\x64_dx12\witcher3.exe'))) { throw 'DX12 game executable missing.' }
if (!(Test-Path -LiteralPath (Join-Path $game 'bin\x64_dx12\dinput8.dll'))) { throw 'Existing ASI loader required; this installer does not replace it.' }

$payload = @(
    'mods\modFirstPersonRemastered\content\scripts\local\firstPersonRemastered.ws',
    'mods\modFirstPersonRemastered\content\scripts\local\fprSettings.ws',
    'bin\x64_dx12\NoNearCameraCulling.asi',
    'bin\x64_dx12\NoNearCameraCulling-LICENSE.txt',
    'bin\x64_dx12\UltimateASILoader-LICENSE.txt',
    'settings_tool.bat',
    'settings_tool.ps1'
)
$operations = @($payload | ForEach-Object {
    $source = Join-Path $stage $_
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing original payload: $_" }
    [pscustomobject]@{ Source = $source; Destination = (Join-Path $game $_); BackupRelative = ('game\' + $_) }
})

# Require the staged keys to preserve every unrelated line in input.settings.
$stripBindings = '(?m)^IK_\w+=\(Action=FPR_(Toggle|Modifier|Wheel|Reticle)\)\r?\n?'
$liveText = [IO.File]::ReadAllText($inputFile)
$stagedText = [IO.File]::ReadAllText($stagedFile)
if ([regex]::Replace($liveText, $stripBindings, '') -cne [regex]::Replace($stagedText, $stripBindings, '')) {
    throw 'Staged input would change unrelated controls; regenerate it from the current input.settings.'
}
if ([regex]::Matches($stagedText, 'Action=FPR_Toggle').Count -ne 11 -or
    [regex]::Matches($stagedText, 'Action=FPR_Reticle').Count -ne 11) { throw 'Expected keys missing in gameplay contexts.' }
if ([regex]::Matches($stagedText, '(?m)^IK_F8=\(Action=FPR_Toggle\)').Count -ne 11 -or
    [regex]::Matches($stagedText, '(?m)^IK_F9=\(Action=FPR_Reticle\)').Count -ne 11 -or
    $stagedText -match 'Action=FPR_(Modifier|Wheel)') { throw 'Staged keys must match the F8/F9 configuration recorded in the manifest.' }
$operations += [pscustomobject]@{ Source = $stagedFile; Destination = $inputFile; BackupRelative = 'documents\input.settings' }

$preserved = @('bin\x64_dx12\dinput8.dll', 'bin\x64_dx12\dxgi.dll') | ForEach-Object {
    $path = Join-Path $game $_
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        [pscustomobject]@{ RelativePath = $_; SHA256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    }
}
if ($DryRun) { $operations | Select-Object Source,Destination; return }

New-Item -ItemType Directory -Path $backup | Out-Null
$manifestFile = Join-Path $backup 'manifest.json'
$manifest = [ordered]@{
    Status = 'Installing'; CreatedAt = (Get-Date).ToString('o'); GamePath = $game
    InputSettings = $inputFile; BackupRoot = $backup; ModVersion = '2.0'
    SourceURL = 'https://www.nexusmods.com/witcher3/mods/13072'
    Keys = @{ Toggle = 'F8'; Reticle = 'F9'; Modifier = 'none' }
    Preserved = @($preserved); Files = @(); RuntimeTested = $false
}
function Save-Manifest { $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestFile -Encoding utf8 }
Save-Manifest
try {
    foreach ($op in $operations) {
        $sourceHash = (Get-FileHash -LiteralPath $op.Source -Algorithm SHA256).Hash
        $priorHash = $null
        $backupFile = $null
        if (Test-Path -LiteralPath $op.Destination -PathType Leaf) {
            $priorHash = (Get-FileHash -LiteralPath $op.Destination -Algorithm SHA256).Hash
            $backupFile = Join-Path $backup $op.BackupRelative
            New-Item -ItemType Directory -Path (Split-Path $backupFile -Parent) -Force | Out-Null
            Copy-Item -LiteralPath $op.Destination -Destination $backupFile
            if ((Get-FileHash -LiteralPath $backupFile -Algorithm SHA256).Hash -ne $priorHash) { throw 'Backup hash mismatch.' }
        }
        $record = [pscustomobject]@{
            Destination = $op.Destination; InstalledSHA256 = $sourceHash
            PreviousSHA256 = $priorHash; BackupFile = $backupFile; Copied = $false
        }
        $manifest.Files += $record
        Save-Manifest
        New-Item -ItemType Directory -Path (Split-Path $op.Destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $op.Source -Destination $op.Destination -Force
        if ((Get-FileHash -LiteralPath $op.Destination -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Installed hash mismatch.' }
        $record.Copied = $true
        Save-Manifest
    }
    foreach ($item in $preserved) {
        if ((Get-FileHash -LiteralPath (Join-Path $game $item.RelativePath) -Algorithm SHA256).Hash -ne $item.SHA256) { throw 'Shared loader or graphics proxy changed unexpectedly.' }
    }
    $manifest.Status = 'InstalledAndHashVerified'
    Save-Manifest
    Write-Output "Installed original FPV 2.0. Manifest: $manifestFile"
} catch {
    $manifest.Status = 'Incomplete'; Save-Manifest
    throw
}
