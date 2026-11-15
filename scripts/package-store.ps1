[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RuntimeDirectory,
    [string]$IdentityFile = (Join-Path $PSScriptRoot '../store/identity.json'),
    [string]$OutputDirectory,
    [string]$MakeAppx
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$identity = Get-Content -LiteralPath $IdentityFile -Raw | ConvertFrom-Json
foreach ($field in 'name','publisher','publisher_display_name','display_name','version') {
    if (-not $identity.$field -or $identity.$field -match 'COPIAR|PLACEHOLDER|PreviewOnly') { throw "Invalid Store identity: $field" }
}
if ($identity.name -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]{2,49}$' -or $identity.publisher -notmatch '^CN=.+') { throw 'Invalid Store package identity.' }
if ($identity.version -notmatch '^[1-9]\d*\.\d+\.\d+\.0$') { throw 'Store package version must have four parts, major >= 1 and revision = 0.' }
foreach ($part in $identity.version.Split('.')) { if ([int]$part -gt 65535) { throw 'Version component outside MSIX limits.' } }
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$manifest = @(Get-Content -LiteralPath (Join-Path $runtime 'manifest-sha256.json') -Raw | ConvertFrom-Json)
foreach ($entry in $manifest) {
    $file = [IO.Path]::GetFullPath((Join-Path $runtime $entry.File))
    if (-not $file.StartsWith($runtime + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe runtime manifest path.' }
    if ((Get-FileHash -LiteralPath $file).Hash -ne $entry.SHA256) { throw "Runtime hash mismatch: $($entry.File)" }
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root "build/store/$($identity.version)" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $output 'package'
if (Test-Path -LiteralPath $stage) { throw 'Store staging already exists. Choose a new output directory.' }
if (-not $MakeAppx) { $MakeAppx = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin/10.0.26100.0/x64/makeappx.exe' }
if (-not (Test-Path -LiteralPath $MakeAppx)) { throw 'MakeAppx not found. Install the Windows SDK or pass -MakeAppx.' }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -Path (Join-Path $runtime '*') -Destination $stage -Recurse
if (Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {$_.Name -match '_tests\.exe$|Qt6Test|\.pdb$|^unins.*\.exe$'}) { throw 'Development/installer files in MSIX staging.' }
$assets = Join-Path $stage 'Assets'
New-Item -ItemType Directory -Path $assets -Force | Out-Null
foreach ($item in @(@(44,'Square44x44Logo'),@(50,'StoreLogo'),@(150,'Square150x150Logo'),@(310,'Square310x310Logo'))) {
    Copy-Item -LiteralPath (Join-Path $root "store/assets/pdf-editor-$($item[0]).png") -Destination (Join-Path $assets "$($item[1]).png")
}
$template = Get-Content -LiteralPath (Join-Path $root 'store/AppxManifest.xml.in') -Raw
$values = @{ NAME=$identity.name; PUBLISHER=$identity.publisher; VERSION=$identity.version; DISPLAY_NAME=$identity.display_name; PUBLISHER_DISPLAY_NAME=$identity.publisher_display_name }
foreach ($key in $values.Keys) { $template = $template.Replace("@$key@", [Security.SecurityElement]::Escape($values[$key])) }
$template | Set-Content -LiteralPath (Join-Path $stage 'AppxManifest.xml') -Encoding utf8
[xml]$xml = $template
$package = Join-Path $output "PDF-Editor-Open-$($identity.version)-x64.msix"
& $MakeAppx pack /d $stage /p $package /h SHA256 /o *> (Join-Path $output 'makeappx.txt')
if ($LASTEXITCODE) { throw "MSIX validation failed; see $output/makeappx.txt" }
$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{File=[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/');SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash;Bytes=$_.Length}
})
$files | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $output 'package-files-sha256.json') -Encoding utf8
[pscustomobject]@{Package=$package;Name=$identity.name;Publisher=$identity.publisher;Version=$identity.version;StoreId=$identity.store_id;SHA256=(Get-FileHash -LiteralPath $package).Hash;Files=$files.Count;MakeAppxValidated=$true;Signed=$false;StoreSubmissionOnly=$true;InstallTested=$false} |
    ConvertTo-Json | Set-Content (Join-Path $output 'resultado.json') -Encoding utf8
Write-Output "Unsigned MSIX for Store submission: $package"
