# One test10 co-op instance with its own controller. Run from a separate kit copy for each player.
param(
    [Parameter(Mandatory=$true)][ValidateRange(1,32)][int]$Controller,
    [Parameter(Mandatory=$true)][ValidateSet('Host','Join')][string]$Role,
    [string]$Kit = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$Kit = (Resolve-Path -LiteralPath $Kit).Path
$launch = Join-Path $Kit "$Role-Hookshot.bat"
if (-not (Test-Path -LiteralPath $launch -PathType Leaf)) { throw "No $Role-Hookshot.bat in $Kit" }
$oldController = $env:HOS_CONTROLLER
$oldBackground = $env:HOS_INPUT_BACKGROUND
try {
    $env:HOS_CONTROLLER = "$Controller"
    $env:HOS_INPUT_BACKGROUND = '1'
    Write-Host "Launching $Role with SDL controller $Controller. Connect every controller before starting either game."
    Push-Location -LiteralPath $Kit
    try { & $launch } finally { Pop-Location }
} finally {
    $env:HOS_CONTROLLER = $oldController
    $env:HOS_INPUT_BACKGROUND = $oldBackground
}
