[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Template,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$Version
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$build = Get-EncBuildVersion $Version
$fixed = '{0},{1},{2},{3}' -f $build.Date.Year,$build.Date.Month,$build.Date.Day,$build.Patch
$source = Split-Path $PSScriptRoot -Parent
$lameLicense = (Join-Path $source 'third_party/lame/COPYING').Replace('\','/')
$lameNotice = (Join-Path $source 'third_party/lame/LICENSE').Replace('\','/')
$content = (Get-Content -LiteralPath $Template -Encoding UTF8 -Raw).
    Replace('@ENC_FIXED_VERSION@', $fixed).Replace('@ENC_BUILD_VERSION@', $build.Name).
    Replace('@ENC_LAME_LICENSE_PATH@', $lameLicense).Replace('@ENC_LAME_NOTICE_PATH@', $lameNotice)
if ($content -match '@[A-Z_]+@') { throw 'Unresolved version resource placeholder.' }
New-Item -ItemType Directory -Path (Split-Path $OutputPath -Parent) -Force | Out-Null
# Run every build to refresh automatic dates, but preserve incremental builds.
if (-not (Test-Path -LiteralPath $OutputPath) -or
    [IO.File]::ReadAllText($OutputPath) -cne $content) {
    [IO.File]::WriteAllText($OutputPath, $content, (New-Object Text.UTF8Encoding($false)))
}
