# Memory of each example's window on Windows, the way Task Manager shows it
# (the private working set), with the GPU libraries it loaded:
#
#     powershell -File script/memory.ps1 [renderer...]
#
# Each renderer named (cpu, gl, vulkan) is forced with MYZIG_RENDERER; ""
# is the default choice.
param([string[]]$Renderers = @(""))
$apps = "counter", "quotes", "list", "gallery", "editor"
$rows = @()
foreach ($r in $Renderers) {
    $env:MYZIG_RENDERER = $r
    foreach ($app in $apps) {
        $p = Start-Process -FilePath "zig-out\bin\$app.exe" -PassThru
        Start-Sleep -Seconds 4
        $perf = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$($p.Id)"
        $p.Refresh()
        $gpu = ($p.Modules | Where-Object { $_.ModuleName -match "vulkan|opengl|nvoglv|ig[0-9a-z]+icd|amdvlk|atig|nvgl" } | ForEach-Object { $_.ModuleName }) -join " "
        $rows += [pscustomobject]@{
            app = $app
            renderer = if ($r) { $r } else { "default" }
            private_ws_mb = [math]::Round($perf.WorkingSetPrivate / 1MB, 2)
            commit_mb = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
            ws_mb = [math]::Round($p.WorkingSet64 / 1MB, 2)
            gpu_libs = $gpu
        }
        Stop-Process -Id $p.Id -Force
    }
}
Remove-Item Env:MYZIG_RENDERER -ErrorAction SilentlyContinue
$rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Output
# As annotations too, which the checks API returns.
foreach ($g in ($rows | Group-Object renderer)) {
    $line = ($g.Group | ForEach-Object { "$($_.app) $($_.private_ws_mb)/$($_.commit_mb)/$($_.ws_mb) MB [$($_.gpu_libs)]" }) -join "; "
    Write-Output "::notice title=Windows memory, $($g.Name) (private WS/commit/WS)::$line"
}
if ($env:GITHUB_STEP_SUMMARY) {
    "### Windows memory (private working set, as Task Manager shows)" | Out-File -Append $env:GITHUB_STEP_SUMMARY
    "| app | renderer | private WS | commit | working set | GPU libraries |" | Out-File -Append $env:GITHUB_STEP_SUMMARY
    "| --- | --- | --- | --- | --- | --- |" | Out-File -Append $env:GITHUB_STEP_SUMMARY
    foreach ($row in $rows) {
        "| $($row.app) | $($row.renderer) | $($row.private_ws_mb) MB | $($row.commit_mb) MB | $($row.ws_mb) MB | $($row.gpu_libs) |" | Out-File -Append $env:GITHUB_STEP_SUMMARY
    }
}
