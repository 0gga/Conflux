$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$deployed = Join-Path $projectRoot 'dist\modpack-studio.exe'
if (Test-Path -LiteralPath $deployed) {
    Start-Process -FilePath $deployed -WorkingDirectory (Split-Path -Parent $deployed)
} else {
    $env:Path = "C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:Path"
    $executable = Join-Path $projectRoot 'build\modpack-studio.exe'
    if (!(Test-Path -LiteralPath $executable)) { throw 'Run scripts/build.ps1 first.' }
    Start-Process -FilePath $executable -WorkingDirectory $projectRoot
}
