$ErrorActionPreference = 'Stop'
New-Item dist/licenses -ItemType Directory -Force | Out-Null
$modules = go list -m -f '{{.Path}}|{{.Dir}}' all
if ($LASTEXITCODE) { throw 'Cannot enumerate module licenses' }
foreach ($entry in $modules) {
  $parts = $entry -split '\|', 2
  if ($parts.Count -lt 2 -or !$parts[1]) { continue }
  $safe = $parts[0] -replace '[^a-zA-Z0-9.-]', '_'
  Get-ChildItem -LiteralPath $parts[1] -File | Where-Object { $_.Name -match '^(LICENSE|NOTICE|COPYING|AUTHORS|PATENTS)' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination "dist/licenses/$safe-$($_.Name)"
  }
}
$goroot = go env GOROOT
Copy-Item "$goroot/LICENSE" dist/licenses/Go-LICENSE
