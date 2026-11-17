$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'

if (Test-Path $dist) {
    Remove-Item $dist -Recurse -Force
}
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $root 'index.html') $dist -Force
Copy-Item (Join-Path $root 'debug.html') $dist -Force
New-Item -ItemType Directory -Force (Join-Path $dist 'webapp') | Out-Null
Copy-Item (Join-Path $root 'webapp\\*') (Join-Path $dist 'webapp') -Recurse -Force

Write-Output 'Synchronized browser assets to dist.'
