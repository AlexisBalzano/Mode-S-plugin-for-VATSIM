# Builds everything and assembles the release folder + zip:
#   release\EHS-Link-<version>\
#     Community\ehslink-module\      pilots: drag into the MSFS Community folder
#     vPilot\EhsLink.vPilot.dll      pilots: drag into vPilot's Plugins folder
#     EuroScope\EhsLink.dll          controllers: load in EuroScope
#     README.md, PROTOCOL.md, LICENSE
# Version comes from the VERSION file (change it with .\set-version.ps1 x.y.z).
param([string]$Version = "")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if (-not $Version) { $Version = (Get-Content (Join-Path $root "VERSION") -TotalCount 1).Trim() }
if (-not (Select-String -Path (Join-Path $root "CHANGELOG.md") -Pattern "^## $([regex]::Escape($Version))\b" -Quiet)) {
    Write-Warning "CHANGELOG.md has no '## $Version' section: add one before publishing this release."
}
Write-Host "Packaging EHS Link $Version"
& (Join-Path $root "update-aircraft-list.ps1")

& (Join-Path $root "msfs-module\build.ps1")
dotnet build (Join-Path $root "vpilot-plugin") -c Release -nologo -v q
if ($LASTEXITCODE -ne 0) { throw "vPilot plugin build failed" }
# (vcvarsall prints a harmless 'vswhere' message on stderr, so run it as its own process)
# (and wait only for cmd itself: the compiler leaves mspdbsrv.exe running, which Start-Process -Wait would wait for)
$p = [System.Diagnostics.Process]::Start("cmd.exe", "/c `"`"$(Join-Path $root 'euroscope-plugin\build.bat')`"`"")
$p.WaitForExit()
if ($p.ExitCode -ne 0) { throw "EuroScope plugin build failed" }

$rel = Join-Path $root "release\EHS-Link-$Version"
if (Test-Path $rel) { Remove-Item $rel -Recurse -Force }
New-Item -ItemType Directory -Force "$rel\Community", "$rel\vPilot", "$rel\EuroScope" | Out-Null
Copy-Item (Join-Path $root "msfs-module\build\ehslink-module") "$rel\Community" -Recurse
Copy-Item (Join-Path $root "vpilot-plugin\bin\Release\net472\EhsLink.vPilot.dll") "$rel\vPilot"
Copy-Item (Join-Path $root "euroscope-plugin\build\EhsLink.dll") "$rel\EuroScope"
Copy-Item (Join-Path $root "README.md"), (Join-Path $root "PROTOCOL.md"), (Join-Path $root "LICENSE"), (Join-Path $root "CHANGELOG.md") $rel

$zip = "$rel.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $rel -DestinationPath $zip
Write-Host "Release: $zip"
