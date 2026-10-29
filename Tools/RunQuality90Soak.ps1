param(
	[string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.3',
	[string]$BrokerTemplate = 'C:/Unreal Projects/ma0t10_dt/Tools/Artemis',
	[string]$HostProject = "$PSScriptRoot/../Saved/Validation/CompatHost/DTCoreCompatHost.uproject"
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$evidenceRoot = Join-Path $repoRoot 'Saved/Validation/quality90'
$brokerRoot = Join-Path $evidenceRoot 'Broker'
$composePath = Join-Path $brokerRoot 'docker-compose.yml'
$projectName = 'dtcore-quality90'
$containerName = 'dtcore-quality90-artemis'
$docker = 'C:/Program Files/Docker/Docker/resources/bin/docker.exe'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$process = $null
$created = $false
$restartCount = 0
$testExit = $null
$cleanupOk = $false
$environmentNames = @('DTCORE_STOMP_SOAK', 'DTCORE_SOAK_URL', 'DTCORE_SOAK_USER', 'DTCORE_SOAK_PASSWORD', 'DTCORE_SOAK_EVIDENCE')
$oldEnvironment = @{}

foreach ($name in $environmentNames)
{
	$oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

# Windows PowerShell 5에서는 native stderr 진행 메시지도 ErrorRecord가 된다.
# Docker 실패는 아래에서 종료 코드로 판정하며 명시적 throw는 계속 종료 오류이다.
$ErrorActionPreference = 'Continue'

try
{
	& $docker info --format '{{.ServerVersion}}' | Set-Content (Join-Path $evidenceRoot 'docker-version.txt')
	if ($LASTEXITCODE -ne 0) { throw 'Docker server unavailable; soak not performed (environment).' }
	$existing = @(& $docker ps -aq --filter "label=com.docker.compose.project=$projectName")
	$networks = @(& $docker network ls -q --filter "label=com.docker.compose.project=$projectName")
	$volumes = @(& $docker volume ls -q --filter "label=com.docker.compose.project=$projectName")
	if ($existing.Count -or $networks.Count -or $volumes.Count)
	{
		throw 'Isolated quality90 project already exists; refusing to reuse unrelated resources.'
	}
	New-Item -ItemType Directory -Force $brokerRoot | Out-Null
	# 원본 폴더에는 쓰지 않는다. bind mount도 검증 폴더의 복사본만 사용한다.
	Copy-Item -LiteralPath (Join-Path $BrokerTemplate 'etc-override') -Destination $brokerRoot -Recurse -Force -ErrorAction Stop
	$compose = [IO.File]::ReadAllText((Join-Path $BrokerTemplate 'docker-compose.yml'))
	$userValue = [regex]::Match($compose, '(?m)^\s+ARTEMIS_USER:\s*(\S+)\s*$').Groups[1].Value
	$passwordValue = [regex]::Match($compose, '(?m)^\s+ARTEMIS_PASSWORD:\s*(\S+)\s*$').Groups[1].Value
	if (!$userValue -or !$passwordValue) { throw 'Broker template credentials could not be read.' }
	$compose = $compose.Replace('container_name: ma0t10-artemis', "container_name: $containerName").Replace('"61616:61616"', '"62616:61616"').Replace('"8161:8161"', '"8261:8161"')
	[IO.File]::WriteAllText($composePath, $compose, [Text.UTF8Encoding]::new($false))
	$created = $true
	& $docker compose -p $projectName -f $composePath up -d *> (Join-Path $evidenceRoot 'broker-up.log')
	if ($LASTEXITCODE -ne 0) { throw 'Isolated broker start failed; see broker-up.log.' }
	$env:DTCORE_STOMP_SOAK = '1'
	$env:DTCORE_SOAK_URL = 'ws://127.0.0.1:62616'
	$env:DTCORE_SOAK_USER = $userValue
	$env:DTCORE_SOAK_PASSWORD = $passwordValue
	$env:DTCORE_SOAK_EVIDENCE = $evidenceRoot
	$logPath = Join-Path $evidenceRoot 'soak.log'
	$reportPath = Join-Path $evidenceRoot 'soak-report'
	[IO.File]::WriteAllText((Join-Path $evidenceRoot 'process-memory.csv'), "utc,working_set_bytes,private_bytes`n")
	$arguments = @("`"$((Resolve-Path $HostProject).Path)`"", '-unattended', '-nosplash', '-nop4', '-NullRHI',
		'"-ExecCmds=Automation RunTests DTCore.Soak;Quit"', '"-TestExit=Automation Test Queue Empty"',
		"`"-abslog=$logPath`"", "`"-ReportExportPath=$reportPath`"")
	$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $evidenceRoot 'soak-console.log') -RedirectStandardError (Join-Path $evidenceRoot 'soak-stderr.log')
	$null = $process.Handle
	$deadline = [DateTime]::UtcNow.AddSeconds(150)
	while (!(Test-Path $logPath) -or (Get-Item -LiteralPath $logPath).LastWriteTimeUtc -lt $process.StartTime.ToUniversalTime() -or !(Select-String -LiteralPath $logPath -SimpleMatch 'DTCORE_SOAK_READY' -Quiet))
	{
		if ($process.HasExited) { throw 'Soak process exited before Ready.' }
		if ([DateTime]::UtcNow -gt $deadline) { throw 'Soak did not reach Ready within 150 seconds.' }
		Start-Sleep -Seconds 2
	}
	$started = [DateTime]::UtcNow
	[IO.File]::WriteAllText((Join-Path $evidenceRoot 'soak-start-utc.txt'), $started.ToString('o'))
	$nextRestart = $started.AddSeconds(600)
	$deadline = $started.AddSeconds(3780)
	while (!$process.HasExited)
	{
		if ([DateTime]::UtcNow -gt $deadline) { throw 'Soak exceeded 63-minute process deadline.' }
		if ($restartCount -lt 5 -and [DateTime]::UtcNow -ge $nextRestart)
		{
			& $docker compose -p $projectName -f $composePath stop -t 5 artemis *>> (Join-Path $evidenceRoot 'broker-restarts.log')
			if ($LASTEXITCODE -ne 0) { throw 'Broker stop failed.' }
			Start-Sleep -Seconds 5
			& $docker compose -p $projectName -f $composePath start artemis *>> (Join-Path $evidenceRoot 'broker-restarts.log')
			if ($LASTEXITCODE -ne 0) { throw 'Broker start failed.' }
			$restartCount++
			Add-Content (Join-Path $evidenceRoot 'broker-restarts.log') "restart=$restartCount utc=$([DateTime]::UtcNow.ToString('o'))"
			$nextRestart = $started.AddSeconds(600 * ($restartCount + 1))
		}
		$process.Refresh()
		Add-Content (Join-Path $evidenceRoot 'process-memory.csv') "$([DateTime]::UtcNow.ToString('o')),$($process.WorkingSet64),$($process.PrivateMemorySize64)"
		Start-Sleep -Seconds 10
	}
	$process.WaitForExit()
	$testExit = $process.ExitCode
}
finally
{
	if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
	if ($created)
	{
		& $docker compose -p $projectName -f $composePath down -v --remove-orphans *> (Join-Path $evidenceRoot 'docker-cleanup.log')
		$downExit = $LASTEXITCODE
		$containers = @(& $docker ps -aq --filter "label=com.docker.compose.project=$projectName")
		$networks = @(& $docker network ls -q --filter "label=com.docker.compose.project=$projectName")
		$volumes = @(& $docker volume ls -q --filter "label=com.docker.compose.project=$projectName")
		$cleanupOk = $downExit -eq 0 -and !$containers.Count -and !$networks.Count -and !$volumes.Count
		@{ downExit = $downExit; containers = $containers; networks = $networks; volumes = $volumes; clean = $cleanupOk } | ConvertTo-Json | Set-Content (Join-Path $evidenceRoot 'docker-cleanup.json')
	}
	foreach ($name in $environmentNames)
	{
		[Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
	}
	@{ testExit = $testExit; brokerRestarts = $restartCount; dockerClean = $cleanupOk } | ConvertTo-Json | Set-Content (Join-Path $evidenceRoot 'soak-result.json')
}

if ($testExit -ne 0 -or !$cleanupOk) { throw 'Soak or Docker cleanup failed; inspect quality90 evidence.' }
