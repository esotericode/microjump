$ErrorActionPreference = 'Stop'

# Configure the runner's installed MSVC toolchain without another setup action.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x64 toolchain not found' }
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$environment = & cmd /s /c "`"$devcmd`" -no_logo -arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        "$($Matches[1])=$($Matches[2])" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    }
}

$version = '1.4.341.0'
$sdk = Join-Path $env:RUNNER_TEMP "VulkanSDK\$version"
$installer = Join-Path $env:RUNNER_TEMP 'vulkan-sdk.exe'
$url = "https://sdk.lunarg.com/sdk/download/$version/windows/vulkansdk-windows-X64-$version.exe"
Invoke-WebRequest -Uri $url -OutFile $installer
$process = Start-Process -FilePath $installer -Wait -PassThru -ArgumentList @(
    '--root', $sdk, '--accept-licenses', '--default-answer', '--confirm-command', 'install'
)
if ($process.ExitCode -ne 0) { throw "Vulkan SDK install failed: $($process.ExitCode)" }
if (-not (Test-Path "$sdk\Include\vulkan\vulkan.h")) { throw 'Vulkan SDK headers not found' }
"VULKAN_SDK=$sdk" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
"$sdk\Bin" | Out-File -FilePath $env:GITHUB_PATH -Append -Encoding utf8
