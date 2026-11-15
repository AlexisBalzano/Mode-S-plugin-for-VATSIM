# Builds the EHS Link MSFS Community package: build\ehslink-module\ (drag into Community).
# Needs the MSFS SDK (WASM toolchain); uses $env:MSFS_SDK or C:\MSFS SDK.
$ErrorActionPreference = "Stop"
$sdk = if ($env:MSFS_SDK) { $env:MSFS_SDK.TrimEnd('\') } else { "C:\MSFS SDK" }
$w = Join-Path $sdk "WASM"
$here = $PSScriptRoot
$out = Join-Path $here "build"
$pkg = Join-Path $out "ehslink-module"
New-Item -ItemType Directory -Force (Join-Path $pkg "modules") | Out-Null

$clang = Join-Path $w "llvm\bin\clang-cl.exe"
$ld = Join-Path $w "llvm\bin\wasm-ld.exe"
$sys = Join-Path $w "wasi-sysroot"
$obj = Join-Path $out "EhsModule.o"

& $clang --driver-mode=g++ --target=wasm32-unknown-wasi "--sysroot=$sys" -std=c++17 -O3 `
    -fms-extensions -fdeclspec -fno-exceptions -fvisibility=hidden -ffunction-sections -fdata-sections `
    -Wno-ignored-attributes -Wno-unused-command-line-argument `
    -D_MSFS_WASM=1 -D__wasi__ -D_LIBCPP_HAS_NO_THREADS -D_WINDLL -D_MBCS `
    "-I$w\include" "-I$sdk\SimConnect SDK\include" `
    -c (Join-Path $here "src\EhsModule.cpp") -o $obj
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

& $ld --no-entry --allow-undefined -L "$sys\lib\wasm32-wasi" -lc -lc++ -lc++abi "$sys\lib\wasm32-wasi\libclang_rt.builtins-wasm32.a" `
    --export-dynamic --export=malloc --export=free --export=__wasm_call_ctors --export-table --gc-sections -O3 `
    $obj -o (Join-Path $pkg "modules\EhsLink.wasm")
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Copy-Item (Join-Path $here "package\manifest.json") $pkg -Force
Copy-Item (Join-Path $here "package\profiles.ini") $pkg -Force

# layout.json lists every file with its size and FILETIME (MSFS ignores files not listed).
$entries = Get-ChildItem $pkg -Recurse -File | Where-Object { $_.Name -notin "layout.json", "manifest.json" } | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($pkg.Length + 1).Replace('\', '/'); size = $_.Length; date = $_.LastWriteTimeUtc.ToFileTimeUtc() }
}
$layout = @{ content = @($entries) } | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $pkg "layout.json"), $layout, (New-Object Text.UTF8Encoding $false))

Write-Host "Built $pkg"
Get-ChildItem $pkg -Recurse -File | ForEach-Object { "  {0}  {1} bytes" -f $_.FullName.Substring($pkg.Length + 1), $_.Length }
