[CmdletBinding()]
param([Parameter(Mandatory)][string]$ReleaseDirectory, [string]$QaDirectory)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Use PowerShell 7.' }
$root = Split-Path $PSScriptRoot -Parent
$release = Get-Content (Join-Path $root 'packaging/release.json') -Raw | ConvertFrom-Json
$output = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$($release.appId)_is1"
if (Test-Path -LiteralPath $uninstallKey) { throw 'An existing installation uses this AppId. Run this test in a clean Windows user profile.' }
$progId = 'HKCU:\Software\Classes\PDFEditor.Document'
$openWith = 'HKCU:\Software\Classes\.pdf\OpenWithProgids'
function Get-OpenWithNames {
    $key = Get-Item -LiteralPath $openWith -ErrorAction SilentlyContinue
    if ($key) { return $key.GetValueNames() }
    return @()
}
if ((Test-Path -LiteralPath $progId) -or ((Get-OpenWithNames) -contains 'PDFEditor.Document')) {
    throw 'Existing PDF Editor file association: use a clean Windows user profile.'
}
if (-not $QaDirectory) { $QaDirectory = Join-Path $root ('build/paso28/qa/distribution-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$qa = [IO.Path]::GetFullPath($QaDirectory)
if (Test-Path -LiteralPath $qa) { throw 'QA directory already exists; choose a new one.' }
New-Item -ItemType Directory -Path $qa | Out-Null
$userChoiceKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\UserChoice'
$userChoiceBefore = Get-ItemProperty -LiteralPath $userChoiceKey -ErrorAction SilentlyContinue | Select-Object ProgId,Hash | ConvertTo-Json -Compress
foreach ($line in Get-Content (Join-Path $output 'SHA256SUMS.txt')) {
    if ($line -notmatch '^([A-Fa-f0-9]{64})  (.+)$') { throw 'Invalid checksum line.' }
    if ((Get-FileHash -LiteralPath (Join-Path $output $Matches[2]) -Algorithm SHA256).Hash -ne $Matches[1]) { throw 'Artifact checksum mismatch.' }
}
$portable = Join-Path $qa 'Portable con espacios'
Expand-Archive -LiteralPath (Join-Path $output "PDF-Editor-$($release.version)-windows-x64-portable.zip") -DestinationPath $portable
$portableApp = Join-Path $portable 'PDF Editor'
$manifest = Get-Content (Join-Path $portableApp 'manifest-sha256.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
    if ((Get-FileHash -LiteralPath (Join-Path $portableApp $entry.File) -Algorithm SHA256).Hash -ne $entry.SHA256) { throw "Runtime checksum mismatch: $($entry.File)" }
}
if (Get-ChildItem -LiteralPath $portableApp -Recurse -File | Where-Object {$_.Name -match '_tests\.exe$|Qt6Test|\.pdb$'}) { throw 'Development files were packaged.' }

# A synthetic PDF, generated with byte offsets so no external PDF library is needed.
$pdfPath = Join-Path $qa 'Documento sintético con espacios.pdf'
$objects = @('<< /Type /Catalog /Pages 2 0 R >>', '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>',
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
$stream = "BT /F1 24 Tf 50 750 Td (PDF Editor - Distribution QA) Tj ET`n"
$objects += "<< /Length $($stream.Length) >>`nstream`n${stream}endstream"
$pdf = "%PDF-1.4`n"
$offsets = @()
for ($i = 0; $i -lt $objects.Count; $i++) { $offsets += $pdf.Length; $pdf += "$($i+1) 0 obj`n$($objects[$i])`nendobj`n" }
$xref = $pdf.Length
$pdf += "xref`n0 6`n0000000000 65535 f `n"
foreach ($offset in $offsets) { $pdf += ('{0:0000000000} 00000 n ' -f $offset) + "`n" }
$pdf += "trailer`n<< /Size 6 /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
[IO.File]::WriteAllText($pdfPath, $pdf, [Text.Encoding]::ASCII)
$originalHash = (Get-FileHash -LiteralPath $pdfPath).Hash
$previousPath = $env:PATH
$previousPlatform = $env:QT_QPA_PLATFORM
$previousPluginPath = $env:QT_PLUGIN_PATH
$previousQpaPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
$launches = [Collections.Generic.List[object]]::new()

function Test-App([string]$AppDirectory, [string]$Name) {
    $profile = Join-Path $qa "$Name profile"
    $start = [Diagnostics.ProcessStartInfo]::new((Join-Path $AppDirectory 'pdf_editor.exe'))
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $qa
    foreach ($argument in @('--profile', $profile, $pdfPath)) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            Start-Sleep -Milliseconds 200
            $process.Refresh()
            if ($process.HasExited) { throw "$Name exited before opening the PDF: $($process.ExitCode)" }
        } until ($process.MainWindowTitle.Contains([IO.Path]::GetFileName($pdfPath)) -or [DateTime]::UtcNow -gt $deadline)
        if (-not $process.MainWindowTitle.Contains([IO.Path]::GetFileName($pdfPath))) { throw "$Name did not open the PDF: $($process.MainWindowTitle)" }
        Start-Sleep -Seconds 2
        $modules = @($process.Modules | ForEach-Object { $_.FileName })
        $foreign = @($modules | Where-Object { -not $_.StartsWith($AppDirectory + '\', [StringComparison]::OrdinalIgnoreCase) -and -not $_.StartsWith($env:SystemRoot + '\', [StringComparison]::OrdinalIgnoreCase) })
        if ($foreign) { throw "Unexpected DLL location: $($foreign -join ', ')" }
        $modules | ConvertTo-Json | Set-Content (Join-Path $qa "$Name-modules.json")
        $title = $process.MainWindowTitle
        if (-not $process.CloseMainWindow() -or -not $process.WaitForExit(20000)) { throw "$Name did not close cleanly." }
        if ($process.ExitCode -ne 0) { throw "$Name exit code: $($process.ExitCode)" }
        $session = Get-Content (Join-Path $profile 'session.json') -Raw | ConvertFrom-Json
        $recentPaths = @($session.recentFiles | ForEach-Object { [IO.Path]::GetFullPath($_) })
        if (-not $session.cleanExit -or $recentPaths -notcontains [IO.Path]::GetFullPath($pdfPath)) { throw "$Name session was not saved correctly." }
        $launches.Add([pscustomobject]@{ Name=$Name; Title=$title; ExitCode=$process.ExitCode; Modules=$modules.Count; IsolatedProfile=$profile })
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}

$installed = Join-Path $qa 'Installed con espacios'
$installer = Join-Path $output "PDF-Editor-$($release.version)-windows-x64-setup.exe"
$group = 'PDF Editor QA ' + [IO.Path]::GetFileName($qa)
function Invoke-Setup([string]$Label, [string]$Tasks) {
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER',"/DIR=`"$installed`"", "/GROUP=`"$group`"", "/TASKS=`"$Tasks`"", "/LOG=`"$(Join-Path $qa "$Label-install.txt")`"")
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) { throw "Installer failed: $($process.ExitCode)" }
    if (-not (Test-Path -LiteralPath $uninstallKey)) { throw 'Uninstall registration missing.' }
    foreach ($entry in $manifest) {
        if ((Get-FileHash -LiteralPath (Join-Path $installed $entry.File) -Algorithm SHA256).Hash -ne $entry.SHA256) { throw "Installed file checksum mismatch: $($entry.File)" }
    }
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_PLUGIN_PATH = $null
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    Test-App $portableApp 'portable'
    Invoke-Setup 'first' ''
    Test-App $installed 'installed'
    $sentinel = Join-Path $qa 'installed profile/preserve-user-data.txt'
    'Preserve this user file across reinstall and uninstall.' | Set-Content $sentinel
    $sessionBefore = (Get-FileHash -LiteralPath (Join-Path $qa 'installed profile/session.json')).Hash
    Invoke-Setup 'reinstall' 'openwith'
    if (-not (Test-Path $sentinel)) { throw 'Reinstall removed user data.' }
    $command = (Get-Item -LiteralPath "$progId\shell\open\command").GetValue('')
    if ($command -ne "`"$installed\pdf_editor.exe`" `"%1`"") { throw 'Invalid Open with command.' }
    if ((Get-Item -LiteralPath $openWith).GetValueNames() -notcontains 'PDFEditor.Document') { throw 'Open with registration missing.' }
    Test-App $installed 'reinstalled'
    $uninstaller = Join-Path $installed 'unins000.exe'
    $process = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$(Join-Path $qa 'uninstall.txt')`"" -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) { throw "Uninstall failed: $($process.ExitCode)" }
    if ((Test-Path (Join-Path $installed 'pdf_editor.exe')) -or (Test-Path -LiteralPath $uninstallKey) -or (Test-Path -LiteralPath $progId)) { throw 'Uninstall left product files/registration.' }
    if ((Get-OpenWithNames) -contains 'PDFEditor.Document') { throw 'Uninstall left Open with registration.' }
    if (-not (Test-Path -LiteralPath $sentinel) -or (Get-FileHash -LiteralPath $pdfPath).Hash -ne $originalHash -or (Get-FileHash -LiteralPath (Join-Path $qa 'installed profile/session.json')).Hash -ne $sessionBefore) { throw 'User data changed.' }
    $groupPath = Join-Path ([Environment]::GetFolderPath('Programs')) $group
    if (Test-Path -LiteralPath $groupPath) { throw 'QA Start menu group was not removed.' }
    $userChoiceAfter = Get-ItemProperty -LiteralPath $userChoiceKey -ErrorAction SilentlyContinue | Select-Object ProgId,Hash | ConvertTo-Json -Compress
    if ($userChoiceBefore -cne $userChoiceAfter) { throw 'Default PDF application changed.' }
    [pscustomobject]@{ Correcto=$true; Version=$release.version; ManifestFiles=$manifest.Count; Launches=$launches; Install=$true; Reinstall=$true; Uninstall=$true; OpenWith=$true; DefaultPdfPreserved=$true; UserDataPreserved=$true; Windows=[Environment]::OSVersion.VersionString; VerifiedUtc=[DateTime]::UtcNow.ToString('o') } |
        ConvertTo-Json -Depth 5 | Set-Content (Join-Path $qa 'resultado.json')
    Get-Content (Join-Path $qa 'resultado.json')
} finally {
    $env:PATH=$previousPath; $env:QT_QPA_PLATFORM=$previousPlatform
    $env:QT_PLUGIN_PATH=$previousPluginPath; $env:QT_QPA_PLATFORM_PLUGIN_PATH=$previousQpaPath
    if (Test-Path -LiteralPath $uninstallKey) {
        Write-Warning "QA installation remains at $installed. See logs before uninstalling; do not remove a real user installation."
    }
}
