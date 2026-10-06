[CmdletBinding()]
param(
    [string] $GameDirectory = 'E:\SteamLibrary\steamapps\common\The Witcher 3',
    [string] $ReportPath
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $GameDirectory 'bin\x64_dx12\witcher3.exe'
$bytes = [System.IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw 'Not a PE executable.' }
$sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
$optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
$sectionTable = $pe + 24 + $optionalSize
function Get-RvaOffset([uint32] $Rva) {
    for ($index = 0; $index -lt $sectionCount; $index++) {
        $section = $sectionTable + $index * 40
        $virtualAddress = [BitConverter]::ToUInt32($bytes, $section + 12)
        $rawSize = [BitConverter]::ToUInt32($bytes, $section + 16)
        $rawAddress = [BitConverter]::ToUInt32($bytes, $section + 20)
        if ($Rva -ge $virtualAddress -and $Rva -lt ($virtualAddress + $rawSize)) {
            return [int]($rawAddress + $Rva - $virtualAddress)
        }
    }
    return -1
}
# Exact function entries used by upstream install_engine_native_head_pose_hooks.
# Matching these is necessary for the provider, NOT sufficient for all VR hooks.
$checks = @(
    @{ Name='GetHeadBoneIndex'; Rva=0x01C4B550; Expected=[byte[]](0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0xFF,0x42,0x30,0x48,0x81,0xC1,0x00,0x02,0x00,0x00,0x49,0x8B,0xD8,0x48,0x8B,0x01,0xFF,0x50,0x38) },
    @{ Name='GetBoneWorldMatrix'; Rva=0x0152E600; Expected=[byte[]](0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55,0x48,0x8D,0x68,0xA1,0x48,0x81,0xEC,0x00,0x01,0x00,0x00) }
)
$results = foreach ($check in $checks) {
    $offset = Get-RvaOffset $check.Rva
    $matches = $offset -ge 0 -and ($offset + $check.Expected.Length) -le $bytes.Length
    if ($matches) {
        for ($index = 0; $index -lt $check.Expected.Length; $index++) {
            if ($bytes[$offset + $index] -ne $check.Expected[$index]) { $matches = $false; break }
        }
    }
    [PSCustomObject]@{ Function=$check.Name; Rva=('0x{0:X8}' -f $check.Rva); Match=$matches }
}
$report = [PSCustomObject]@{
    ExecutableVersion=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion
    ExecutableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    UpstreamCommit='79293dd5f523af11579e061aa0b777d65225911c'
    HeadProviderSignaturesMatch=(@($results | Where-Object { -not $_.Match }).Count -eq 0)
    Checks=$results
    GameRuntimeValidated=$false
    FullVrCompatibilityValidated=$false
    Note='Static check of two first-person callbacks only. Does not validate stereo, controllers, combat or FPS.'
}
$json = $report | ConvertTo-Json -Depth 5
if ($ReportPath) { Set-Content -LiteralPath $ReportPath -Value $json -Encoding utf8 }
$json
if (-not $report.HeadProviderSignaturesMatch) { exit 3 }
