[CmdletBinding()]
param([string]$QtRoot, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'resolve-environment.ps1')
$environment = Resolve-PdfEditorEnvironment -QtRoot $QtRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $env:TEMP 'pdf-editor-icon-build' }
& $environment.CMake -S (Join-Path $environment.ProjectRoot 'tools/icon-generator') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$($environment.QtRoot)"
if ($LASTEXITCODE) { throw 'Icon generator configuration failed.' }
& $environment.CMake --build $BuildDirectory --config Release --parallel 2
if ($LASTEXITCODE) { throw 'Icon generator compilation failed.' }
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM
try {
    $env:PATH = "$(Join-Path $environment.QtRoot 'bin');$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'offscreen'
    & (Join-Path $BuildDirectory 'Release/pdf_editor_icons.exe') (Join-Path $environment.ProjectRoot 'resources/branding/pdf-editor.svg') (Join-Path $environment.ProjectRoot 'resources/branding')
    if ($LASTEXITCODE) { throw 'Icon generation failed.' }
} finally { $env:PATH = $previousPath; $env:QT_QPA_PLATFORM = $previousPlatform }
