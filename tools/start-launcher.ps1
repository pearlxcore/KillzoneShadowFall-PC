$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$candidates = @(
    (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\pythonw.exe'),
    'C:\msys64\clang64\bin\pythonw.exe'
)
$python = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$python) { throw 'Python with Tkinter is required. No supported Python installation was found.' }
Start-Process -FilePath $python -ArgumentList @('"' + (Join-Path $projectRoot 'launcher\kzsf_local.py') + '"') -WorkingDirectory $projectRoot -WindowStyle Hidden
