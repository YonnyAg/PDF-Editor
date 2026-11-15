[CmdletBinding()]
param([switch]$SkipQt)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path $PSScriptRoot -Parent
$taskDependencies = Get-Content -LiteralPath (Join-Path $taskProjectRoot 'dependencies.json') -Raw | ConvertFrom-Json
$taskOriginalPythonPath = $env:PYTHONPATH
Push-Location -LiteralPath $taskProjectRoot

function Ensure-Checkout([string]$Repository, [string]$Revision, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        git clone --no-checkout --depth 1 $Repository $Path
        if ($LASTEXITCODE -ne 0) { throw "Cannot clone $Repository" }
        git -C $Path fetch --depth 1 origin $Revision
        if ($LASTEXITCODE -ne 0) { throw "Cannot fetch $Revision" }
        git -C $Path checkout --detach $Revision
        if ($LASTEXITCODE -ne 0) { throw "Cannot check out $Revision" }
    }
    $taskActualRevision = git -C $Path rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $taskActualRevision -ne $Revision) {
        throw "Unexpected revision in $Path. Expected $Revision. Existing files were left unchanged."
    }
}

try {
    Ensure-Checkout $taskDependencies.pdf4qt.repository $taskDependencies.pdf4qt.revision '.tools/PDF4QT'
    $taskPdfCmake = Get-Content -LiteralPath '.tools/PDF4QT/CMakeLists.txt' -Raw
    if (-not $taskPdfCmake.Contains('# PDF Editor: skip app translations when only building the core library.')) {
        $taskPatch = Join-Path $taskProjectRoot 'third_party/patches/pdf4qt-core-only-translations.patch'
        git -C .tools/PDF4QT apply --check $taskPatch
        if ($LASTEXITCODE -ne 0) { throw 'The PDF4QT compatibility patch cannot be applied. Existing files were left unchanged.' }
        git -C .tools/PDF4QT apply $taskPatch
        if ($LASTEXITCODE -ne 0) { throw 'The PDF4QT compatibility patch failed.' }
    }
    $taskNameTreeHeader = Get-Content -LiteralPath '.tools/PDF4QT/Pdf4QtLibCore/sources/pdfnametreeloader.h' -Raw
    if (-not $taskNameTreeHeader.Contains('// PDF Editor: bound name-tree traversal before catalog initialization.')) {
        $taskNameTreePatch = Join-Path $taskProjectRoot 'third_party/patches/pdf4qt-name-tree-limits.patch'
        git -C .tools/PDF4QT apply --check $taskNameTreePatch
        if ($LASTEXITCODE -ne 0) { throw 'The PDF4QT name-tree patch cannot be applied. Existing files were left unchanged.' }
        git -C .tools/PDF4QT apply $taskNameTreePatch
        if ($LASTEXITCODE -ne 0) { throw 'The PDF4QT name-tree patch failed.' }
    }
    Ensure-Checkout $taskDependencies.vcpkg.repository $taskDependencies.vcpkg.revision '.tools/vcpkg'
    if (-not (Test-Path -LiteralPath '.tools/vcpkg/vcpkg.exe')) {
        & .\.tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
        if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
    }

    if (-not $SkipQt) {
        $taskQtRoot = Join-Path $taskProjectRoot ".tools/Qt/$($taskDependencies.qt.version)/msvc2022_64"
        $taskQtComponents = @('bin/Qt6Widgets.dll', 'lib/cmake/Qt6Svg/Qt6SvgConfig.cmake', 'lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake')
        $taskQtReady = $true
        foreach ($taskComponent in $taskQtComponents) {
            if (-not (Test-Path -LiteralPath (Join-Path $taskQtRoot $taskComponent))) { $taskQtReady = $false }
        }
        if (-not $taskQtReady) {
            python -m pip install --target .tools/python "aqtinstall==$($taskDependencies.aqtinstall)"
            if ($LASTEXITCODE -ne 0) { throw 'aqtinstall setup failed.' }
            $env:PYTHONPATH = Join-Path $taskProjectRoot '.tools/python'
            python -m aqt install-qt windows desktop $taskDependencies.qt.version $taskDependencies.qt.architecture --outputdir .tools/Qt --archives qtbase qtsvg qttools
            if ($LASTEXITCODE -ne 0) { throw 'Qt download failed.' }
        }
    }

    Write-Output 'Source checkouts and tools are ready. build.ps1 will install the native libraries through vcpkg.'
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    $env:PYTHONPATH = $taskOriginalPythonPath
    Pop-Location
}
