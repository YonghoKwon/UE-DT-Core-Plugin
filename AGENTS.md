# DTCore Plugin — AI 에이전트 가이드

이 파일은 AI 코딩 에이전트(Claude Code, Copilot, Cursor 등)가 이 저장소에서 작업할 때 지켜야 할 규칙과 핵심 컨텍스트를 정리한 것입니다. 현재 구조와 동작은 `README.md`를 따릅니다. `docs/` 아래 문서는 해당 시점의 기록입니다(README 14절).

## 프로젝트 개요

Unreal Engine 기반 디지털 트윈 프로젝트에서 반복 사용하는 런타임 기능(API 통신, WebSocket/STOMP 수신·송신, 데이터 채널, 데이터 큐 처리, 객체 레지스트리, 위젯 관리, 파일 로그)을 공통화한 **재사용 플러그인**입니다. Runtime 모듈 1개(`DTCore`)로 구성되며, 소비 프로젝트의 `Plugins/DTCore`에 배치되어 함께 빌드됩니다. **플러그인 빌드에는 소비 프로젝트 또는 최소 compat host가 필요합니다. 자체 자동화 테스트는 `Source/DTCore/Private/Tests`에 있으며 CI는 없습니다.**

## 디렉터리 구조

```text
Source/DTCore
├─ Public / Private
│  ├─ Core            # Subsystem 8종, 데이터 채널, Settings, GameMode/GameInstance, RuntimeConfig
│  ├─ Api             # FApiStruct(DataTable Row), UApiMessage 핸들러 기반 클래스
│  ├─ WebSocket       # FTransactionCodeStruct, UTransactionCodeMessage 핸들러 기반
│  ├─ UI              # DxWidget, WidgetConfig/Theme DataAsset
│  ├─ ActorComponent  # DataSync/StatusVisualizer/MechDriver/VirtualDistanceSensor
│  ├─ Player          # DxPlayerBase, DxPlayerControllerBase
│  ├─ Manager         # DxLevelManagerBase
│  ├─ InteractableActor
│  ├─ Lib             # YyJsonParser + yyjson (서드파티)
│  └─ Tests           # 자동화 테스트 (Private)
└─ DTCore.Build.cs
Config/DefaultDTCore.ini   # CoreRedirects
Config/DefaultGame.ini     # [DTCoreRuntimeOverride] 템플릿
```

## 절대 규칙 (위반 금지)

1. **스레드 모델**: `ParseToStruct()`는 백그라운드 스레드에서 실행됩니다. 내부에서 `NewObject`, `GetWorld()`, Actor/Component/Blueprint 접근을 절대 하지 마세요. 순수 파싱만 허용됩니다. UObject 접근이 필요한 최종 처리는 GameThread에서 실행되는 `ProcessStructData()`에서만 합니다.
2. **프로젝트 독립성**: 특정 프로젝트 모듈/맵/Actor에 의존하거나 서버 URL을 cpp에 하드코딩하지 마세요. 환경별 값은 `UDTCoreSettings`(Project Settings) 또는 `Game.ini`의 `[DTCoreRuntimeOverride]` 섹션으로만 주입합니다.
3. **의존성 격리**: Public 의존 모듈은 `Core`, `CoreUObject`, `Engine`, `UMG`뿐입니다. Public 헤더에 새 모듈 의존성을 추가하지 마세요. HTTP/WebSockets/Stomp/Slate 계열은 Private에만 둡니다.
4. **서드파티 불변**: `Source/DTCore/Private/Lib/yyjson.c`와 `Public/Lib/yyjson.h`는 서드파티(yyjson) 코드입니다. 수정하지 마세요.
5. **Tick 정책**: Base 클래스들(`DataSyncCompBase`, `StatusVisualizerCompBase`, `MechDriverCompBase`, `DxLevelManagerBase`, `InteractableActor`)은 `bStartWithTickEnabled = false` 상태를 유지해야 합니다. Tick이 필요한 파생 클래스가 직접 켭니다.
6. **메커니즘과 정책 분리**: 연결·재연결·구독·상태 집계 같은 메커니즘은 DTCore에 둡니다. 토픽 이름, 메시지 형식, 장치별 전송 구현 같은 정책은 사용 프로젝트나 플러그인에 둡니다. DTCore는 DTDevice를 참조하지 않습니다(의존 방향은 DTDevice → DTCore).
7. **기본 동작 유지**: 데이터 채널은 opt-in입니다. `DataChannels`가 비어 있고 `RegisterChannel`을 부르지 않으면 기존 기본 연결의 동작이 바뀌면 안 됩니다.

## 아키텍처 핵심 (수정 시 반드시 이해할 것)

데이터 파이프라인 (`UDxDataSubsystem` 중심):

```text
HTTP/STOMP 수신 → TQueue 적재 → Tick에서 5ms 예산 내 배치 추출
→ 백그라운드 스레드: yyjson 파싱 + Handler->ParseToStruct
→ GameThread: Handler->ProcessStructData
```

- 비동기 작업은 `TWeakObjectPtr` + `bIsShuttingDown` 이중 체크, 핸들러 맵은 `TSharedPtr<TMap>` 스냅샷으로 백그라운드에 전달하는 기존 패턴을 그대로 따르세요.
- 핸들러 디스패치 키: API는 응답 JSON의 `meta.resource`/`meta.action` 조합, WebSocket은 `MESSAGE_ID`(TransactionCode). DataTable 표기와 **대소문자까지 일치**해야 매칭됩니다.
- Subsystem은 모두 `UGameInstanceSubsystem`입니다. Subsystem 간 초기화 순서는 보장되지 않으므로 다른 Subsystem이 Initialize에서 필요하면 `Collection.InitializeDependency<T>()`를 사용하세요.
- 에셋 로드(DataTable 등)는 CDO 생성자가 아니라 `Initialize()`에서 수행합니다 (쿠킹/시작 히치 방지).
- 데이터 채널: 제어 API와 `OnStateChanged` broadcast는 GameThread입니다. `UpdateState`, `SetLastError`, `Record*` 보호 훅만 어느 스레드에서 불러도 됩니다. 이전 연결의 결과는 `IsCurrentGeneration`으로 거릅니다. 비밀값은 설정에 넣지 않고 Runtime Override 키 이름만 둡니다.
- 설정: `UDTCoreSettings`는 `Config=Game`입니다. 패키지에서는 pak의 설정이 일반 파일을 가리며, 패키지 옆에서 바꿀 수 있는 값은 `DTCoreRuntimeConfig`가 읽는 접속 키뿐입니다(README 9절).

## 코딩 컨벤션

- 클래스 접두사 `Dx` (예: `UDxApiSubsystem`), 구조체는 `F`, DataTable Row 구조체는 `Api/WebSocket` 폴더에 위치.
- 주석은 한국어를 사용합니다. 기존 파일의 주석 밀도와 스타일을 따르세요.
- 로그는 GameThread에서 `DX_LOG(GetWorld(), ...)`, 백그라운드 스레드에서는 `UE_LOG(LogBase, ...)`를 사용합니다.
- 탭 들여쓰기, Allman 브레이스 등 기존 UE 스타일을 유지하세요.

## 빌드 / 검증

- 빌드: 소비 UE 프로젝트의 `Plugins/DTCore`에 배치 → "Generate Visual Studio project files" → 프로젝트 빌드. CI가 없으므로 수정 후 소비 프로젝트 또는 compat host에서 컴파일을 확인합니다. compat host 명령은 `docs/QUALITY_90.md`에 있습니다.
- 수정 후 검증 항목은 `README.md` 11절(패키징 테스트 체크리스트)을 따르세요.
- 알려진 이슈/불일치 목록은 `docs/ANALYSIS.md` 4절에 정리되어 있습니다. Topic 라우팅은 소유자 결정으로 확정되었습니다. `WebSocketTopics`와 `ApiTopics`는 모두 구독 대상 목록이며 `EnqueueWebSocketData`와 `MESSAGE_ID` 핸들러를 사용합니다. 이 동작을 유지하세요.

- 자체 테스트는 Editor에서 `Automation RunTests DTCore`로 실행합니다. fake STOMP/RequestProbe 계약 시험과 opt-in 실제 Broker 60분 시험을 구분하고 skip을 성공으로 보고하지 마세요. 상세 검증은 `docs/QUALITY_90.md`를 참고하세요.
