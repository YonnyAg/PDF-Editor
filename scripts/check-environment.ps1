param([string]$QtRoot)

# Read-only check: discovers tools without installing or changing environment variables.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'resolve-environment.ps1')

try {
    $taskEnvironment = Resolve-PdfEditorEnvironment -QtRoot $QtRoot
    Write-Output "[OK] Visual Studio C++ : $($taskEnvironment.VisualStudioRoot)"
    Write-Output "[OK] CMake : $($taskEnvironment.CMake)"
    Write-Output "[OK] CTest : $($taskEnvironment.CTest)"
    Write-Output "[OK] Qt : $($taskEnvironment.QtRoot)"
    Write-Output 'Tools located. CMake configuration will verify versions and compatibility.'
} catch {
    Write-Output "[MISSING] $($_.Exception.Message)"
    Write-Output 'See docs/DESARROLLO.md.'
    exit 1
}
