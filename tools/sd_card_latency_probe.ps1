# microSD write-latency probe.
#
# Throughput is not the problem: the recorder needs only 176 kB/s. What kills it
# is a burst of writes that each block for hundreds of milliseconds while the
# card does internal housekeeping. The card in the P4 was measured doing eight
# consecutive ~360 ms stalls, which drains the whole 2.9 s ring. So this measures
# the latency distribution of sustained writes, not MB/s.
param(
    [string]$Drive = "F:",
    [int]$TotalMB  = 256,
    [int]$ChunkKB  = 32
)

$path  = Join-Path $Drive "ddj_sd_latency_probe.tmp"
$chunk = $ChunkKB * 1KB
$count = [int](($TotalMB * 1MB) / $chunk)
$buf   = New-Object byte[] $chunk
(New-Object Random 1234).NextBytes($buf)

Write-Output "probe: $path"
Write-Output "writing $TotalMB MB in $count x $ChunkKB KiB WriteThrough chunks"

$lat = New-Object double[] $count
# WriteThrough so each chunk reaches the device instead of sitting in RAM.
$fs = [System.IO.FileStream]::new($path, [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None, $chunk,
        [System.IO.FileOptions]::WriteThrough)
$sw = [System.Diagnostics.Stopwatch]::new()
$t0 = [System.Diagnostics.Stopwatch]::StartNew()
try {
    for ($i = 0; $i -lt $count; $i++) {
        $sw.Restart()
        $fs.Write($buf, 0, $chunk)
        $sw.Stop()
        $lat[$i] = $sw.Elapsed.TotalMilliseconds
    }
    $sw.Restart(); $fs.Flush($true); $sw.Stop()
    $finalFlush = $sw.Elapsed.TotalMilliseconds
} finally {
    $fs.Dispose()
}
$t0.Stop()

$sorted = [double[]]$lat; [Array]::Sort($sorted)
function P([double[]]$a, [double]$q) { $a[[int][Math]::Floor(($a.Length - 1) * $q)] }

$mb   = $TotalMB / $t0.Elapsed.TotalSeconds
$o100 = ($lat | Where-Object { $_ -ge 100 }).Count
$o360 = ($lat | Where-Object { $_ -ge 360 }).Count
$o50  = ($lat | Where-Object { $_ -ge 50  }).Count

Write-Output ""
Write-Output "--- write latency over $count chunks ---"
Write-Output ("  median      {0,8:N2} ms" -f (P $sorted 0.50))
Write-Output ("  p99         {0,8:N2} ms" -f (P $sorted 0.99))
Write-Output ("  p99.9       {0,8:N2} ms" -f (P $sorted 0.999))
Write-Output ("  MAX         {0,8:N2} ms" -f $sorted[-1])
Write-Output ("  final flush {0,8:N2} ms" -f $finalFlush)
Write-Output ""
Write-Output ("  >= 50 ms    {0}" -f $o50)
Write-Output ("  >= 100 ms   {0}" -f $o100)
Write-Output ("  >= 360 ms   {0}   <- the failure mode seen on the P4 card" -f $o360)
Write-Output ("  throughput  {0:N1} MB/s (recorder needs 0.18)" -f $mb)

# Longest run of consecutive slow chunks: one stall is survivable, a burst is
# what actually drained the ring.
$run = 0; $best = 0
foreach ($v in $lat) { if ($v -ge 100) { $run++; if ($run -gt $best) { $best = $run } } else { $run = 0 } }
Write-Output ("  longest consecutive >=100 ms run: {0} chunks" -f $best)

Remove-Item $path -Force -ErrorAction SilentlyContinue
Write-Output ""
Write-Output "probe file removed"
