# UE-DT-Core-Plugin

`UE-DT-Core-Plugin`은 Unreal Engine 기반 디지털 트윈 프로젝트에서 반복적으로 사용하는 런타임 기능을 공통화한 플러그인입니다.

API 통신, WebSocket/STOMP 수신, 데이터 큐 처리, 객체 등록/검색, 위젯 관리, 로그, 플레이어/레벨 기반 기능, 센서/상태 컴포넌트 등을 하나의 재사용 가능한 Core 계층으로 제공하는 것을 목표로 합니다.

---

## 1. 전체 목표

| 목표 | 설명 |
|---|---|
| 공통 Runtime 기능 제공 | 여러 디지털 트윈 프로젝트에서 반복되는 기반 기능을 하나의 플러그인으로 관리합니다. |
| 프로젝트 독립성 유지 | 특정 프로젝트 모듈, 특정 맵, 특정 Actor, 특정 서버 URL에 직접 의존하지 않는 구조를 지향합니다. |
| 설정 기반 확장 | DataTable, `UDTCoreSettings`, `Game.ini`를 통해 프로젝트별 차이를 주입합니다. |
| 실시간 데이터 처리 | API/WebSocket 데이터를 큐에 넣고 배치/비동기 방식으로 처리합니다. |
| UI/객체/검색 공통화 | Runtime Widget, Object Registry, Search Base를 공통 기반으로 제공합니다. |

---

## 2. 소스 구조 요약

```text
Source/DTCore
├─ Public
│  ├─ Api
│  ├─ ActorComponent
│  ├─ Core
│  ├─ InteractableActor
│  ├─ Lib
│  ├─ Manager
│  ├─ Player
│  ├─ UI
│  └─ WebSocket
├─ Private
│  ├─ ActorComponent
│  ├─ Core
│  ├─ InteractableActor
│  ├─ Lib
│  ├─ Manager
│  ├─ Player
│  └─ UI
└─ DTCore.Build.cs
```

| 영역 | 역할 |
|---|---|
| `Core` | 주요 Subsystem, Settings, GameMode/GameInstance, Runtime 설정 유틸리티 |
| `Api` | API DataTable Row 구조와 API Message Handler 기반 클래스 |
| `WebSocket` | TransactionCode 기반 WebSocket Message Handler 구조 |
| `UI` | 공통 Widget, Widget 설정 DataAsset, Theme DataAsset |
| `ActorComponent` | 동기화, 상태 표시, 센서, 기계 구동 등 공통 ActorComponent 기반 |
| `Player` | 공통 Player / PlayerController 기반 클래스 |
| `Manager` | Level Manager 기반 클래스 |
| `InteractableActor` | UI/상호작용 대상 Actor 기반 |
| `Lib` | JSON Parser 등 공통 유틸리티 |

---

## 3. 모듈 의존성

`DTCore.Build.cs` 기준으로 Public dependency는 최소화하고, HTTP/WebSocket/STOMP/Slate 계열은 Private dependency로 관리합니다.

| 구분 | 모듈 |
|---|---|
| Public | `Core`, `CoreUObject`, `Engine`, `UMG` |
| Private | `InputCore`, `EnhancedInput`, `HTTP`, `WebSockets`, `Stomp`, `Slate`, `SlateCore`, `DeveloperSettings` |

이 구조는 DTCore를 사용하는 외부 프로젝트에 불필요한 의존성이 전파되는 것을 줄이기 위한 방향입니다.

---

## 4. Core Subsystem 구성

### 4.1 `UDxApiSubsystem`

DataTable에 정의된 API 정보를 기준으로 HTTP 요청을 수행하는 Subsystem입니다.

주요 역할:

- `FApiStruct` Row 기반 API 요청 수행
- `EApiType`에 따른 URL 선택
- `EApiMethod`에 따른 HTTP Method 결정
- 직접 URL 호출용 `DxHttpCall` 제공
- 요청 중인 HTTP Request 추적 및 종료 시 Cancel 처리
- 성공 응답을 `UDxDataSubsystem::EnqueueApiData`로 전달

주요 함수:

```cpp
DxHttpCall(...)
DxRequestApi(...)
DxRequestApiWithParameter(...)
IsApiDataTableLoaded()
```

API Row 구조인 `FApiStruct`는 다음 정보를 가집니다.

| 필드 | 설명 |
|---|---|
| `ApiType` | Local/Test/Prod 중 어떤 URL을 사용할지 결정 |
| `ApiMethod` | GET/POST/PUT/DELETE/PATCH |
| `ApiUrl` | 서버 URL 뒤에 붙는 API path |
| `ApiResource` | 응답 처리용 Resource Key |
| `ApiAction` | 응답 처리용 Action Key |
| `ApiMessageClass` | 응답 파싱/처리 Handler 클래스 |
| `UseLevels` | 사용 Level 정보 |
| `Comment` | 설명용 주석 |

---

### 4.2 `UDxWebSocketSubsystem`

STOMP WebSocket 연결, 구독, 수신 메시지 라우팅을 담당합니다.

주요 역할:

- `WebSocketUrl`, Login, Passcode 기반 STOMP 연결
- 연결 성공 후 Topic 자동 구독
- 연결 오류/종료 시 재연결 시도
- Topic 종류에 따라 API Queue 또는 WebSocket Queue로 라우팅
- 수신 메시지를 GameThread에서 안전하게 Delegate 실행

Topic 라우팅 기준:

| 설정 | 처리 |
|---|---|
| `WebSocketTopics` | `UDxDataSubsystem::EnqueueWebSocketData` |
| `ApiTopics` | `UDxDataSubsystem::EnqueueApiData` |

---

### 4.3 `UDxDataSubsystem`

API/WebSocket에서 들어온 문자열 데이터를 큐에 쌓고, Handler를 통해 파싱/처리하는 Subsystem입니다.

처리 흐름:

```text
API/WebSocket 수신
        ↓
ApiDataQueue 또는 WebSocketDataQueue 적재
        ↓
Tick에서 제한 시간 내 Batch 추출
        ↓
Background Thread에서 ParseToStruct 실행
        ↓
GameThread에서 ProcessStructData 실행
```

주요 특징:

- `FTickableGameObject` 기반 Tick 처리
- API/WebSocket Queue 분리
- Batch 처리로 다량 메시지 대응
- `bApiProcessing`, `bWebSocketProcessing`으로 중복 작업 생성 방지
- `bIsShuttingDown`으로 종료 중 비동기 작업 보호
- Handler Cache를 사용하여 반복 생성 비용 감소

중요 규칙:

```text
ParseToStruct = 순수 파싱 전용
ProcessStructData = GameThread에서 최종 처리
```

`ParseToStruct` 내부에서는 Actor/Component 접근, Blueprint 호출, `NewObject`, `GetWorld()` 사용을 피해야 합니다.

---

### 4.4 `UDxObjectSubsystem`

Runtime Actor를 Category/ID 기준으로 등록하고 조회하는 공통 Registry입니다.

주요 함수:

```cpp
RegisterObject(...)
UnregisterObject(...)
FindObject(...)
GetAllObjects(...)
GetObjectIds(...)
ContainsObject(...)
GetObjectCount(...)
GetRegisteredObjectCount(...)
CompactInvalidObjects(...)
CompactAllInvalidObjects(...)
```

활용 예:

```cpp
UDxObjectSubsystem* ObjectSubsystem = GetGameInstance()->GetSubsystem<UDxObjectSubsystem>();
ObjectSubsystem->RegisterObject(TEXT("Sensor"), TEXT("LIDAR-001"), SensorActor);
AActor* FoundActor = ObjectSubsystem->FindObject(TEXT("Sensor"), TEXT("LIDAR-001"));
```

---

### 4.5 `UDxSearchSubsystem`

프로젝트별 검색 기능을 구현하기 위한 Base Subsystem입니다.

현재 구조:

- 기존 `uint8` 기반 검색 API 유지
- 신규 코드는 `FName` 기반 검색 권장
- `FDxSearchOptions`로 최대 결과 수, 대소문자 구분, 부분 일치 여부 설정
- 프로젝트별 Subsystem에서 `SearchByCategoryName` override 권장

권장 함수:

```cpp
PerformSearchByCategory(FName Category, const FString& SearchText, const FDxSearchOptions& Options);
```

---

### 4.6 `UDxWidgetSubsystem`

공통 Widget 생성, 닫기, 자식 Widget 관리, Z-order 처리를 담당합니다.

주요 역할:

- Actor 기반 Widget 열기
- Widget에서 자식 Widget 열기
- Widget 닫기
- 열린 Widget 목록 관리
- Widget을 앞으로 가져오기
- MainWidget / AddWidgetPanel 기반 UI 배치 지원

주요 함수:

```cpp
OpenWidget(...)
OpenWidgetFromWidget(...)
CloseWidget(...)
CloseWidgetFromWidget(...)
BringToFront(...)
GetOpenWidgets()
```

---

### 4.7 `UDxLogSubsystem`

Runtime 로그를 파일로 저장하는 Subsystem입니다.

주요 특징:

- 비동기 파일 쓰기
- 로그 버퍼링
- 날짜별 로그 파일 생성
- 파일 크기 기반 rotation
- 오래된 로그 삭제
- 종료 시 로그 flush
- Linux에서도 Unreal `IFileManager` 기반 디렉터리 생성 사용

기본 로그 경로 예:

```text
<LaunchDir>/Logs/CustomLogs
```

---

## 5. API / WebSocket Handler 구조

### 5.1 API Handler

`UApiMessage`는 API 응답을 파싱하고 처리하기 위한 기반 클래스입니다.

핵심 함수:

```cpp
ParseToStruct(const FString& JsonString)
ProcessStructData(const TSharedPtr<FApiDataBase>& Data)
```

흐름:

```text
API 응답 문자열
        ↓
ApiResource + ApiAction으로 Handler 검색
        ↓
ParseToStruct에서 데이터 구조화
        ↓
ProcessStructData에서 최종 반영
```

---

### 5.2 WebSocket Handler

`UTransactionCodeMessage`는 WebSocket 메시지를 TransactionCode 기준으로 처리하기 위한 기반 클래스입니다.

핵심 함수:

```cpp
ParseToStruct(const FString& JsonString)
ProcessStructData(const TSharedPtr<FTransactionCodeDataBase>& Data)
```

`FTransactionCodeStruct` Row는 TransactionCode 설명, Handler 클래스, 사용 Level, Comment를 관리합니다.

---

## 6. UI 구조

### 6.1 `UDxWidget`

공통 Widget 기반 클래스입니다.

주요 기능:

- 열림/닫힘/재시도 이벤트
- Close Button / Retry Button 자동 바인딩
- 부모/자식 Widget 관계 관리
- Theme 적용
- Player/Pawn 변경 감지
- Widget 클릭 시 앞으로 가져오기

Blueprint 확장 포인트:

```cpp
OpenWidgetAddLogic()
CloseWidgetAddLogic()
RetryWidget()
ApplyTheme()
```

---

### 6.2 Widget Config / Theme

| 클래스 | 역할 |
|---|---|
| `UDxWidgetConfigData` | Widget Flag와 Widget Class 매핑 |
| `UDxWidgetThemeData` | Widget 색상/스타일 Theme 관리 |
| `FDxWidgetInfo` | Widget 생성에 필요한 정보 구조 |

---

## 7. ActorComponent / Actor / Player 계층

| 영역 | 설명 |
|---|---|
| `DataSyncCompBase` | Actor 데이터 동기화를 위한 Base Component |
| `StatusVisualizerCompBase` | 상태 시각화를 위한 Base Component |
| `MechDriverCompBase` | 기계/장비 구동 관련 Base Component |
| `VirtualDistanceSensorComp` | 가상 거리 센서 Component |
| `InteractableActor` | UI 또는 상호작용 대상 Actor 기반 |
| `DxPlayerBase` | 프로젝트 공통 Player 기반 |
| `DxPlayerControllerBase` | PlayerController 공통 기반 |
| `DxLevelManagerBase` | Level 전환/관리 기반 |

이 계층은 프로젝트별 Actor/Component가 DTCore의 공통 기능을 상속하거나 조합해서 사용할 수 있도록 만들어진 기반입니다.

---

## 8. 설정 구조

`UDTCoreSettings`는 Project Settings 또는 Config 파일을 통해 DTCore 동작에 필요한 값을 주입합니다.

주요 설정:

| 설정 그룹 | 항목 |
|---|---|
| DataTable | `ApiDataTable`, `WebSocketDataTable`, `LevelDataTable`, `ShipObjectNameDataTable` |
| Network/API | `BaseApiUrl`, `LocalApiUrl`, `TestApiUrl`, `ProdApiUrl` |
| Network/WebSocket | `WebSocketUrl`, `WebSocketLogin`, `WebSocketPasscode` |
| Topics | `WebSocketTopics`, `ApiTopics` |
| UI | `DefaultWidgetTheme` |
| Object | `ObjectBPClasses` |

---

## 9. Runtime Config / Game.ini Override 요약

패키징 후 API/WebSocket 접속 정보를 바꾸기 위해 `DTCoreRuntimeConfig`가 `Game.ini` 후보 경로를 확인합니다.

우선순위:

```text
1. 외부 편집용 Game.ini의 [DTCoreRuntimeOverride]
2. 외부 편집용 Game.ini의 [/Script/DTCore.DTCoreSettings]
3. Unreal GGameIni의 [DTCoreRuntimeOverride]
4. Unreal GGameIni의 [/Script/DTCore.DTCoreSettings]
5. UDTCoreSettings 기본값
6. 코드 fallback
```

후보 경로:

```text
FPaths::ProjectDir() / Config / Game.ini
FPaths::LaunchDir() / Config / Game.ini
FPaths::LaunchDir() / ../../Config/Game.ini
FPaths::LaunchDir() / <ProjectName> / Config / Game.ini
```

템플릿:

```ini
[DTCoreRuntimeOverride]
BaseApiUrl=
LocalApiUrl=
TestApiUrl=
ProdApiUrl=
WebSocketUrl=
WebSocketLogin=
WebSocketPasscode=
```

빈 값은 override로 처리하지 않습니다. 필요한 항목만 채워서 사용하면 됩니다.

---

## 10. 설치 및 적용 순서

```text
1. YourProject/Plugins/DTCore 위치에 플러그인 배치
2. Generate Visual Studio project files 실행
3. Unreal Editor에서 DTCore Plugin 활성화 확인
4. 프로젝트 빌드
5. Project Settings > Plugins > DTCore Settings 설정
6. 필요한 DataTable / WidgetConfig / ThemeData 연결
7. 패키징 후 접속 정보 변경이 필요하면 Game.ini override 사용
```

---

## 11. 패키징 테스트 체크리스트

```text
1. 프로젝트가 정상 빌드되는지
2. ApiDataTable / WebSocketDataTable이 정상 로드되는지
3. WebSocket 연결 로그가 정상 출력되는지
4. API 응답이 UDxDataSubsystem으로 전달되는지
5. WebSocket 메시지가 Topic 설정에 따라 올바른 Queue로 들어가는지
6. ParseToStruct가 Background Thread에서 안전하게 동작하는지
7. Widget 열기/닫기/자식 Widget 관리가 정상 동작하는지
8. Game.ini override 값을 채웠을 때 접속 정보가 바뀌는지
9. 빈 override 값은 기본값으로 fallback 되는지
10. Windows/Linux 패키징에서 Game.ini 후보 경로가 정상 동작하는지
```

---

## 12. 프로젝트 독립성 규칙

DTCore는 여러 프로젝트에서 재사용하기 위한 공통 플러그인입니다.

지켜야 할 원칙:

```text
특정 프로젝트 모듈에 의존하지 않기
특정 프로젝트명 경로 하드코딩하지 않기
운영 서버 URL을 cpp에 직접 입력하지 않기
프로젝트별 Asset, Map, Widget, DataTable은 소비 프로젝트에 두기
환경별 값은 UDTCoreSettings 또는 Game.ini에서 관리하기
ParseToStruct 내부에서는 UObject/GameThread 전용 작업을 하지 않기
```

---

## 13. 주의사항

- `Game.ini` 수정 후 실행 중 즉시 반영되지는 않습니다. 클라이언트 재시작을 권장합니다.
- Linux 패키징은 실행 방식에 따라 `LaunchDir`가 달라질 수 있으므로 실제 생성 경로를 확인해야 합니다.
- `ParseToStruct()` 내부에서 Actor/Component/Blueprint 접근을 하면 스레드 안정성 문제가 생길 수 있습니다.
- Public Header에 불필요한 의존성이 늘어나면 DTCore를 사용하는 프로젝트의 빌드 의존성도 커질 수 있습니다.
- Base 클래스(`DataSyncCompBase`, `StatusVisualizerCompBase`, `MechDriverCompBase`, `DxLevelManagerBase`, `InteractableActor`)는 기본적으로 Tick이 꺼진 상태로 시작합니다(`bStartWithTickEnabled = false`). Tick이 필요한 파생 클래스/Blueprint는 `SetComponentTickEnabled(true)` 또는 `SetActorTickEnabled(true)`를 직접 호출해야 합니다.
