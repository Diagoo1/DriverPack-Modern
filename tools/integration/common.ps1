$ErrorActionPreference = 'Stop'
function Say([string]$Message){Write-Host ('[DriverPack Modern] '+$Message)}
function Require-Exit([string]$Operation){if($LASTEXITCODE -ne 0){throw "$Operation failed: $LASTEXITCODE"}}
