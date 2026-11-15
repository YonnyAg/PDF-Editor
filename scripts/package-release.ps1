[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RuntimeDirectory,
    [string]$QtRoot,
    [string]$InnoCompiler,
    [string]$Python = 'python',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'resolve-environment.ps1')
$environment = Resolve-PdfEditorEnvironment -QtRoot $QtRoot
$root = $environment.ProjectRoot
$release = Get-Content -LiteralPath (Join-Path $root 'packaging/release.json') -Raw | ConvertFrom-Json
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root "build/releases/$($release.version)" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $output 'PDF Editor'
# Never erase a previous package or an arbitrary caller-supplied directory.
if (Test-Path -LiteralPath $stage) { throw "Staging directory already exists: $stage. Choose a new OutputDirectory." }
if (-not $InnoCompiler) { $InnoCompiler = Join-Path $root '.tools/InnoSetup-7.1.1/ISCC.exe' }
if (-not (Test-Path -LiteralPath $InnoCompiler)) { throw 'Install Inno Setup 7.1.1 and pass -InnoCompiler.' }
$version = (Get-Item -LiteralPath (Join-Path $runtime 'pdf_editor.exe')).VersionInfo.ProductVersion
if ($version -ne $release.version) { throw "Runtime version mismatch: $version" }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$nativeDlls = @('Pdf4QtLibCore.dll','asmjit.dll','blend2d.dll','brotlicommon.dll','brotlidec.dll','bz2.dll','freetype.dll','jpeg62.dll','lcms2-2.dll','libcrypto-3-x64.dll','libpng16.dll','openjp2.dll','zlib1.dll')
foreach ($name in @('pdf_editor.exe') + $nativeDlls) {
    Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination $stage
}
$previousPath = $env:PATH
try {
    $env:PATH = "$(Join-Path $environment.QtRoot 'bin');$env:SystemRoot\System32;$env:SystemRoot"
    & (Join-Path $environment.QtRoot 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime --dir $stage (Join-Path $stage 'pdf_editor.exe') (Join-Path $stage 'Pdf4QtLibCore.dll') *> (Join-Path $output 'windeployqt.txt')
    if ($LASTEXITCODE) { throw 'windeployqt failed.' }
    $redist = Get-ChildItem -LiteralPath (Join-Path $environment.VisualStudioRoot 'VC/Redist/MSVC') -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $redist) { throw 'MSVC redistributable not found.' }
    Copy-Item -Path (Join-Path $redist.FullName 'x64/Microsoft.VC143.CRT/*.dll') -Destination $stage
    & $Python (Join-Path $PSScriptRoot 'release-assets.py') collect $stage
    if ($LASTEXITCODE) { throw 'License/source collection failed.' }
    Copy-Item -LiteralPath (Join-Path (Split-Path $InnoCompiler -Parent) 'license.txt') -Destination (Join-Path $stage 'licenses/Inno-Setup.txt')
    New-Item -ItemType Directory -Path (Join-Path $stage 'translations') -Force | Out-Null
    & (Join-Path $environment.QtRoot 'bin/lrelease.exe') (Join-Path $output 'qtbase_es.ts') -qm (Join-Path $stage 'translations/qtbase_es.qm')
    if ($LASTEXITCODE) { throw 'Spanish Qt translation generation failed.' }
} finally { $env:PATH = $previousPath }
"[Paths]`nPrefix=.`nPlugins=.`nTranslations=translations`n" | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding utf8
foreach ($name in 'LICENSE','THIRD_PARTY_NOTICES.md','dependencies.json') {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage
}
foreach ($name in 'LEEME.txt','PRIVACIDAD.txt','FUENTES.md') {
    Copy-Item -LiteralPath (Join-Path $root "packaging/$name") -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $root 'packaging/release.json') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'docs/PASO_27.md') -Destination (Join-Path $stage 'FUNCIONES-Y-LIMITES.md')
$forbidden = Get-ChildItem -LiteralPath $stage -File -Recurse | Where-Object { $_.Name -match '(_tests\.exe$|Qt6Test|\.pdb$|VerifyDetachedSignature|^Qt6.*d\.dll$|^Pdf4QtLibCored\.dll$)' }
if ($forbidden) { throw "Development files in runtime: $($forbidden.FullName -join ', ')" }
$manifest = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{ File = [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\','/'); SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash; Bytes = $_.Length }
})
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'manifest-sha256.json') -Encoding utf8
& $InnoCompiler "/DStageDir=$stage" "/DOutputDir=$output" "/DReleaseVersion=$($release.version)" "/DWindowsVersion=$($release.windowsVersion)" "/DProductAppId=$($release.appId)" (Join-Path $root 'packaging/windows/setup.iss') *> (Join-Path $output 'inno-compile.txt')
if ($LASTEXITCODE) { throw "Installer compilation failed. See $output/inno-compile.txt" }
Compress-Archive -LiteralPath $stage -DestinationPath (Join-Path $output "PDF-Editor-$($release.version)-windows-x64-portable.zip") -CompressionLevel Optimal
& $Python (Join-Path $PSScriptRoot 'release-assets.py') archives $output
if ($LASTEXITCODE) { throw 'Source archive generation failed.' }
Get-ChildItem -LiteralPath $output -File | Where-Object { $_.Extension -in '.exe','.zip' } | Sort-Object Name | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
Write-Output "Release artifacts: $output"
