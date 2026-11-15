[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$QtRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'resolve-environment.ps1')
$taskOriginalPath = $env:PATH

try {
    $taskEnvironment = Resolve-PdfEditorEnvironment -QtRoot $QtRoot
    $taskExecutable = Join-Path $taskEnvironment.ProjectRoot "build/windows-msvc/$Configuration/pdf_editor.exe"
    if (-not (Test-Path -LiteralPath $taskExecutable)) {
        throw "Application not compiled for $Configuration. Run scripts/build.ps1 -Configuration $Configuration first."
    }

    # The application inherits this PATH; the user's environment stays unchanged.
    $env:PATH = "$(Join-Path $taskEnvironment.QtRoot 'bin');$taskOriginalPath"
    $taskProcess = Start-Process -FilePath $taskExecutable -WorkingDirectory $taskEnvironment.ProjectRoot -WindowStyle Normal -PassThru
    if (-not $taskProcess.WaitForInputIdle(10000)) {
        throw "The application did not become ready within 10 seconds (PID $($taskProcess.Id))."
    }
    $taskProcess.Refresh()
    if ($taskProcess.HasExited) {
        throw "Application exited with code $($taskProcess.ExitCode)."
    }
    Write-Output "PDF Editor started (PID $($taskProcess.Id)). No build or dependency installation was run."
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    $env:PATH = $taskOriginalPath
}
