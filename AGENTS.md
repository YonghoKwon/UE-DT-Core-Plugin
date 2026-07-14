# DTCore Plugin — AI 에이전트 가이드

이 파일은 AI 코딩 에이전트(Claude Code, Copilot, Cursor 등)가 이 저장소에서 작업할 때 지켜야 할 규칙과 핵심 컨텍스트를 정리한 것입니다. 상세 아키텍처는 `README.md`, 코드 분석은 `docs/ANALYSIS.md`, 수정 요청 프롬프트는 `docs/수정요청.md`를 참고하세요.

## 프로젝트 개요

Unreal Engine 기반 디지털 트윈 프로젝트에서 반복 사용하는 런타임 기능(API 통신, WebSocket/STOMP 수신, 데이터 큐 처리, 객체 레지스트리, 위젯 관리, 파일 로그)을 공통화한 **재사용 플러그인**입니다. Runtime 모듈 1개(`DTCore`)로 구성되며, 소비 프로젝트의 `Plugins/DTCore`에 배치되어 함께 빌드됩니다. **플러그인 단독으로는 빌드할 수 없고, 자동화 테스트와 CI는 없습니다.**

## 디렉터리 구조

```text
Source/DTCore
├─ Public / Private
│  ├─ Core            # Subsystem 8종, Settings, GameMode/GameInstance, RuntimeConfig
│  ├─ Api             # FApiStruct(DataTable Row), UApiMessage 핸들러 기반 클래스
│  ├─ WebSocket       # FTransactionCodeStruct, UTransactionCodeMessage 핸들러 기반
│  ├─ UI              # DxWidget, WidgetConfig/Theme DataAsset
│  ├─ ActorComponent  # DataSync/StatusVisualizer/MechDriver/VirtualDistanceSensor
│  ├─ Player          # DxPlayerBase, DxPlayerControllerBase
│  ├─ Manager         # DxLevelManagerBase
│  ├─ InteractableActor
│  └─ Lib             # YyJsonParser + yyjson (서드파티)
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

## 코딩 컨벤션

- 클래스 접두사 `Dx` (예: `UDxApiSubsystem`), 구조체는 `F`, DataTable Row 구조체는 `Api/WebSocket` 폴더에 위치.
- 주석은 한국어를 사용합니다. 기존 파일의 주석 밀도와 스타일을 따르세요.
- 로그는 GameThread에서 `DX_LOG(GetWorld(), ...)`, 백그라운드 스레드에서는 `UE_LOG(LogBase, ...)`를 사용합니다.
- 탭 들여쓰기, Allman 브레이스 등 기존 UE 스타일을 유지하세요.

## 빌드 / 검증

- 빌드: 소비 UE 프로젝트의 `Plugins/DTCore`에 배치 → "Generate Visual Studio project files" → 프로젝트 빌드. CI가 없으므로 수정 후 반드시 소비 프로젝트에서 컴파일 확인이 필요합니다.
- 수정 후 검증 항목은 `README.md` 11절(패키징 테스트 체크리스트)을 따르세요.
- 알려진 이슈/불일치 목록은 `docs/ANALYSIS.md` 4절에 정리되어 있습니다. 특히 Topic 라우팅(`DxWebSocketSubsystem.cpp`의 `ReceivedMessage`)은 의도 확인이 필요한 상태이므로 임의로 "수정"하지 마세요.
