param(
    [string]$QtRoot = 'C:\Qt\6.10.1\mingw_64',
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$ReportName = 'current',
    [string]$Filter = 'src/',
    [string]$TestRegex = '',
    [double]$FailUnderLine = 0,
    [switch]$IncludeSmokeTest,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'artifacts\coverage-build'
$toolsRoot = Join-Path $projectRoot 'artifacts\coverage-tools'
$reportRoot = Join-Path $projectRoot ('artifacts\coverage\' + $ReportName)
$env:Path = "$QtRoot\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:Path"
$env:PYTHONPATH = $toolsRoot + [IO.Path]::PathSeparator + $env:PYTHONPATH
Push-Location $projectRoot
try {
    if (!(Test-Path -LiteralPath (Join-Path $toolsRoot 'gcovr\__init__.py'))) {
        python -m pip install --disable-pip-version-check --target $toolsRoot --requirement (Join-Path $PSScriptRoot 'coverage-requirements.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Could not install isolated coverage tools.' }
    }
    if (!$SkipBuild) {
        cmake --preset windows-coverage "-DCMAKE_PREFIX_PATH=$QtRoot"
        if ($LASTEXITCODE -ne 0) { throw 'Coverage configuration failed.' }
        cmake --build --preset windows-coverage
        if ($LASTEXITCODE -ne 0) { throw 'Coverage build failed.' }
    }
    $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
    $expectedBuild = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts\coverage-build'))
    if ($resolvedBuild -ne $expectedBuild) { throw 'Unexpected coverage build directory.' }
    # Reset generated counters only, so every report represents this run.
    Get-ChildItem -LiteralPath $resolvedBuild -Filter '*.gcda' -File -Recurse | ForEach-Object {
        if (!$_.FullName.StartsWith($resolvedBuild + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Counter escaped the build directory.' }
        Remove-Item -LiteralPath $_.FullName
    }
    $testArguments = @('--preset', 'windows-coverage', '--output-on-failure')
    if ($TestRegex) { $testArguments += @('-R', $TestRegex) }
    & ctest @testArguments
    $testExit = $LASTEXITCODE
    $smokeExit = 0
    if ($IncludeSmokeTest) {
        $webpPlugin = Join-Path $QtRoot 'plugins\imageformats\qwebp.dll'
        if (!(Test-Path -LiteralPath $webpPlugin)) { $webpPlugin = Join-Path $projectRoot 'build\imageformats\qwebp.dll' }
        if (!(Test-Path -LiteralPath $webpPlugin)) { throw 'The smoke needs Qt ImageFormats; install it or run scripts/build.ps1 once to obtain the verified decoder.' }
        New-Item -ItemType Directory -Path (Join-Path $resolvedBuild 'imageformats') -Force | Out-Null
        Copy-Item -LiteralPath $webpPlugin -Destination (Join-Path $resolvedBuild 'imageformats\qwebp.dll') -Force
        $smokeRoot = Join-Path $reportRoot ('smoke-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff'))
        New-Item -ItemType Directory -Path $smokeRoot -Force | Out-Null
        $previousPlatform = $env:QT_QPA_PLATFORM
        $previousBackend = $env:QT_QUICK_BACKEND
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_QUICK_BACKEND = 'software'
        try {
            $smoke = Start-Process -FilePath (Join-Path $resolvedBuild 'modpack-studio.exe') -ArgumentList @('--smoke-test', ('"' + $smokeRoot + '"')) -WorkingDirectory $resolvedBuild -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $smokeRoot 'stdout.log') -RedirectStandardError (Join-Path $smokeRoot 'stderr.log')
            $deadline = [DateTime]::UtcNow.AddMinutes(5)
            while (!$smoke.WaitForExit(1000)) {
                if ([DateTime]::UtcNow -gt $deadline) {
                    if ($smoke.Path -eq (Join-Path $resolvedBuild 'modpack-studio.exe')) { Stop-Process -Id $smoke.Id }
                    throw 'Test-owned offscreen coverage smoke exceeded five minutes.'
                }
            }
            $smokeExit = $smoke.ExitCode
        } finally {
            $env:QT_QPA_PLATFORM = $previousPlatform
            $env:QT_QUICK_BACKEND = $previousBackend
        }
    }
    New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
    $coverageArguments = @('-m', 'gcovr', '--root', $projectRoot, '--filter', $Filter,
        '--gcov-executable', 'C:/Qt/Tools/mingw1310_64/bin/gcov.exe',
        '--exclude-unreachable-branches', '--exclude-throw-branches',
        '--html-details', (Join-Path $reportRoot 'index.html'),
        '--json', (Join-Path $reportRoot 'coverage.json'),
        '--json-summary', (Join-Path $reportRoot 'summary.json'),
        '--txt', (Join-Path $reportRoot 'summary.txt'),
        '--xml', (Join-Path $reportRoot 'cobertura.xml'), '--print-summary',
        '--fail-under-line', [string]$FailUnderLine, $resolvedBuild)
    & python @coverageArguments
    $coverageExit = $LASTEXITCODE
    if ($testExit -ne 0) { throw "Coverage tests failed; reports remain in $reportRoot." }
    if ($smokeExit -ne 0) { throw "Coverage smoke failed; reports remain in $reportRoot." }
    if ($coverageExit -ne 0) { throw "Coverage report or threshold failed; reports remain in $reportRoot." }
    Write-Output "Coverage report: $reportRoot\index.html"
} finally { Pop-Location }
