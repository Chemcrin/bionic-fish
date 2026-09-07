$ErrorActionPreference = "Stop"
$PackageDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Set-Location $PackageDir
py -m pip install -r requirements-windows.txt
py generate_whale_geometry.py --output output
py solidworks_2025_build.py --package-dir $PackageDir

Write-Host "Completed. Native files are in: $PackageDir\native"
