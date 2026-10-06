[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$dependencyRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'external'

function Sync-Dependency {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Url,
        [Parameter(Mandatory)] [string] $Commit,
        [Parameter(Mandatory)] [string[]] $SparsePaths
    )

    $target = Join-Path $dependencyRoot $Name
    if (-not (Test-Path -LiteralPath (Join-Path $target '.git'))) {
        if (Test-Path -LiteralPath $target) {
            throw "Dependency path exists but is not a Git checkout: $target"
        }
        git clone --filter=blob:none --no-checkout $Url $target
        if ($LASTEXITCODE -ne 0) { throw "Failed to clone $Name" }
    }

    git -C $target sparse-checkout init --cone
    if ($LASTEXITCODE -ne 0) { throw "Failed to initialize sparse checkout for $Name" }
    git -C $target sparse-checkout set @SparsePaths
    if ($LASTEXITCODE -ne 0) { throw "Failed to configure sparse checkout for $Name" }
    git -C $target fetch origin $Commit
    if ($LASTEXITCODE -ne 0) { throw "Failed to fetch $Commit for $Name" }
    git -C $target checkout --detach $Commit
    if ($LASTEXITCODE -ne 0) { throw "Failed to check out $Commit for $Name" }
}

New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null

Sync-Dependency -Name 'minhook' `
    -Url 'https://github.com/TsudaKageyu/minhook.git' `
    -Commit '98b74f1fc12d00313d91f10450e5b3e0036175e3' `
    -SparsePaths @('include', 'src')
Sync-Dependency -Name 'openxr-sdk' `
    -Url 'https://github.com/KhronosGroup/OpenXR-SDK.git' `
    -Commit '5267613edf3d937e3d77556a106a65c2f82b25c6' `
    -SparsePaths @('include')
Sync-Dependency -Name 'openxr-loader' `
    -Url 'https://github.com/KhronosGroup/OpenXR-SDK.git' `
    -Commit '458984d7f59d1ae6dc1b597d94b02e4f7132eaba' `
    -SparsePaths @('include', 'src')
Sync-Dependency -Name 'directx-headers' `
    -Url 'https://github.com/microsoft/DirectX-Headers.git' `
    -Commit '2c305c16da8a4450db8d7f1e7d8d014c8bc665ee' `
    -SparsePaths @('include')
Sync-Dependency -Name 'streamline' `
    -Url 'https://github.com/NVIDIA-RTX/Streamline.git' `
    -Commit 'e8aaa6eaac968711fb62473d4ae8256dde20919b' `
    -SparsePaths @('external/ngx-sdk/include')

# CMake needs the API header even when RenderDoc capture is disabled. This
# downloads the public header only, not the capture DLL or application.
$renderdocRoot = Join-Path $dependencyRoot 'renderdoc'
New-Item -ItemType Directory -Force -Path $renderdocRoot | Out-Null
Invoke-WebRequest `
    -Uri 'https://raw.githubusercontent.com/baldurk/renderdoc/e43b7c14d3c37fab664391db94f7586be29e49a0/renderdoc/api/app/renderdoc_app.h' `
    -OutFile (Join-Path $renderdocRoot 'renderdoc_app.h')

Write-Host 'Witcher3VR dependencies are ready.'
