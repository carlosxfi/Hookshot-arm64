# Adds only the controller patch/launcher to an extracted daurosk test10 Windows kit.
param([Parameter(Mandatory=$true)][string]$Kit)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$Kit = (Resolve-Path -LiteralPath $Kit).Path
$pinFile = Join-Path $Kit 'hookshot\PIN'
if (-not (Test-Path -LiteralPath $pinFile -PathType Leaf)) { throw 'Choose the extracted co-op kit folder containing hookshot\PIN.' }
$pin = Get-Content -LiteralPath $pinFile -Raw
if ($pin -notmatch '(?m)^commit=4aee23659b9436dab2cd7be1ac07333c06246b7c\s*$') {
    throw 'This patch is for test10 (Hookshot v2.2.0-rc.1). The kit has a different Hookshot pin.'
}
foreach ($name in 'Build-Hookshot-Coop.bat','Host-Hookshot.bat','Join-Hookshot.bat') {
    if (-not (Test-Path -LiteralPath (Join-Path $Kit $name) -PathType Leaf)) { throw "Missing $name in this kit." }
}
$patchDir = Join-Path $Kit 'hookshot\patches'
$expected = @('0001-coop-bridge.patch','0002-coop-puppets.patch','0003-coop-platforms.patch','0004-controller-selection.patch')
foreach ($name in $expected[0..2]) {
    if (-not (Test-Path -LiteralPath (Join-Path $patchDir $name) -PathType Leaf)) { throw "Missing test10 patch $name" }
}
foreach ($file in Get-ChildItem -LiteralPath $patchDir -Filter '*.patch' -File) {
    if ($file.Name -notin $expected) { throw "Unexpected patch $($file.Name); use an unmodified test10 kit." }
}
$originalHashes = @(
    'e066a1982b89c66086714d014b0ccd96076d41d859409ede42296a2c51cc7c11',
    'f8a892d916b40d1bc4084871ef19839edd9c60a7f9e435b4157dfc7f1e414c2f',
    '43e2a42136da56fb3eabdb5b26a62f713476bc6ebb32c6cedef2e1e7535c8468'
)
for ($i=0; $i -lt 3; $i++) {
    $text = (Get-Content -LiteralPath (Join-Path $patchDir $expected[$i]) -Raw -Encoding UTF8).Replace("`r`n","`n").TrimEnd() + "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text)))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($hash -ne $originalHashes[$i]) { throw "Test10 patch $($expected[$i]) differs; use the original test10 kit." }
}
$payloads = @(
    @{ Name='0004-controller-selection.patch'; Hash='14572d08bae266cd386f0b6c3b945bc7b25e41beb702441ec91a3710e4b25e5f'; Destination=(Join-Path $patchDir '0004-controller-selection.patch') },
    @{ Name='Launch-Hookshot-Controller.ps1'; Hash='0d278a9328f18e0044a198778f76da5164f8e552718f7ab1aaf9d25a51da4c58'; Destination=(Join-Path $Kit 'Launch-Hookshot-Controller.ps1') }
)
$base = 'https://raw.githubusercontent.com/carlosxfi/Hookshot-arm64/coop-controller-selection/scripts/coop/'
$staging = Join-Path ([IO.Path]::GetTempPath()) ('hookshot-controllers-' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $staging)
try {
    # Validate every payload and existing destination before publishing either file.
    foreach ($item in $payloads) {
        $temp = Join-Path $staging $item.Name
        $local = Join-Path $PSScriptRoot $item.Name
        if (Test-Path -LiteralPath $local -PathType Leaf) { Copy-Item -LiteralPath $local -Destination $temp }
        else { Invoke-WebRequest -UseBasicParsing -Uri ($base + $item.Name) -OutFile $temp }
        if ((Get-FileHash -LiteralPath $temp -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.Hash) {
            throw "Checksum mismatch for $($item.Name). Nothing installed."
        }
        if (Test-Path -LiteralPath $item.Destination) {
            if ((Get-FileHash -LiteralPath $item.Destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.Hash) {
                throw "A different file already exists at $($item.Destination). Nothing installed."
            }
        }
    }
    foreach ($item in $payloads) {
        if (-not (Test-Path -LiteralPath $item.Destination)) {
            Copy-Item -LiteralPath (Join-Path $staging $item.Name) -Destination $item.Destination -ErrorAction Stop
        }
    }
} finally {
    # Only the unique temporary directory created above is removed.
    Remove-Item -LiteralPath $staging -Recurse -Force
}
Write-Host 'Controller patch installed. Now run Build-Hookshot-Coop.bat again.' -ForegroundColor Green
Write-Host 'After building, use a separate kit/game folder for each player.'
Write-Host '.\Launch-Hookshot-Controller.ps1 -Controller 1 -Role Host'
Write-Host '.\Launch-Hookshot-Controller.ps1 -Controller 2 -Role Join'
