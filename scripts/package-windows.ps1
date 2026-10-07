param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$QtDirectory,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$LicenseDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Version = '0.2.0',
    [ValidateSet('Installed','Portable')][string]$Edition = 'Portable',
    [string]$InnoCompiler
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $OutputDirectory "CodexSync-v$Version-windows-x64-$($Edition.ToLowerInvariant())"
if (Test-Path -LiteralPath $destination) { throw 'Package destination already exists; use a new output directory.' }
foreach ($file in @('CodexSync.exe','codex-sync.exe','cxs-vss-helper.exe','codex_sync.dll','libsodium.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory $file))) { throw "Missing build output: $file" }
}
if (!(Test-Path -LiteralPath (Join-Path $LicenseDirectory 'Qt'))) { throw 'Provide Qt runtime license texts.' }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
foreach ($file in @('CodexSync.exe','codex-sync.exe','cxs-vss-helper.exe','codex_sync.dll','libsodium.dll')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory $file) -Destination $destination
}
& (Join-Path $QtDirectory 'bin\windeployqt.exe') --release --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --qmldir (Join-Path $project 'ui') --dir $destination (Join-Path $destination 'CodexSync.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# App-local redistributable runtime, without changing the user's installed runtime.
foreach ($file in Get-ChildItem -LiteralPath $RuntimeDirectory -File -Filter '*.dll') {
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}
foreach ($file in @('README.md','DISCLAIMER.md','DISCLAIMER.en.md','LICENSE','THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $project $file) -Destination $destination
}
New-Item -ItemType Directory -Path (Join-Path $destination 'include') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'include\codex_sync.h') -Destination (Join-Path $destination 'include')
New-Item -ItemType Directory -Path (Join-Path $destination 'licenses') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'third_party\licenses') -Destination (Join-Path $destination 'licenses\source-dependencies') -Recurse
Copy-Item -LiteralPath (Join-Path $project 'ui\icons\LICENSE.txt') -Destination (Join-Path $destination 'licenses\fluent-icons-MIT.txt')
Copy-Item -LiteralPath $LicenseDirectory -Destination (Join-Path $destination 'licenses\runtime') -Recurse
$qtVersion = (& (Join-Path $QtDirectory 'bin\qmake.exe') -query QT_VERSION).Trim()
$sourceInfo = @"
CodexSync upstream: https://github.com/zybin7890/CodexSync
Exact corresponding source: CodexSync-v$Version-source.zip supplied beside these local packages.
Qt $qtVersion shared libraries are unmodified upstream libraries, used under LGPL-3.0.
Qt corresponding source: https://download.qt.io/official_releases/qt/6.8/$qtVersion/single/qt-everywhere-src-$qtVersion.tar.xz
Qt module source and attribution files: https://github.com/qt/qtbase/tree/v$qtVersion ; https://github.com/qt/qtdeclarative/tree/v$qtVersion ; https://github.com/qt/qtsvg/tree/v$qtVersion
Qt libraries are dynamically linked and can be replaced with compatible builds.
libsodium 1.0.22 source: https://github.com/jedisct1/libsodium/tree/1.0.22-RELEASE
Microsoft VC runtime DLLs are unmodified app-local redistributable runtime files.
"@
[IO.File]::WriteAllText((Join-Path $destination 'DEPENDENCY_SOURCES.txt'), $sourceInfo, [Text.UTF8Encoding]::new($false))
$forbidden = @(Get-ChildItem -LiteralPath $destination -Recurse -File | Where-Object {
    $_.Name -match '\.(sqlite.*|db.*|jsonl.*|key|pem|pfx|p12|cxs|dpapi|pdb|log)$' -or $_.Name -match '^(auth\.json|config\.toml.*|sync\.json|history-coverage\.json|\.env.*)$'
})
if ($forbidden.Count) { throw 'Unexpected local data or debug/test artifacts in package' }
if ($Edition -eq 'Portable') {
    [IO.File]::WriteAllText((Join-Path $destination 'portable.flag'), 'CodexSync portable edition; runtime data stays in ./data', [Text.UTF8Encoding]::new($false))
    Compress-Archive -LiteralPath $destination -DestinationPath (Join-Path $OutputDirectory "CodexSync-v$Version-windows-x64-portable.zip")
} else {
    if (!$InnoCompiler -or !(Test-Path -LiteralPath $InnoCompiler)) { throw 'Provide the official Inno Setup compiler for the installed edition.' }
    & $InnoCompiler "/DPayloadDirectory=$destination" "/DOutputDirectory=$OutputDirectory" "/DAppVersion=$Version" (Join-Path $project 'packaging\windows.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
}
Write-Output $destination
