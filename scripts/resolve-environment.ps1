function Resolve-PdfEditorEnvironment {
    [CmdletBinding()]
    param([string]$QtRoot)

    $taskProjectRoot = Split-Path $PSScriptRoot -Parent
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $taskVswhere)) {
        throw 'Visual Studio Installer not found. Install the Visual Studio 2022 C++ desktop tools.'
    }

    $taskVisualStudio = @(& $taskVswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $taskVisualStudio.Count -eq 0) {
        throw 'Visual Studio 2022 C++ desktop tools not found.'
    }
    $taskVsRoot = $taskVisualStudio[0]
    $taskCmakeBin = Join-Path $taskVsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'

    $taskTools = @{}
    foreach ($taskName in @('cmake', 'ctest')) {
        $taskCommand = Get-Command "$taskName.exe" -ErrorAction SilentlyContinue
        if ($taskCommand) {
            $taskTools[$taskName] = $taskCommand.Source
        } else {
            $taskCandidate = Join-Path $taskCmakeBin "$taskName.exe"
            if (-not (Test-Path -LiteralPath $taskCandidate)) {
                throw "$taskName not found. Select C++ CMake tools for Windows in Visual Studio Installer."
            }
            $taskTools[$taskName] = $taskCandidate
        }
    }

    # Explicit paths take precedence. Do not silently replace a broken user setting.
    if (-not $QtRoot) {
        $QtRoot = $env:QT_ROOT_DIR
    }
    if (-not $QtRoot) {
        $taskQtRoots = @((Join-Path $taskProjectRoot '.tools\Qt'), 'C:\Qt', 'D:\Qt')
        foreach ($taskQtRoot in $taskQtRoots) {
            if (-not (Test-Path -LiteralPath $taskQtRoot)) { continue }
            $taskVersions = Get-ChildItem -LiteralPath $taskQtRoot -Directory |
                Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending
            foreach ($taskVersion in $taskVersions) {
                if ([version]$taskVersion.Name -lt [version]'6.9.0') { continue }
                $taskCandidate = Join-Path $taskVersion.FullName 'msvc2022_64'
                if (Test-Path -LiteralPath (Join-Path $taskCandidate 'lib\cmake\Qt6\Qt6Config.cmake')) {
                    $QtRoot = $taskCandidate
                    break
                }
            }
            if ($QtRoot) { break }
        }
    }

    if (-not $QtRoot) {
        throw 'Qt not found. Install Qt 6.9+ for MSVC 2022 x64 or run scripts/setup-dependencies.ps1.'
    }
    $taskQtPrefix = (Resolve-Path -LiteralPath $QtRoot -ErrorAction Stop).Path
    foreach ($taskRequired in @('lib\cmake\Qt6\Qt6Config.cmake', 'bin\Qt6Widgets.dll', 'bin\Qt6Test.dll', 'lib\cmake\Qt6Svg\Qt6SvgConfig.cmake', 'lib\cmake\Qt6LinguistTools\Qt6LinguistToolsConfig.cmake')) {
        if (-not (Test-Path -LiteralPath (Join-Path $taskQtPrefix $taskRequired))) {
            throw "Qt component missing: $taskRequired under $taskQtPrefix"
        }
    }

    [pscustomobject]@{
        ProjectRoot = $taskProjectRoot
        VisualStudioRoot = $taskVsRoot
        CMake = $taskTools['cmake']
        CTest = $taskTools['ctest']
        QtRoot = $taskQtPrefix
    }
}
