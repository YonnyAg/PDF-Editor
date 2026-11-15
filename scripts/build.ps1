[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$QtRoot,
    [switch]$Fresh,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'resolve-environment.ps1')
$taskOriginalPath = $env:PATH
$taskOriginalVcpkgTools = $env:VCPKG_FORCE_DOWNLOADED_BINARIES

try {
    # Keep tool versions stable between Visual Studio, PowerShell and automation.
    $env:VCPKG_FORCE_DOWNLOADED_BINARIES = '1'
    $taskEnvironment = Resolve-PdfEditorEnvironment -QtRoot $QtRoot
    $taskDependencies = Get-Content -LiteralPath (Join-Path $taskEnvironment.ProjectRoot 'dependencies.json') -Raw | ConvertFrom-Json
    foreach ($taskCheckout in @('PDF4QT', 'vcpkg')) {
        $taskDependency = if ($taskCheckout -eq 'PDF4QT') { $taskDependencies.pdf4qt } else { $taskDependencies.vcpkg }
        $taskCheckoutPath = Join-Path $taskEnvironment.ProjectRoot ".tools/$taskCheckout"
        if (-not (Test-Path -LiteralPath (Join-Path $taskCheckoutPath '.git'))) {
            throw "Missing $taskCheckout. Run scripts/setup-dependencies.ps1 first."
        }
        $taskRevision = git -C $taskCheckoutPath rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $taskRevision -ne $taskDependency.revision) {
            throw "Unexpected $taskCheckout revision. See dependencies.json."
        }
    }
    # Scope DLL discovery to this process and its children, not the user's global PATH.
    $env:PATH = "$(Join-Path $taskEnvironment.QtRoot 'bin');$taskOriginalPath"
    Push-Location -LiteralPath $taskEnvironment.ProjectRoot
    try {
        $taskConfigureArguments = @('--preset', 'windows-msvc', "-DCMAKE_PREFIX_PATH=$($taskEnvironment.QtRoot)")
        # Codex/runtime updates can move pwsh and leave vcpkg's cached path stale.
        $taskPowerShell = Get-Command pwsh.exe -ErrorAction SilentlyContinue
        if (-not $taskPowerShell) { $taskPowerShell = Get-Command powershell.exe -ErrorAction SilentlyContinue }
        if ($taskPowerShell) {
            $taskConfigureArguments += "-DZ_VCPKG_PWSH_PATH=$($taskPowerShell.Source)"
            $taskConfigureArguments += "-DZ_VCPKG_POWERSHELL_PATH=$($taskPowerShell.Source)"
        }
        $taskCachePath = Join-Path $taskEnvironment.ProjectRoot 'build/windows-msvc/CMakeCache.txt'
        if (Test-Path -LiteralPath $taskCachePath) {
            $taskCache = Get-Content -LiteralPath $taskCachePath -Raw
            if ($taskCache -notmatch '(?m)^CMAKE_TOOLCHAIN_FILE:[^=]*=.*vcpkg\.cmake\r?$') {
                # Migrate the initial Qt-only build without deleting source files.
                $Fresh = $true
            }
        }
        if ($Fresh) { $taskConfigureArguments += '--fresh' }
        & $taskEnvironment.CMake @taskConfigureArguments
        if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

        & $taskEnvironment.CMake --build build/windows-msvc --config $Configuration --parallel 2
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }

        & $taskEnvironment.CTest --test-dir build/windows-msvc -C $Configuration --output-on-failure --no-tests=error
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

        if ($Run) {
            & (Join-Path $PSScriptRoot 'run.ps1') -Configuration $Configuration -QtRoot $taskEnvironment.QtRoot
            if (-not $?) { throw 'Application launch failed.' }
        }
    } finally {
        Pop-Location
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    $env:PATH = $taskOriginalPath
    $env:VCPKG_FORCE_DOWNLOADED_BINARIES = $taskOriginalVcpkgTools
}
