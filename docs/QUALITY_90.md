# DTCore 품질 보강 A+B+C

기준은 `origin/main`의 `7157339d42847fd540f226355350233d99d8f269`이며 작업 브랜치는 `codex/dtcore-quality-90`이다. 검증 증거는 저장소의 무시 경로 `Saved/Validation/quality90/`에 보관하며 커밋하지 않는다. 이 문서는 Windows UE 5.3 compat host 검증과 실제 소비 프로젝트 수용을 구분한다.

## 변경과 호환 계약

| 영역 | 변경 | 유지한 계약 |
|---|---|---|
| STOMP Topic | 중복 enum 분기를 제거하고 구독 목록 의미를 문서화 | 두 Topic 목록 모두 `EnqueueWebSocketData`, `MESSAGE_ID` 핸들러 사용. enum·Config 이름 유지 |
| 연결 상태 | `EDxWebSocketConnectionState`, 3개 조회 함수, `OnConnectionStateChanged` | 기존 `OnConnected` 시그니처와 batch 완료 의미 유지. 성공·실패 혼합 및 Topic 0개도 완료 |
| receipt timeout | 선택적으로 미회수 receipt를 실패로 집계하여 batch 완료 | 기본 0에서는 무기한 대기. 늦은 callback·중복·이전 generation은 집계/통지하지 않음 |
| 위젯 | 중복 검사·BringToFront·hover 및 Main 교체 시 죽은 항목 정리 | 화면에 붙은 정적 등록 항목 보존. `CloseWidgetAddLogic`을 추가 호출하지 않음 |
| UI 모드 | 마지막 일치 행 주석 정정, 중복 일치 1회 경고 | DataTable 순회의 마지막 일치 행 적용. 행 이름 정렬 의미 추가 없음 |
| HTTP | timeout·최대 시도·초기 재시도 지연 설정화 | 0 timeout은 `SetTimeout` 미호출. 연결 실패/5xx 재시도, 4xx 정책·Body·URL 우선순위·종료 generation 유지 |
| 데이터 | 원자 통계 및 미등록 키 최초 경고 | 큐/배치/5ms 추출 예산·파싱 스레드·핸들러 처리 순서 유지. 기존 `TotalProcessedCount` 위치/의미 유지 |
| 로그 | 디렉터리·보관 일수 설정화 | 기본 LaunchDir 경로·7일, 파일명·50MB·suffix 1~20 rotation·flush/IO 직렬화 유지 |
| 입력/하이라이트 | stencil 및 우선 채널·object 목록 편집 속성 | 초기 stencil 100/254, 우선 채널 및 기존 5개 object type 유지 |
| Legacy 설정 | CCTV/Ship DataTable을 Legacy 프로젝트 전용으로 표시 | 이름·UPROPERTY·Config 키 유지. 신규 사용 비권장 표시만 추가 |
| 자동화 | 기존 계약 11개 이관, 품질 시험 7개, 실제 STOMP opt-in 1개 | 프로젝트 자산·맵·모듈 의존 없음. Editor 모듈 의존은 Private 및 Editor target에만 추가 |

새 상태 이벤트는 상태가 바뀔 때에만 GameThread에서 발생한다. `GaveUp` 반복 진입 시 중복 통지하지 않으며 `ConnectWebSocket()`으로 다시 시도할 수 있다. 수동 Disconnect는 재연결 의도를 지우고 `Disconnected`가 된다. 종료는 이벤트 없이 상태·타이머·receipt를 정리한다. 이벤트에서 Disconnect를 재진입 호출하는 경우 진행 중 연결/구독 생성을 중단한다.

`GetReconnectAttemptCount()`는 기존에 retry timer를 예약할 때 증가하던 횟수이다. 전송 연결이 성공하면 기존처럼 0으로 초기화된다. `GetLastSubscribeFailureCount()`는 진행 중/마지막 batch의 명시적 실패와 receipt timeout 수 합계이며 다음 batch 시작 시 초기화된다. `Ready`는 업무 데이터 수용 또는 모든 구독 성공을 뜻하지 않는다.

## 설정과 기본값

새 UDTCoreSettings 항목은 `[/Script/DTCore.DTCoreSettings]` Config에 저장한다. 기존 접속 문자열용 `[DTCoreRuntimeOverride]` reader에 새 숫자/경로 키를 추가한 것은 아니다.

| 항목 | 기본값 | 의미 |
|---|---|---|
| `SubscribeReceiptTimeoutSeconds` | `0.0` | 0은 비활성. 양수일 때 batch 시작 기준 timeout |
| `HttpRequestTimeoutSeconds` | `0.0` | 0은 UE 엔진 기본값, `SetTimeout` 미호출 |
| `HttpMaxAttempts` | `3` | 최초 요청 포함. 코드에서 최소 1로 제한 |
| `HttpInitialRetryDelaySeconds` | `1.0` | 기존 지수 2.0 유지. 코드에서 최소 0.001초로 제한 |
| `LogDirectory` | 빈 문자열 | `LaunchDir/Logs/CustomLogs`. 상대 경로는 `FPaths::LaunchDir()` 기준. Initialize 때 고정 |
| `LogRetentionDays` | `7` | 음수는 0으로 제한. 0은 날짜 비교상 0일 보관이며 무기한 보관 옵션이 아님 |
| `AInteractableActor.HighlightStencilValue` | `100` | 인스턴스 편집 가능. 정적 `HighlightActor`는 메시의 InteractableActor 소유자 설정 사용 |
| `AInteractableActor.ErrorStencilValue` | `254` | 일반 Actor 소유 메시에는 InteractableActor CDO 기본값 사용 |
| `ADxPlayerControllerBase.PriorityTraceChannel` | `ECC_GameTraceChannel1` | 여러 충돌 결과 중 우선 선택할 object channel |
| `ADxPlayerControllerBase.TraceObjectTypes` | WorldStatic, WorldDynamic, Pawn, PhysicsBody, GameTraceChannel1 | 기존 쿼리 대상과 같은 목록 |

`GetStatistics()`의 `FDxDataStatistics`는 API/WS 적용 완료 수, 공통 JSON 파서 실패, dispatch 필드 누락, 미등록 키 수, API/WS 큐 대기를 제공한다. 각 항목은 독립적인 64비트 원자 snapshot이다. 큐에서 이미 추출된 batch는 대기에 포함하지 않는다. 누락 필드는 `meta`, 비어 있는 resource/action, 없거나 빈 `MESSAGE_ID`이며 공통 JSON 실패와 구별한다. Handler의 `ParseToStruct`가 반환한 null은 공통 JSON 파싱 실패 수에 포함하지 않는다. 미등록 경고는 API/WS별 키마다 Subsystem 수명 중 최초 1회이고, 미등록 메시지 카운터는 매번 증가한다.

## 동작 차이의 전체 범위

기본 설정에서 HTTP 재시도·timeout, receipt 대기, Topic 라우팅, 로그 위치/보관, 입력 채널과 stencil 값은 기존과 같다. 예외는 요청된 죽은 위젯 항목 제거로 다시 열기가 정상화되는 수정, 새 상태/통계 API·진단 경고이다. Main 교체 시 기존 위젯에 새 닫기 hook을 보내지 않는다.

timeout을 양수로 설정하면 미회수 receipt가 있더라도 실패로 집계한 뒤 기존 `OnConnected`를 한 번 발생시키는 새 opt-in 동작이다. HTTP·로그·입력 속성을 명시적으로 바꾸면 해당 설정에 따른 동작을 사용한다. URL/HTTP 오류 본문/JSON 원문을 진단 로그에서 제거하여 payload 또는 URL에 포함될 수 있는 인증값을 남기지 않는다. 구독 실패 원문 대신 실패 수를 공개한다. 업무 콜백의 Content/Error 시그니처는 바꾸지 않는다.

Public 의존 모듈은 Core/CoreUObject/Engine/UMG를 유지한다. yyjson 두 파일과 Base Tick 기본값은 수정하지 않았다. 기존 공개 함수·UPROPERTY·Blueprint 노드를 삭제하거나 바꾸지 않았다. 소비 프로젝트 전용 `FDTCoreSlabBrokerCheck` friend만 요청대로 제거했다. 내부 테스트의 friend는 유지/추가했다. fixture UObject 타입은 UHT용 Private 헤더에 선언하고 실제 시험 코드는 `WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR`로 제한한다. fixture 이름은 소비 프로젝트의 기존 Listener와 충돌하지 않도록 `UDxCoreTest*`를 사용한다.

## 재현 명령과 증거

최소 host는 읽기 전용 템플릿 `C:\Unreal Projects\ma0t10_dt\Tools\DTCoreCompatHost`를 `C:\Unreal Projects\UE-DT-Core-Plugin\Saved\Validation\CompatHost`로 복사했다. `Plugins/DTCore`에는 현재 DTCore.uplugin, Source, Config, Content, Resources를 실제 복사했다. junction은 사용하지 않았다. `git check-ignore Saved/Validation/quality90/probe.txt`로 무시 경로를 확인했다. ma0t10_dt에서는 파일 읽기/복사 원본 참조만 했고 수정·빌드·Editor 실행·Git 조작은 하지 않았다.

```powershell
$hostProject = 'C:/Unreal Projects/UE-DT-Core-Plugin/Saved/Validation/CompatHost/DTCoreCompatHost.uproject'
$evidence = 'C:/Unreal Projects/UE-DT-Core-Plugin/Saved/Validation/quality90'
& 'C:/Program Files/Epic Games/UE_5.3/Engine/Build/BatchFiles/Build.bat' DTCoreCompatHostEditor Win64 Development "-Project=$hostProject" -WaitMutex -NoHotReloadFromIDE
& 'C:/Program Files/Epic Games/UE_5.3/Engine/Build/BatchFiles/Build.bat' DTCoreCompatHost Win64 Shipping "-Project=$hostProject" -WaitMutex -NoHotReloadFromIDE
& 'C:/Program Files/Epic Games/UE_5.3/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' $hostProject -unattended -nosplash -nop4 -NullRHI '-ExecCmds=Automation RunTests DTCore;Quit' '-TestExit=Automation Test Queue Empty' "-abslog=$evidence/automation-final.log" "-ReportExportPath=$evidence/automation-final"
& ./Tools/RunQuality90Soak.ps1
git diff --check
```

| 검증 | 구분 | 결과/증거 |
|---|---|---|
| Editor Win64 Development | 수행·통과 | `build-editor-final.log` |
| Win64 Shipping | 수행·통과 | `build-shipping.log`, exit 0 |
| 일반 DTCore 자동화 | 수행·통과 | 18개: 성공 17, 경고 포함 성공 1, 실패 0, skip 0, notRun 0. `automation-final/index.json`, `automation-final.log`, exit 0 |
| NullRHI 한계 | 해당 없음 | Blueprint close, 입력 정책, 위젯 트리 fixture 포함 일반 시험 모두 NullRHI로 완료. 별도 D3D12 실행 불필요 |
| 실제 STOMP 60분 | 수행·통과(경고 포함) | 3,600.0초, 경고 포함 성공 1, 실패 0, skip 0, notRun 0. `soak-report/index.json`, `soak.log`, exit 0 |
| Docker 정리 | 수행·통과 | stop/start 5회. `down -v --remove-orphans` exit 0, 해당 프로젝트 컨테이너/네트워크/볼륨 모두 0개. `docker-cleanup.json`, `docker-cleanup.log`, `soak-result.json` |
| diff/인코딩/복사 동일성 | 수행·통과 | 77개 payload 파일 host 해시 동일, 기존 BOM/줄바꿈 보존. `git diff --check`, `source-manifest.json`, `encoding-check.json` |
| 실제 ma0t10_dt 수용·Linux·패키징 실행 | 미수행·범위/환경 | 읽기 전용 소비 프로젝트 보호. compat host 컴파일/로컬 시험을 회사 또는 실제 프로젝트 수용으로 대체하지 않음 |

최종 실제 Broker 시험은 한국 시간 2026-10-29 11:12:11.829 Ready부터 12:12:11.854 완료까지 3,600.0초를 측정했다. 10분 간격으로 5회 Broker stop/start를 수행했고 Ready 6회, 수신 재개 확인 6회, 실제 메시지 3,533건, Reconnecting 상태 진입 15회를 기록했다. 재시작마다 중단 중 여러 연결 시도가 있으므로 재연결 상태 횟수와 Broker 재시작 횟수는 다르다. 프로세스는 크래시 없이 종료 코드 0으로 끝났다. 중단 구간의 메시지 무손실을 검증한 시험은 아니다.

soak의 경고 포함 성공은 Windows 오디오 장치 변경에 따른 AudioMixer 경고 3개(장치 변경·제거·기본 장치 전환)이며 오류는 0개이다. Docker CLI의 진행 stderr는 Windows PowerShell 5에서 NativeCommandError 형식으로 로그에 남을 수 있으나 각 stop/start 및 최종 down 종료 코드는 0이고 실제 복구/정리 결과로 판정했다. 일반 자동화 18개와 실제 Broker 1개를 합하면 19개 성공(그중 경고 포함 2개), 실패 0, skip 0이다.

| 메모리 기록 | 첫 샘플 | 마지막 샘플 | 최소 / 최대 | 증가 |
|---|---|---|---|---|
| UE 프로세스 physical(30초 간격, 120개) | 2,124.04MiB | 2,188.26MiB | 2,124.04 / 2,188.30MiB | 64.22MiB |
| UE 프로세스 private(약 10초 간격, 357개) | 2,100.02MiB | 2,138.10MiB | 2,099.65 / 2,138.10MiB | 38.07MiB |

초기 안정화 이후 일정 구간을 유지하다 약 41분대에 증가한 뒤 다시 비슷한 수준을 유지했다. 증가 원인을 DTCore에 귀속하거나 누수 부재를 단정하지 않는다. 원본 CSV와 `memory-summary.json`을 함께 남겼다. 시험 컨테이너 `dtcore-quality90-artemis`, compose 프로젝트 `dtcore-quality90`의 자원은 최종 조회에서도 모두 없었다.

실패/중단 이력도 보존한다. `build-editor-01.log`는 fixture 데이터 구조체 include 누락, `build-editor-03.log`는 지역 Generation shadow 오류로 실패했다. 수정 후 후속 Editor 빌드와 일반 자동화는 통과했다. Broker 예비 실행은 PowerShell 5의 native stderr 진행 메시지 처리 오류와 최종 소스 반영을 위한 명시적 중단이 있었다. 짧은 예비 실행은 60분 성공으로 세지 않으며 `soak-preflight-02/`, `soak-preflight-03/`에 보관했다. 예비 Docker 자원 정리는 완료했다.

일반 자동화의 경고 포함 성공 1개는 `DataStatistics`가 의도적으로 잘못된 JSON/누락 필드를 주입한 경우이다. 미등록 키 경고는 키별 최초 1회 예상 횟수까지 검증한다. 로그 쓰기 실패 주입은 `LogFlush`에서 예상 오류로 확인하며 숨기거나 skip으로 처리하지 않는다.

## 이관한 시험과 소비 프로젝트 작업

다음 11개는 모두 DTCore만으로 성립하도록 이관했다. 누락된 요청 계약 시험은 없다.

`DTCore.Contract.BlueprintCloseDispatch`, `ClickPolicy`, `ParseShutdown`, `LogFlush`, `RegistryGC`, `SubscriptionLifecycle`, `HttpBodyAndFallback`, `WidgetIdentifiers`, `QueueSurvivesWorldCleanup`, `RegistryEngineStreamOut`, `WidgetCloseOrder`.

품질 시험은 `DTCore.Quality.ConnectionState`, `ReceiptTimeout`, `HttpSettings`, `DataStatistics`, `LogSettings`, `WidgetPruning`, `InputSettings`이다. fake STOMP/RequestProbe/RuntimeOverrideReader를 사용한다. `WidgetPruning`은 실제 최소 World·Viewport·WidgetTree로 정적 자식 유지, 정적/동적 직접 제거, Main 교체, 마지막 일치 행 경고와 닫기 hook 불호출을 검사한다.

의도적으로 이관하지 않은 프로젝트 시험은 `MA0T10.SensorFiles.CameraAcquisitionSnapshot`(프로젝트 Camera/Sensor 타입과 JPEG 획득 정책), `MA0T10.DTCoreIntegration.CraneDispatch`(CraneDataSyncComp/업무 상태 구조체), `MA0T10.DTCoreIntegration.SlabBroker`(SlabActor·소비 프로젝트 맵/데이터 파이프라인)이다. 실제 Slab 업무 시험은 DTCore만으로 성립하지 않는다. 대신 일반 STOMP 60분 연결·복구·수신 시험을 새로 추가했다.

ma0t10_dt가 이 버전을 사용할 때는 다음 변경이 필요하다. 이번 작업에서는 해당 파일을 수정하지 않았다.

1. `DTCoreSlabBrokerTests.cpp`는 friend/private 멤버 대신 `GetConnectionState() == EDxWebSocketConnectionState::Ready`, `GetReconnectAttemptCount()`, `GetLastSubscribeFailureCount()`를 사용한다. 수동 Disconnect 후 `Disconnected`를 확인한다. `Ready`를 모든 구독 성공으로 해석하지 않는다.
2. `DTCoreContractTests.cpp`에서 위 11개 이관 시험과 사용하지 않게 된 helper/fixture는 중복이므로 제거 대상이다. 프로젝트 Camera/Crane 및 실제 업무 수용 시험은 소비 프로젝트에 남긴다.
3. 새로운 설정은 기본값으로 먼저 적용하고 프로젝트가 timeout·retry·로그 배포 위치·충돌 채널을 선택적으로 설정한다. 기존 Cctv/Ship Config 이름은 그대로 읽을 수 있다.

## 남은 위험

receipt timeout 기본값 0에서는 receipt가 영구 미회수면 기존처럼 Subscribing에서 대기한다. Ready는 전송과 구독 callback 집계 상태이며 업무 ACK가 아니다. 미등록 키 최초 경고 집합은 서로 다른 키 수에 비례해 증가한다. 카운터 snapshot은 항목 간 동시 시점 일관성을 보장하지 않는다. 파싱 worker와 로그 I/O 종료 대기는 기존처럼 작업 완료를 기다리며 강제 취소 상한을 추가하지 않았다. 메모리는 compat host Editor 프로세스 전체 값이므로 플러그인 단독 사용량이나 장기 누수 부재를 증명하지 않는다. soak는 custom 수신 delegate로 전송 복구를 관측하며 업무 Handler 수용을 대신하지 않는다. CI·Linux 검증과 실제 프로젝트 BP/자산·회사 Broker 수용은 별도로 남아 있다.

검증한 실행 소스는 `adef3c0a9ef3e4778b15300ae80b29fb474bc06a`이다. 기반 4개 구현 커밋은 `95407fc`(설정/HTTP/로그/입력), `0d8a0b8`(STOMP), `8dc8816`(통계), `b419787`(Widget)이며 `adef3c0`은 자체 자동화와 soak 도구를 추가한다. 후속 문서 커밋은 실행 소스를 바꾸지 않는다. 바이너리 SHA-256과 UE 버전은 `validation-metadata.json`에 기록했다.
