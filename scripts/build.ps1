param([string]$QtRoot = 'C:\Qt\6.10.1\mingw_64', [switch]$Deploy)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$env:Path = "$QtRoot\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:Path"
Push-Location $projectRoot
try {
    cmake --preset windows-local "-DCMAKE_PREFIX_PATH=$QtRoot"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    cmake --build --preset windows-local
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    # This workstation's Qt install omits the ImageFormats add-on used by mod icons.
    # Keep the official decoder local to this project; do not modify the Qt installation.
    $webpPlugin = Join-Path $QtRoot 'plugins\imageformats\qwebp.dll'
    if (!(Test-Path -LiteralPath $webpPlugin)) {
        if ((& "$QtRoot\bin\qmake.exe" -query QT_VERSION) -ne '6.10.1') {
            throw 'Install the Qt ImageFormats add-on for your Qt version (WebP support).'
        }
        $moduleRoot = Join-Path $projectRoot 'build\qt-imageformats'
        $archive = Join-Path $moduleRoot 'imageformats.7z'
        New-Item -ItemType Directory -Path $moduleRoot -Force | Out-Null
        $expectedHash = '7E1144F21C5A30E5C26BCC9355F890E744366C9823E65E10AC3B238E71F75D24'
        if (!(Test-Path -LiteralPath $archive)) {
            $url = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6101/qt6_6101/qt.qt6.6101.addons.qtimageformats.win64_mingw/6.10.1-0-202511161843qtimageformats-Windows-Windows_11_24H2-Mingw-Windows-Windows_11_24H2-X86_64.7z'
            Invoke-WebRequest -Uri $url -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Qt ImageFormats archive checksum did not match.' }
        Push-Location $moduleRoot
        try {
            cmake -E tar xf $archive 'plugins/imageformats/qwebp.dll' 'sbom/qtimageformats-6.10.1.spdx.json'
            if ($LASTEXITCODE -ne 0) { throw 'Could not extract the Qt WebP decoder.' }
        } finally { Pop-Location }
        $webpPlugin = Join-Path $moduleRoot 'plugins\imageformats\qwebp.dll'
    }
    New-Item -ItemType Directory -Path (Join-Path $projectRoot 'build\imageformats') -Force | Out-Null
    Copy-Item -LiteralPath $webpPlugin -Destination (Join-Path $projectRoot 'build\imageformats\qwebp.dll') -Force
    ctest --preset windows-local
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    if ($Deploy) {
        $destination = Join-Path $projectRoot 'dist'
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $projectRoot 'build\modpack-studio.exe') -Destination $destination -Force
        & "$QtRoot\bin\windeployqt.exe" --release --no-translations --qmldir (Join-Path $projectRoot 'qml') (Join-Path $destination 'modpack-studio.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
        New-Item -ItemType Directory -Path (Join-Path $destination 'imageformats') -Force | Out-Null
        Copy-Item -LiteralPath $webpPlugin -Destination (Join-Path $destination 'imageformats\qwebp.dll') -Force
        # The packaged smoke test renders offscreen; windeployqt otherwise copies only qwindows.
        Copy-Item -LiteralPath (Join-Path $QtRoot 'plugins\platforms\qoffscreen.dll') -Destination (Join-Path $destination 'platforms\qoffscreen.dll') -Force
    }
} finally { Pop-Location }
