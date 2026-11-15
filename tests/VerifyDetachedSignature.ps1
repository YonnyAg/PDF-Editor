param([Parameter(Mandatory=$true)][string]$Pdf)
$ErrorActionPreference = 'Stop'
try {
    Add-Type -AssemblyName System.Security
    $bytes = [IO.File]::ReadAllBytes($Pdf)
    $text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $match = [regex]::Match($text, '/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*\]')
    if (!$match.Success) { throw 'No byte ranges' }
    $range = @(0..3 | ForEach-Object { [int]$match.Groups[$_+1].Value })
    if ($range[0] -ne 0 -or $range[2] + $range[3] -ne $bytes.Length -or $text[$range[1]] -ne '<' -or $text[$range[2]-1] -ne '>') { throw 'Invalid byte coverage' }
    $content = New-Object IO.MemoryStream
    $content.Write($bytes, $range[0], $range[1])
    $content.Write($bytes, $range[2], $range[3])
    $hex = $text.Substring($range[1]+1, $range[2]-$range[1]-2)
    $signature = New-Object byte[] ($hex.Length/2)
    for ($i=0; $i -lt $signature.Length; $i++) { $signature[$i] = [Convert]::ToByte($hex.Substring(2*$i,2),16) }
    $info = New-Object Security.Cryptography.Pkcs.ContentInfo -ArgumentList (,$content.ToArray())
    $cms = New-Object Security.Cryptography.Pkcs.SignedCms -ArgumentList $info,$true
    $cms.Decode($signature)
    $cms.CheckSignature($true)
    if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].DigestAlgorithm.Value -ne '2.16.840.1.101.3.4.2.1') { throw 'Expected one SHA-256 signer' }
    Write-Output 'Windows SignedCms: detached SHA-256 signature verified'
    exit 0
} catch { Write-Error $_; exit 1 }
