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

Export-ModuleMember -Function ConvertTo-EspAppVersion
