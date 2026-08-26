function ConvertTo-EspAppVersion {
    param(
        [Parameter(Mandatory = $true)][string]$Version
    )

    if ([string]::IsNullOrEmpty($Version)) {
        throw "ESP application version must not be empty"
    }

    # esp_app_desc_t.version is a 32-byte NUL-terminated field. Trim by UTF-8
    # bytes, never by UTF-16 code units, so the signed manifest contains the
    # exact valid text that can fit in the image descriptor.
    $utf8 = [System.Text.UTF8Encoding]::new($false, $true)
    $candidate = $Version
    while ($candidate.Length -gt 0) {
        try {
            if ($utf8.GetByteCount($candidate) -le 31) {
                return $candidate
            }
        } catch [System.Text.EncoderFallbackException] {
            # Removing one UTF-16 code unit may temporarily leave a high
            # surrogate. Continue trimming until a valid boundary is reached.
        }
        $candidate = $candidate.Substring(0, $candidate.Length - 1)
    }

    throw "ESP application version has no valid UTF-8 prefix within 31 bytes"
}

function Resolve-OtaSigningPython {
    $candidates = @()
    if ($env:PAJONIIIR_OTA_PYTHON) {
        $candidates += $env:PAJONIIIR_OTA_PYTHON
    }
    if ($env:IDF_PYTHON_ENV_PATH) {
        $relativeExe = if ($env:OS -eq "Windows_NT") {
            "Scripts\python.exe"
        } else {
            "bin/python"
        }
        $candidates += Join-Path $env:IDF_PYTHON_ENV_PATH $relativeExe
    }
    if ($env:OS -eq "Windows_NT") {
        $candidates += "C:\Espressif\tools\python\v6.0.2\venv\Scripts\python.exe"
        $candidates += "C:\Espressif\python_env\idf6.0_py3.13_env\Scripts\python.exe"
        $candidates += "C:\Espressif\python_env\idf6.0_py3.11_env\Scripts\python.exe"
    }
    foreach ($name in @("python", "python3")) {
        foreach ($command in @(Get-Command $name -All -ErrorAction SilentlyContinue)) {
            if ($command.Source) { $candidates += $command.Source }
        }
    }

    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        if (-not $candidate -or -not (Test-Path -LiteralPath $candidate)) { continue }
        $previousErrorAction = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $candidate -c "import cryptography" 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) { return $candidate }
        } catch {
            # Try the next interpreter.
        } finally {
            $ErrorActionPreference = $previousErrorAction
        }
    }
    throw "No Python interpreter with a working cryptography module was found"
}

Export-ModuleMember -Function ConvertTo-EspAppVersion, Resolve-OtaSigningPython
