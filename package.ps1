# Builds everything and assembles the release folder + zip:
#   release\EHS-Link-<version>\
#     Community\ehslink-module\      pilots: drag into the MSFS Community folder
#     vPilot\EhsLink.vPilot.dll      pilots: drag into vPilot's Plugins folder
#     EuroScope\EhsLink.dll          controllers: load in EuroScope
#     README.md, PROTOCOL.md, LICENSE
param([string]$Version = "0.1.0")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

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
Copy-Item (Join-Path $root "README.md"), (Join-Path $root "PROTOCOL.md"), (Join-Path $root "LICENSE") $rel

$zip = "$rel.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $rel -DestinationPath $zip
Write-Host "Release: $zip"
