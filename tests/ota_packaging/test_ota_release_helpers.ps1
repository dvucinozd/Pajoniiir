$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $RepoRoot "tools/OtaReleaseHelpers.psm1") -Force

function Assert-Equal {
    param([string]$Expected, [string]$Actual, [string]$Name)
    if ($Expected -cne $Actual) {
        throw "$Name expected '$Expected', got '$Actual'"
    }
}

Assert-Equal "RC2-test" (ConvertTo-EspAppVersion "RC2-test") "short version"
$ascii31 = "1234567890123456789012345678901"
Assert-Equal $ascii31 (ConvertTo-EspAppVersion $ascii31) "31-byte version"
Assert-Equal $ascii31 (ConvertTo-EspAppVersion ($ascii31 + "X")) "32-byte truncation"
$euro = [char]0x20AC
Assert-Equal ("a" * 29) (ConvertTo-EspAppVersion (("a" * 29) + $euro)) `
    "UTF-8 boundary truncation"
Assert-Equal (("a" * 28) + $euro) `
    (ConvertTo-EspAppVersion (("a" * 28) + $euro + "X")) "UTF-8 exact fit"

$emptyRejected = $false
try {
    ConvertTo-EspAppVersion "" | Out-Null
} catch {
    $emptyRejected = $true
}
if (-not $emptyRejected) {
    throw "empty version was accepted"
}

$Python = Resolve-OtaSigningPython
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("pajoniiir-ota-publish-test-" + [guid]::NewGuid().ToString("N"))
try {
    # Execute the actual packaging reader without running signing/publication.
    $tokens = $null; $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $RepoRoot 'tools/package_ota_release.ps1'), [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw 'Packaging script parse failure' }
    $reader = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Read-TargetBuild'
    }, $true)
    $definition = [scriptblock]::Create($reader.Extent.Text)
    $buildFixture = Join-Path $tempRoot 'firmware/main-deck-p4/build_test'
    New-Item -ItemType Directory -Path (Join-Path $buildFixture 'config') -Force | Out-Null
    '{"project_name":"main-deck-p4","app_bin":"missing.bin"}' |
        Set-Content -LiteralPath (Join-Path $buildFixture 'project_description.json')
    foreach ($flag in @('AUDIO_RECORDER_ENABLED','AUDIO_RECORDER_EXPERIMENTAL_BUILD','PAJONIIIR_SD_IDLE_WAIT')) {
        "#define CONFIG_$flag 1" | Set-Content -LiteralPath (Join-Path $buildFixture 'config/sdkconfig.h')
        $rejected = $false
        try {
            & { param($RepoRoot,$definition)
                $BuildName = 'build_test'; . $definition
                Read-TargetBuild -RelativeProjectDir 'firmware/main-deck-p4' -ExpectedProject 'main-deck-p4' -ExpectedChipId 0x12 -SlotSize 0x380000
            } $tempRoot $definition | Out-Null
        } catch {
            $rejected = $_.Exception.Message -match 'Storage experiment images cannot be packaged'
        }
        if (-not $rejected) { throw "Production packager accepted CONFIG_$flag" }
    }
    $releaseDir = Join-Path $tempRoot "directory-name-must-not-be-the-release"
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
    $privateKey = Join-Path $tempRoot "private.pem"
    $publicKey = Join-Path $tempRoot "public.der"
    $image = Join-Path $tempRoot "image.bin"
    $bundle = Join-Path $releaseDir "main-deck-p4.ddjota"
    [System.IO.File]::WriteAllBytes($image, [byte[]](1..24))

    & $Python (Join-Path $RepoRoot "tools/ota_signing.py") generate-key `
        --private $privateKey --public $publicKey
    if ($LASTEXITCODE -ne 0) { throw "test key generation failed" }
    & $Python (Join-Path $RepoRoot "tools/ota_signing.py") bundle `
        --private-key $privateKey --target p4 --chip-id 0x0012 `
        --project main-deck-p4 --version M2.1 `
        --input $image --output $bundle
    if ($LASTEXITCODE -ne 0) { throw "test bundle creation failed" }

    $publishOutput = & (Join-Path $RepoRoot "tools/publish_ota_release.ps1") `
        -ReleaseDir $releaseDir -PublicKey $publicKey -WriteToReleaseDir
    if (($publishOutput -join "`n") -notmatch `
            [regex]::Escape("https://ota.pajoniiir.eu/latest.json")) {
        throw "publisher did not use the canonical OTA base URL"
    }
    $latest = Get-Content -LiteralPath (Join-Path $releaseDir "latest.json") `
        -Raw | ConvertFrom-Json
    Assert-Equal "M2.1" ([string]$latest.release) `
        "publisher derives release from signed bundle"
    foreach ($project in @('main-deck-p4','main-deck-jc1060')) {
        $fixture = Join-Path $tempRoot "firmware/$project/build_test"
        New-Item -ItemType Directory -Path (Join-Path $fixture 'config') -Force | Out-Null
        $description = @{project_name=$project;project_version='M2.1';app_bin="$project.bin"}
        $description | ConvertTo-Json | Set-Content (Join-Path $fixture 'project_description.json')
        $config = Join-Path $fixture 'config/sdkconfig.h'
        Set-Content $config ''
        $binary = New-Object byte[] 256
        $binary[0]=0xe9; $binary[12]=0x12
        [Array]::Copy([BitConverter]::GetBytes([Convert]::ToUInt32('abcd5432',16)),0,$binary,32,4)
        [Array]::Copy([Text.Encoding]::UTF8.GetBytes('M2.1'),0,$binary,48,4)
        [Array]::Copy([Text.Encoding]::UTF8.GetBytes($project),0,$binary,80,$project.Length)
        $path = Join-Path $fixture "$project.bin"
        [IO.File]::WriteAllBytes($path,$binary)
        $readBuild = {
            param($RepoRoot,$definition,$project)
            $BuildName='build_test'; . $definition
            Read-TargetBuild -RelativeProjectDir "firmware/$project" -ExpectedProject $project -ExpectedChipId 0x12 -SlotSize 0x400000
        }
        $result = & $readBuild $tempRoot $definition $project
        Assert-Equal $project $result.Project 'ordinary project reader'
        foreach ($flag in @('PAJONIIIR_DJ_OVERVIEW','USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM')) {
            "#define CONFIG_$flag 1" | Set-Content $config
            $rejected=$false
            try { & $readBuild $tempRoot $definition $project | Out-Null }
            catch { $rejected=$_.Exception.Message -match 'Preview or DMA experiment' }
            if (-not $rejected) { throw "Ordinary candidate accepted $flag" }
        }
        Set-Content $config ''
        $binary[80]=[byte][char]'x'; [IO.File]::WriteAllBytes($path,$binary)
        $rejected=$false
        try { & $readBuild $tempRoot $definition $project | Out-Null }
        catch { $rejected=$_.Exception.Message -match 'Embedded app descriptor' }
        if (-not $rejected) { throw 'Packager accepted wrong embedded board' }
        $binary[80]=[byte][char]'m'; $binary[48]=[byte][char]'X'
        [IO.File]::WriteAllBytes($path,$binary)
        $rejected=$false
        try { & $readBuild $tempRoot $definition $project | Out-Null }
        catch { $rejected=$_.Exception.Message -match 'Embedded app descriptor' }
        if (-not $rejected) { throw 'Packager accepted wrong embedded version' }
        $large = New-Object byte[] 0x380001
        [Array]::Copy($binary,$large,$binary.Length); [IO.File]::WriteAllBytes($path,$large)
        $rejected=$false
        try { & $readBuild $tempRoot $definition $project | Out-Null }
        catch { $rejected=$_.Exception.Message -match '0x380000 build budget' }
        if (-not $rejected) { throw 'Packager accepted image above fixed budget' }
    }
    $jcBundle = Join-Path $releaseDir 'main-deck-jc1060.ddjota'
    & $Python (Join-Path $RepoRoot 'tools/ota_signing.py') bundle --private-key $privateKey --target p4 --chip-id 0x12 --project main-deck-jc1060 --version M2.1 --input $image --output $jcBundle
    if ($LASTEXITCODE -ne 0) { throw 'JC1060 test bundle failed' }
    $jcOutput = & (Join-Path $RepoRoot 'tools/publish_ota_release.ps1') -Project main-deck-jc1060 -ReleaseDir $releaseDir -PublicKey $publicKey -WriteToReleaseDir
    if (($jcOutput -join "`n") -notmatch [regex]::Escape('https://ota.pajoniiir.eu/jc1060/latest.json')) {
        throw 'JC1060 did not get a separate channel root'
    }
    $latest = Get-Content (Join-Path $releaseDir 'latest.json') -Raw | ConvertFrom-Json
    Assert-Equal 'M2.1/main-deck-jc1060.ddjota' $latest.p4.url 'JC1060 compatible channel schema'
    $rejected=$false
    try { & (Join-Path $RepoRoot 'tools/publish_ota_release.ps1') -Project main-deck-jc1060 -ReleaseDir $releaseDir -PublicKey $publicKey -BaseUrl 'https://ota.pajoniiir.eu/' | Out-Null }
    catch { $rejected=$_.Exception.Message -match 'separate OTA channel' }
    if (-not $rejected) { throw 'JC1060 accepted the JC4880 channel root' }
    # A correctly signed bundle named after the selected board still cannot
    # carry the other board's signed project.
    Copy-Item -LiteralPath $bundle -Destination $jcBundle -Force
    $rejected=$false
    try { & (Join-Path $RepoRoot 'tools/publish_ota_release.ps1') -Project main-deck-jc1060 -ReleaseDir $releaseDir -PublicKey $publicKey | Out-Null }
    catch { $rejected=$_.Exception.Message -match 'not a versioned main-deck-jc1060' }
    if (-not $rejected) { throw 'Channel accepted renamed wrong-board bundle' }
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host "OTA release helper tests passed."
