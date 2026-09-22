[CmdletBinding()]
param(
    [string]$RepoRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    $RepoRoot = (Resolve-Path (Join-Path $scriptDirectory "..")).Path
}

$repoRootFull = [IO.Path]::GetFullPath($RepoRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
$failures = New-Object System.Collections.Generic.List[string]

function Add-DocumentationFailure {
    param(
        [string]$RelativePath,
        [int]$LineNumber,
        [string]$Message
    )

    $location = if ($LineNumber -gt 0) { "${RelativePath}:${LineNumber}" } else { $RelativePath }
    $failures.Add("$location - $Message")
}

function Get-DocumentationFiles {
    # Include untracked, non-ignored files so the local pre-commit check also
    # validates a newly created document before it is staged.
    $paths = @(& git -C $repoRootFull ls-files --cached --others --exclude-standard -- "*.md" "*.html")
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed"
    }

    return @($paths | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        (Test-Path -LiteralPath (Join-Path $repoRootFull $_))
    })
}

function Test-LocalReference {
    param(
        [string]$SourceRelativePath,
        [int]$LineNumber,
        [string]$RawTarget
    )

    $target = $RawTarget.Trim().Trim('<', '>')
    if ([string]::IsNullOrWhiteSpace($target) -or
        $target.StartsWith('#') -or
        $target.StartsWith('//') -or
        $target -match '^[A-Za-z][A-Za-z0-9+.-]*:') {
        return
    }

    $pathPart = ($target -split '[?#]', 2)[0]
    if ([string]::IsNullOrWhiteSpace($pathPart)) {
        return
    }

    try {
        $decoded = [Uri]::UnescapeDataString($pathPart)
        if ($decoded.StartsWith('/')) {
            $candidate = Join-Path $repoRootFull $decoded.TrimStart('/')
        } else {
            $sourceDirectory = Split-Path -Parent (Join-Path $repoRootFull $SourceRelativePath)
            $candidate = Join-Path $sourceDirectory $decoded
        }
        $resolved = [IO.Path]::GetFullPath($candidate)
    } catch {
        Add-DocumentationFailure $SourceRelativePath $LineNumber "invalid local reference '$target'"
        return
    }

    if (-not (Test-Path -LiteralPath $resolved)) {
        Add-DocumentationFailure $SourceRelativePath $LineNumber "missing local target '$target'"
        return
    }

    $rootPrefix = $repoRootFull + [IO.Path]::DirectorySeparatorChar
    if ($resolved -ne $repoRootFull -and
        -not $resolved.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Add-DocumentationFailure $SourceRelativePath $LineNumber "local reference escapes the repository: '$target'"
    }
}

$documentationFiles = @(Get-DocumentationFiles)
$contents = @{}
$markdownLinkPattern = '!?' + '\[[^\]]*\]\((?<target><[^>]+>|[^)\s]+)(?:\s+["''][^"'']*["''])?\)'
$htmlReferencePattern = '(?:src|href)\s*=\s*["''](?<target>[^"'']+)["'']'

foreach ($relativePath in $documentationFiles) {
    $fullPath = Join-Path $repoRootFull $relativePath
    $lines = @(Get-Content -LiteralPath $fullPath)
    $contents[$relativePath] = $lines

    for ($index = 0; $index -lt $lines.Count; $index++) {
        $lineNumber = $index + 1
        foreach ($match in [regex]::Matches($lines[$index], $markdownLinkPattern)) {
            Test-LocalReference $relativePath $lineNumber $match.Groups['target'].Value
        }

        if ($relativePath.EndsWith('.html', [StringComparison]::OrdinalIgnoreCase)) {
            foreach ($match in [regex]::Matches($lines[$index], $htmlReferencePattern)) {
                Test-LocalReference $relativePath $lineNumber $match.Groups['target'].Value
            }
        }
    }
}

# A normal broken-link pass catches Markdown/HTML links, but obsolete plans were
# also often named in prose or code spans. Use Git history as the retired-path
# inventory and reject references to documentation that no longer exists.
$deletedPaths = @(& git -C $repoRootFull log --all --diff-filter=D --format= --name-only -- "*.md" "*.html")
if ($LASTEXITCODE -ne 0) {
    throw "git log failed while collecting removed documentation paths"
}

$currentBasenames = @{}
foreach ($relativePath in $documentationFiles) {
    $currentBasenames[[IO.Path]::GetFileName($relativePath).ToLowerInvariant()] = $true
}

$retiredPaths = @($deletedPaths |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
    ForEach-Object { $_.Trim() -replace '\\', '/' } |
    Sort-Object -Unique |
    Where-Object { -not (Test-Path -LiteralPath (Join-Path $repoRootFull $_)) })

foreach ($retiredPath in $retiredPaths) {
    $retiredBackslashPath = $retiredPath -replace '/', '\\'
    $basename = [IO.Path]::GetFileName($retiredPath)
    $basenameIsUniqueToHistory = -not $currentBasenames.ContainsKey($basename.ToLowerInvariant())

    foreach ($relativePath in $documentationFiles) {
        $lines = $contents[$relativePath]
        for ($index = 0; $index -lt $lines.Count; $index++) {
            $line = $lines[$index]
            $mentionsPath = $line.IndexOf($retiredPath, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
                $line.IndexOf($retiredBackslashPath, [StringComparison]::OrdinalIgnoreCase) -ge 0
            $mentionsUniqueBasename = $basenameIsUniqueToHistory -and
                $line.IndexOf($basename, [StringComparison]::OrdinalIgnoreCase) -ge 0

            if ($mentionsPath -or $mentionsUniqueBasename) {
                Add-DocumentationFailure $relativePath ($index + 1) "references removed documentation '$retiredPath'"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Documentation integrity check failed with $($failures.Count) issue(s):"
    $failures | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host "Documentation integrity check passed."
Write-Host "  Markdown/HTML files: $($documentationFiles.Count)"
Write-Host "  retired documentation paths checked: $($retiredPaths.Count)"
