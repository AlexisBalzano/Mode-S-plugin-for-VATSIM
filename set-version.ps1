# Sets the EHS Link version everywhere it appears, so all parts always match.
#   .\set-version.ps1 0.3.0
# Then add a "## 0.3.0" section to CHANGELOG.md and run .\package.ps1
param([Parameter(Mandatory = $true)][string]$Version)
$ErrorActionPreference = "Stop"
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 1.2.3" }
$root = $PSScriptRoot

function Stamp([string]$rel, [string]$pattern, [string]$replacement) {
    $path = Join-Path $root $rel
    $text = [IO.File]::ReadAllText($path)
    $new = [regex]::Replace($text, $pattern, $replacement)
    if ($new -eq $text -and $text -notmatch [regex]::Escape($Version)) { throw "Version pattern not found in $rel" }
    [IO.File]::WriteAllText($path, $new, (New-Object Text.UTF8Encoding $false))
    Write-Host "  $rel"
}

Write-Host "Setting version $Version in:"
Stamp "VERSION" '^\d+\.\d+\.\d+' $Version
Stamp "vpilot-plugin\EhsLinkPlugin.cs" 'Version = "\d+\.\d+\.\d+"' "Version = `"$Version`""
Stamp "vpilot-plugin\EhsLinkVpilot.csproj" '<Version>\d+\.\d+\.\d+</Version>' "<Version>$Version</Version>"
Stamp "euroscope-plugin\src\EhsLinkPlugin.cpp" 'kVersion = "\d+\.\d+\.\d+"' "kVersion = `"$Version`""
Stamp "msfs-module\package\manifest.json" '"package_version": "\d+\.\d+\.\d+"' "`"package_version`": `"$Version`""

if (-not (Select-String -Path (Join-Path $root "CHANGELOG.md") -Pattern "^## $([regex]::Escape($Version))\b" -Quiet)) {
    Write-Warning "CHANGELOG.md has no '## $Version' section yet: add one before releasing."
}
