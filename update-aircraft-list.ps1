# Regenerates the aircraft list in README.md and the website fallback (docs\index.html) from
# msfs-module\package\profiles.ini, so the three never disagree. Run after editing profiles.ini
# (package.ps1 runs it too). Only the text between the "aircraft-list" markers is replaced.
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$ini = Join-Path $root "msfs-module\package\profiles.ini"

$rows = @(); $cur = $null
foreach ($line in Get-Content $ini) {
    $t = $line.Trim()
    if ($t -match '^\[(.+)\]$') {
        $name = $Matches[1]
        if ($name -in @('Update', 'Standard')) { $cur = $null; continue }
        $cur = [pscustomobject]@{ Name = ($name -replace '\s*\(draft\)\s*$', ''); Verified = $false; Sims = '' }
        $rows += $cur
    } elseif ($cur -and $t -match '^verified\s*=\s*(.+)$') { $cur.Verified = $Matches[1].Trim() -match '^(yes|true|1)$' }
    elseif ($cur -and $t -match '^sims\s*=\s*(.+)$') { $cur.Sims = $Matches[1].Trim() }
}
$rows = @($rows | Sort-Object @{ Expression = { -not $_.Verified } }, Name)
$nV = @($rows | Where-Object Verified).Count
$nD = $rows.Count - $nV
function Test-Sim($r, $y) { $r.Verified -and $r.Sims -match $y }
function Get-Tick($b) { if ($b) { [string][char]0x2713 } else { '' } }

# README: markdown table
$md = @("| Aircraft | Status | MSFS 2020 | MSFS 2024 |", "|---|---|:-:|:-:|")
foreach ($r in $rows) {
    $st = if ($r.Verified) { 'Verified' } else { 'Draft' }
    $md += "| $($r.Name) | $st | $(Get-Tick (Test-Sim $r '2020')) | $(Get-Tick (Test-Sim $r '2024')) |"
}
$md += ""
$md += "$nV verified, $nD draft. A tick means checked in that simulator's cockpit."

# Website fallback (the page replaces it with the live list when GitHub is reachable)
function ConvertTo-Html([string]$s) { $s.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;') }
$v = ($rows | Where-Object Verified | ForEach-Object { ConvertTo-Html $_.Name }) -join ', '
$d = ($rows | Where-Object { -not $_.Verified } | ForEach-Object { ConvertTo-Html $_.Name }) -join ', '
$html = @("      <p><strong>Verified</strong>: $v.</p>", "      <p style=`"margin:0`"><strong>Draft</strong>: $d.</p>")

function Set-Between($path, $start, $end, [string[]]$body) {
    $text = [IO.File]::ReadAllText($path)
    $i = $text.IndexOf($start); $j = $text.IndexOf($end)
    if ($i -lt 0 -or $j -lt $i) { throw "Markers '$start' / '$end' not found in $path" }
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $new = $text.Substring(0, $i + $start.Length) + $nl + ($body -join $nl) + $nl + $text.Substring($j)
    [IO.File]::WriteAllText($path, $new, (New-Object Text.UTF8Encoding $false))
}
Set-Between (Join-Path $root "README.md") "<!-- aircraft-list:start -->" "<!-- aircraft-list:end -->" $md
Set-Between (Join-Path $root "docs\index.html") "<!-- aircraft-list:start -->" "<!-- aircraft-list:end -->" $html
Write-Host "Aircraft list updated: $nV verified, $nD draft"
