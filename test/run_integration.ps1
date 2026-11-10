param(
    [Parameter(Mandatory = $true)] [string] $ServerExe,
    [Parameter(Mandatory = $true)] [string] $TestExe,
    [Parameter(Mandatory = $true)] [string] $ProjectRoot
)

$runRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("wechat-mall-integration-" + [guid]::NewGuid().ToString("N"))
$databaseRoot = Join-Path $runRoot "database"
New-Item -ItemType Directory -Path $databaseRoot -Force | Out-Null
Copy-Item (Join-Path $ProjectRoot "database\schema.sql") $databaseRoot -Force
Copy-Item (Join-Path $ProjectRoot "database\init_data.sql") $databaseRoot -Force

$serverPath = Join-Path $runRoot "server.exe"
$testPath = Join-Path $runRoot "test_integration.exe"
Copy-Item $ServerExe $serverPath -Force
Copy-Item $TestExe $testPath -Force

$server = $null
$serverStarted = $false
$exitCode = 1
try {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $serverPath
    $startInfo.WorkingDirectory = $runRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $server = New-Object System.Diagnostics.Process
    $server.StartInfo = $startInfo

    # 异步读取并丢弃输出：持续排空重定向管道，防止约 4KB 缓冲区
    # 写满后子进程的日志线程（cout/cerr）阻塞，导致后续请求超时。
    $drain = { }
    Register-ObjectEvent -InputObject $server -EventName OutputDataReceived -Action $drain | Out-Null
    Register-ObjectEvent -InputObject $server -EventName ErrorDataReceived -Action $drain | Out-Null

    [void]$server.Start()
    $server.BeginOutputReadLine()
    $server.BeginErrorReadLine()
    $serverStarted = $true
    Start-Sleep -Seconds 2

    if ($server.HasExited) {
        throw "server exited before integration test started"
    }

    & $testPath
    $exitCode = $LASTEXITCODE
}
finally {
    if ($serverStarted -and $null -ne $server -and -not $server.HasExited) {
        $server.StandardInput.WriteLine("q")
        $server.StandardInput.Close()
        if (-not $server.WaitForExit(5000)) {
            $server.Kill()
            $server.WaitForExit()
        }
    }

    Remove-Item -LiteralPath $runRoot -Recurse -Force -ErrorAction SilentlyContinue
}

exit $exitCode
