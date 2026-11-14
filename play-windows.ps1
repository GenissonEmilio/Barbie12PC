param([Parameter(Mandatory = $true)][string]$Prepared)

$ErrorActionPreference = 'Stop'
$Prepared = (Resolve-Path -LiteralPath $Prepared).Path
$Image = Get-Content -LiteralPath (Join-Path $Prepared 'disc-image.txt') -Encoding utf8 -TotalCount 1
Push-Location -LiteralPath $Prepared
try {
    & (Join-Path $Prepared 'Barbie12PC.exe') (Join-Path $Prepared 'disc/SLUS_215.79') $Image
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
