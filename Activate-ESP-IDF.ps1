# Dot-source this script: . .\Activate-ESP-IDF.ps1
param(
    [string]$EspIdfPath = 'C:\esp\v6.1\esp-idf',
    [string]$EspToolsPath = 'C:\Espressif',
    [string]$EspPythonEnvPath = 'C:\Espressif\tools\python\v6.1\venv-win'
)

$espPython = Join-Path $EspPythonEnvPath 'Scripts\python.exe'
$espExport = Join-Path $EspIdfPath 'export.ps1'
if (!(Test-Path -LiteralPath $espPython) -or !(Test-Path -LiteralPath $espExport)) {
    throw 'ESP-IDF or its Windows Python environment was not found. Supply the installation paths to this script.'
}

$env:IDF_PATH = $EspIdfPath
$env:IDF_TOOLS_PATH = $EspToolsPath
$env:IDF_PYTHON_ENV_PATH = $EspPythonEnvPath
$env:PYTHONUTF8 = '1'
$env:Path = (Split-Path $espPython) + ';' + $env:Path
. $espExport
if (!$env:ESP_IDF_VERSION) {
    throw 'ESP-IDF activation failed. Check the messages above.'
}
