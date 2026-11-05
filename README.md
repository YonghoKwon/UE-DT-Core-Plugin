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
| `Core` | 주요 Subsystem, 데이터 채널(`UDxDataChannelBase`, `UDxStompChannel`), Settings, GameMode/GameInstance, Runtime 설정 유틸리티 |
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
- 설정 가능한 요청 timeout 및 연결 실패/5xx 재시도 (기본: 엔진 timeout, 3회, 초기 1초)
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
- 연결 오류/종료 시 지수 백오프 재연결 (`MaxReconnectAttempts`로 최대 횟수 제한, 0 = 무제한. 대기 시간은 `Reconnect*` 설정)
- 등록된 Topic 수신 데이터를 `UDxDataSubsystem` Queue로 전달
- 수신 메시지를 GameThread에서 안전하게 Delegate 실행
- `GetConnectionState`, `GetReconnectAttemptCount`, `GetLastSubscribeFailureCount` 조회 및 `OnConnectionStateChanged` 통지
- 선택적인 구독 receipt timeout (`SubscribeReceiptTimeoutSeconds`, 기본 0 = 비활성)
- `PublishMessage`(UTF-8 문자열) / `PublishBinaryMessage`(바이트 그대로)로 STOMP SEND. 전송 연결 상태(`Subscribing`/`Ready`)가 아니면 보내지 않고 `false`를 반환하며 `GetPublishDroppedCount`에 누적. 완료 callback을 넘기면 Broker receipt를 요청하고, 결과 전에 연결이 바뀌면 결과를 전달하지 않음. Body는 로그에 남기지 않음

Topic 라우팅 기준:

| 설정 | 처리 |
|---|---|
| `WebSocketTopics` | `UDxDataSubsystem::EnqueueWebSocketData` |
| `ApiTopics` | `UDxDataSubsystem::EnqueueWebSocketData` |

`ApiTopics`는 WebSocket으로 받는 Api 방식 데이터를 일반 WebSocket 데이터와 구분하기 위한 목록입니다. 두 목록 모두 WebSocket Queue로 전달되고 `MESSAGE_ID` 핸들러가 처리하며, API Queue로는 보내지 않습니다(소유자 확인). 토픽별 첫 수신 때 `ReceivedMessage: RouteType=<WebSocket|Api>, Topic=<토픽>, Bytes=<길이>` 로그를 한 번 남겨 어느 구분으로 들어왔는지 확인할 수 있습니다. 수신량이 많은 토픽에서 로그가 커지지 않도록 같은 토픽은 다시 기록하지 않습니다(Subsystem 수명 동안).

#### 데이터 채널 (선택)

기본 연결과 별개인 연결이 필요할 때 사용합니다(예: 같은 Broker라도 대용량·고빈도 메시지를 분리해 작은 실시간 메시지가 밀리지 않게 함). 채널을 등록하거나 `DataChannels`를 설정하지 않으면 기존 동작은 바뀌지 않습니다.

**메커니즘은 DTCore, 토픽 정책은 프로젝트가 소유합니다.**

| 소유 | 내용 |
|---|---|
| DTCore | 연결·재연결·구독 유지, 상태와 집계, 채널 등록, `DataChannels`의 연결 설정 |
| 사용 프로젝트·플러그인 | 어떤 채널에 어떤 토픽을 쓰는지, 메시지 형식, 대용량 전송 구현, 채널별 업무 처리 |

DTCore에는 특정 토픽 이름이나 장치 타입을 넣지 않습니다. `DataChannels`에도 토픽 항목은 없습니다.

```text
UDxDataChannelBase (추상)
├─ UDxStompChannel            별도 STOMP 연결
└─ UDxDefaultWebSocketChannel 기존 기본 연결의 adapter ("Default")
```

사용 예:

```cpp
UDxWebSocketSubsystem* WebSocket = GetGameInstance()->GetSubsystem<UDxWebSocketSubsystem>();
UDxDataChannelBase* Channel = WebSocket->FindChannel(TEXT("telemetry"));
if (Channel)
{
	Channel->PublishText(TEXT("topic.example.telemetry"), Body, {}, FSTOMPRequestCompleted());
}
```

```ini
[/Script/DTCore.DTCoreSettings]
+DataChannels=(ChannelName="telemetry",bAutoConnect=True)
```

`ChannelClass`가 비어 있으면 `UDxStompChannel`을 만듭니다. `Url`이 비어 있으면 기본 연결과 같은 서버에 별도 연결을 엽니다.

**DTDevice와의 관계:** [UE-DT-Device-Plugin](https://github.com/YonghoKwon/UE-DT-Device-Plugin)이 이 채널을 상속해 쓰는 사용자입니다. 의존 방향은 DTDevice → DTCore이며 DTCore는 DTDevice를 참조하지 않습니다.

| DTDevice 클래스 | DTCore 기반 | 역할 |
|---|---|---|
| `UDxDeviceSensorStompChannel` | `UDxStompChannel` | JSON·수동 binary 전송(`sensor` 채널) |
| `UDxDeviceSensorBulkChannel` | `UDxDataChannelBase` | DTDevice의 Raw TCP worker를 쓰는 대용량 전송(`sensor-bulk` 채널) |
| `UDxDeviceSensorChannelSubsystem` | `RegisterChannel` 사용 | 두 채널의 등록과 토픽 구독 관리 |
| `UDeviceDataChannelSettings` | 없음(DTDevice 자체 설정) | 채널별 토픽 정책 |

자세한 내용은 DTDevice의 `Docs/DataChannels.md`를 참고하세요.

API 요약:


- `UDxDataChannelBase`(추상): `Connect` / `Disconnect` / `PublishText` / `PublishBinary` / `Subscribe` / `Unsubscribe` / `GetStatus`. 파생 클래스용 보호 훅: 재연결 정책 조회(`GetReconnectDelaySeconds`, `GetMaxReconnectAttempts`, `ScheduleReconnect`), 상태 갱신(`UpdateState`, `SetLastError`), 집계(`RecordPublished` / `RecordDropped` / `RecordBackpressure`), `CheckMessageSize`, `ResolveUrl` / `ResolveCredentials`
- 스레드 규칙: 제어 API와 `OnStateChanged` broadcast는 GameThread. `GetThreadPolicy()`가 `GameThread`인 채널은 콜백을 GameThread에서 호출하고, `Worker`인 채널은 작업 스레드에서 처리한 뒤 GameThread에는 상태·집계만 전달. 보호 훅은 어느 스레드에서 불러도 안전
- `UDxStompChannel`: 기본 연결과 별도 인스턴스의 STOMP 연결. URL을 비우면 기본 `WebSocketUrl`과 같은 서버에 붙습니다. 구독 키는 재연결 후에도 유지되고 구독은 재연결마다 다시 요청합니다. 대용량 파생 클래스는 `CreateStompClient` / `SendFrame` / `OnFrameReceived`만 바꿔 끼웁니다
- 등록: `RegisterChannel(Name, Channel)`(같은 이름의 `DataChannels` 설정 사용) / `RegisterChannelWithSettings` / `UnregisterChannel` / `FindChannel` / `GetChannelNames` / `GetAllChannelStatus`. None·`Default`·중복 이름·이미 등록된(또는 해제된) 채널은 거부. GameInstance 종료 시 모든 채널을 먼저 종료
- `FindChannel("Default")`: 기존 기본 연결을 감싼 adapter(처음 호출 시 생성). 기존 공개 함수로 전달만 하며 `Subscribing`·`Ready`는 `Connected`로 보입니다. 해제·종료 시 기본 연결을 끊지 않습니다
- 상태(`FDxDataChannelStatus`): 연결 상태, 마지막 오류, 보낸 수, 보내지 못한 수(미연결·크기 초과·종료 후), 밀림 수(파생 채널 집계), 재연결 시도 수
- 공통 규약(`UDxDataChannelLibrary`, 새 채널 코드 전용): `MakeDestination` / `IsValidDestination`(소문자 영숫자·`-`·`_` 조각을 `.`로 연결), `MakeStandardHeaders`(`dx-schema-version`, `dx-message-id`, `dx-sent-at` UTC ISO-8601). receipt 시간 초과는 채널 설정 `ReceiptTimeoutSeconds`

상태는 `Disconnected → Connecting → Subscribing → Ready`이며 오류/종료 시 `Reconnecting`, 횟수 제한 도달 시 `GaveUp`으로 전환합니다. 상태가 실제로 바뀔 때에만 새 상태와 사유를 통지하며, 같은 재시도 포기 상태를 반복 통지하지 않습니다. 수동 Disconnect는 `Disconnected`이고 종료 중에는 상태 이벤트를 broadcast하지 않습니다. 조회·연결 제어 API는 GameThread에서 사용합니다.

`Ready`는 기존 `OnConnected`와 같이 **모든 구독 완료 callback 회수**를 뜻합니다. 모든 구독 성공을 보장하지 않으며 Topic 0개이면 즉시 완료합니다. 실패 수는 현재/마지막 구독 batch의 `bSuccess=false` 및 timeout 미회수 수 합계입니다. 연결 성공 시 재시도 횟수는 기존처럼 0으로 초기화합니다.

timeout을 명시적으로 켜면 batch 시작부터 지정 시간이 지나도 미회수인 receipt를 실패로 집계하고 `OnConnected`를 한 번 발생시킵니다. 이미 회수한 실패와 합산하며 늦은 원래 callback·중복 callback은 재집계하지 않습니다. 재연결·Disconnect·종료 시 generation과 타이머를 무효화합니다. 기본값 0에서는 기존처럼 callback이 없으면 계속 대기합니다.

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
- `GetStatistics()`로 원자 카운터 snapshot (`FDxDataStatistics`) 조회
- 처리 완료(API/WS), JSON 파싱 실패, dispatch 필드 누락, 미등록 키, 큐 대기(API/WS) 집계
- 미등록 키는 기존처럼 버리며 API/WS별 키 첫 1회만 백그라운드 `UE_LOG` 경고

완료 수는 GameThread의 `ProcessStructData` 호출이 반환된 건수입니다. JSON 실패는 공통 JSON 파서 실패 수이며 개별 Handler의 구조체 변환 실패와 다릅니다. 누락 수는 `meta` 또는 resource/action이 비어 있거나 `MESSAGE_ID`가 없거나 빈 경우입니다. 미등록 키 수는 dispatch 필드가 채워졌지만 맵에 없는 메시지 수입니다. 큐 대기는 batch로 추출되기 전 메시지만 포함하며 각 카운터는 개별 원자 읽기입니다. 전체 snapshot의 한 시점 일관성이나 큐 처리율을 보장하지 않습니다. 기존 내부 `TotalProcessedCount`의 WS batch 결과 계수 의미는 유지합니다.

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
- 중복 조회·BringToFront·hover 조회 시 화면 트리에서 빠진 관리 항목 정리
- Main 교체 시 제거된 트리의 항목만 정리하며 닫기 후처리를 추가 호출하지 않음
- `FindOpenWidget`/`FocusOrOpenWidget`: Actor의 위젯이 이미 열려 있으면 앞으로 가져오고 없으면 열기. `Click()` 기본 동작은 그대로이며 프로젝트가 선택적으로 사용
- `UDxWidget::NavigateTo`/`NavigateBack`: 같은 위젯 안의 탭·하위 화면 이동과 뒤로 가기. 실제 전환은 자식 클래스의 `HandleNavigateTo`가 구현(기본은 처리 안 함). 기록은 최대 32개이며 위젯을 닫으면 초기화

`SwitchUIMode`는 DataTable 순회 중 **마지막 일치 행**을 적용합니다. 이름순 정렬을 보장하지 않으므로 모드별 한 행을 권장하며, 중복 일치 시 경고를 한 번 남깁니다. Main 트리에 정적으로 등록한 Widget은 트리가 viewport에 붙어 있는 동안 유지합니다.

주요 함수:

```cpp
OpenWidget(...)
OpenWidgetFromWidget(...)
CloseWidget(...)
CloseWidgetFromWidget(...)
BringToFront(...)
FindOpenWidget(...)
FocusOrOpenWidget(...)
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

`LogDirectory`가 비어 있으면 위 기본 경로를 유지합니다. 절대 경로는 그대로, 상대 경로는 `FPaths::LaunchDir()` 기준으로 해석하며 Initialize 때 고정합니다. `LogRetentionDays` 기본값은 7이며 최소 1일입니다(0 이하를 넣으면 1일로 제한해 당일 로그를 지우지 않습니다). 날짜별 `DxLog_YYYY-MM-DD.log`, 50MB 및 suffix 1~20 rotation 규칙을 유지합니다. 진단 로그 내용(HTTP 오류 본문·요청 URL·파싱 실패 payload 앞 256자·구독 실패 사유)은 기존과 같습니다.

---

### 4.8 `UDxProcessSubsystem`

ComponentId 기준으로 ActorComponent를 등록/조회하는 Registry Subsystem입니다.

주요 함수:

```cpp
RegisterComponent(...)
UnregisterComponent(...)
FindComponent(...)
```

`FindComponent`는 조회 시 무효화된 Component를 자동으로 정리합니다.

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
HandleNavigateTo(FName SubTarget)
```

같은 위젯 안의 탭·하위 화면 이동은 `NavigateTo`, `NavigateBack`, `CanNavigateBack`을 사용합니다. 실제 화면 전환은 자식 클래스의 `HandleNavigateTo`가 구현합니다. 첫 화면을 기록하려면 `OpenWidgetAddLogic`에서 `NavigateTo(첫 화면, false)`를 호출합니다. 처리하지 않으면 `false`를 반환하며, `NavigateBack`이 실패해도 기록은 유지됩니다.

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
| `InteractableActor` | UI 또는 상호작용 대상 Actor 기반. `HighlightStencilValue`(100), `ErrorStencilValue`(254) |
| `DxPlayerBase` | 프로젝트 공통 Player 기반 |
| `DxPlayerControllerBase` | PlayerController 공통 기반. `PriorityTraceChannel`, `TraceObjectTypes`로 입력 대상 설정 |
| `DxLevelManagerBase` | Level 전환/관리 기반 |

이 계층은 프로젝트별 Actor/Component가 DTCore의 공통 기능을 상속하거나 조합해서 사용할 수 있도록 만들어진 기반입니다.

---

## 8. 설정 구조

`UDTCoreSettings`는 Project Settings 또는 Config 파일을 통해 DTCore 동작에 필요한 값을 주입합니다.

주요 설정:

| 설정 그룹 | 항목 |
|---|---|
| DataTable | `ApiDataTable`, `WebSocketDataTable`, `LevelDataTable` |
| Legacy 프로젝트 DataTable | `CctvUrlDataTable`, `ShipObjectNameDataTable` (이름·Config 키 유지, 내부 미사용, 신규 사용 비권장) |
| Network/API | `BaseApiUrl`, `LocalApiUrl`, `TestApiUrl`, `ProdApiUrl` |
| HTTP 정책 | `HttpRequestTimeoutSeconds` = 0 (엔진 기본, `SetTimeout` 미호출), `HttpMaxAttempts` = 3, `HttpInitialRetryDelaySeconds` = 1.0 |
| Network/WebSocket | `WebSocketUrl`, `WebSocketLogin`, `WebSocketPasscode`, `MaxReconnectAttempts` = 10 (0 = 무제한), `ReconnectInitialDelaySeconds` = 1.0, `ReconnectMaxDelaySeconds` = 60.0, `ReconnectBackoffMultiplier` = 2.0, `SubscribeReceiptTimeoutSeconds` = 0 (비활성) |
| Network/DataChannel | `DataChannels` = 빈 배열(기본 연결만). 항목: `ChannelName`, `ChannelClass`(빈 값 = `UDxStompChannel`), `Url`(빈 값 = 기본 URL), `UrlOverrideKey` / `LoginOverrideKey` / `PasscodeOverrideKey`(Runtime Override 키 이름, 비밀값은 설정에 두지 않음), `bUseDefaultCredentials` = true, `bAutoConnect` = true, `MaxReconnectAttempts` = -1(기본값 사용), `Reconnect*` = 0(기본값 사용), `MaxMessageBytes` = 0(제한 없음), `ReceiptTimeoutSeconds` = 0(비활성) |
| Topics | `WebSocketTopics`, `ApiTopics` (두 목록 모두 `MESSAGE_ID` 라우팅) |
| Log | `LogDirectory` = 빈 문자열 (`LaunchDir/Logs/CustomLogs`), `LogRetentionDays` = 7 |
| UI | `DefaultWidgetTheme` |
| Object | `ObjectBPClasses` |

---

## 9. Runtime Config / Game.ini Override 요약

패키징 후 API/WebSocket 접속 정보를 바꾸기 위해 `DTCoreRuntimeConfig`가 `Game.ini`와 플랫폼별 ini 후보 경로를 확인합니다.

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

그다음 같은 네 위치의 `Config/Windows/WindowsGame.ini`(Linux는 `Config/Linux/LinuxGame.ini`)를 같은 순서로 확인합니다. 앞선 파일에 값이 있으면 그 값을 씁니다.

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

빈 값은 override로 처리하지 않습니다. 필요한 항목만 채워서 사용하면 됩니다. 데이터 채널의 `UrlOverrideKey`, `LoginOverrideKey`, `PasscodeOverrideKey`에 적은 키도 같은 경로로 읽습니다. 실행 시 첫 후보 경로에 빈 템플릿을 만들거나 빠진 키를 채웁니다.

### 패키지의 설정 파일 동작

패키지에서는 pak에 포함된 설정이 같은 경로의 일반 파일을 가립니다. 현재 동작은 다음과 같습니다.

| 패키지 옆에서 바꾼 파일과 항목 | DTCore 반영 여부 |
|---|---|
| `Config/DefaultGame.ini`의 모든 항목 | 반영 안 됨. DTCore는 이 파일을 직접 읽지 않으며 pak의 값이 우선합니다 |
| `Config/Game.ini` 등 위 후보 경로의 접속 키(템플릿 7개, 채널 override 키) | 반영됨. 단 같은 파일이 pak에 들어 있으면 그 파일이 가립니다 |
| 위 후보 경로의 `WebSocketTopics`, `ApiTopics`, `DataChannels`, `Reconnect*`, HTTP, 로그 설정 | 반영 안 됨. 이 값은 패키징 시 설정(pak)에서 옵니다 |

- `Game.ini`가 pak에 들어가지 않게 하려면 사용 프로젝트의 `DefaultGame.ini` `[Staging]`에 `+DisallowedConfigFiles=<프로젝트>/Config/Game.ini`를 추가합니다.
- `DataChannels`나 토픽 목록을 배포 후 바꿔야 하면 사용 쪽에서 직접 읽어야 합니다. DTDevice는 채널·토픽과 일부 연결 항목을 패키지 옆 `DefaultGame.ini`와 플랫폼 ini에서 직접 읽습니다. 이 동작은 DTDevice에만 있으며 API 주소 등 DTCore의 다른 항목에는 적용되지 않습니다.

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
5. 두 Topic 목록 모두 WebSocket Queue에서 MESSAGE_ID 핸들러로 전달되는지
6. ParseToStruct가 Background Thread에서 안전하게 동작하는지
7. Widget 열기/닫기/자식 Widget 관리가 정상 동작하는지
8. Game.ini override 값을 채웠을 때 접속 정보가 바뀌는지 (Game.ini가 pak에 들어가지 않았는지 포함)
9. 빈 override 값은 기본값으로 fallback 되는지
10. Windows/Linux 패키징에서 Game.ini 후보 경로가 정상 동작하는지
```

---

자동화 테스트는 `Source/DTCore/Private/Tests`에 있으며 Editor에서 `Automation RunTests DTCore`로 실행합니다. 정상 실행은 fake STOMP·HTTP RequestProbe를 사용해 외부 Broker/HTTP가 필요하지 않습니다. 테스트 구현은 `WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR`, Editor 의존 모듈은 Private에만 추가합니다. CI는 없습니다. 최소 host 구성과 명령은 [QUALITY_90.md](docs/QUALITY_90.md)를 참고하세요.

일반 자동화는 29개입니다(`DTCore.Contract` 11, `Quality` 7, `Functional` 4, `Channel` 7). 데이터 채널 추가 커밋 `dd7bae7`에서 compat host로 Editor·Shipping 빌드와 29개 통과(NullRHI, `DataStatistics` 1개는 의도된 경고 포함)를 확인했습니다. 이 결과는 실제 Broker 연결이나 대용량 송신을 검증한 것이 아닙니다. 사용 프로젝트에서의 확인 결과는 해당 프로젝트 문서를 따릅니다.

실제 Broker 60분 시험은 `DTCORE_STOMP_SOAK=1`에서만 목록에 등록되는 `DTCore.Soak.Stomp60Minutes`입니다. [Tools/RunQuality90Soak.ps1](Tools/RunQuality90Soak.ps1)이 별도 Docker compose 프로젝트·포트·복사된 설정을 사용하고 10분 간격 5회 stop/start, 상태·수신·메모리를 기록한 뒤 컨테이너·네트워크·볼륨을 정리합니다. 일반 자동화 실행의 성공 수에는 이 opt-in 시험을 넣지 않습니다.

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
토픽 이름, 메시지 형식, 장치별 전송 정책을 DTCore에 넣지 않기 (메커니즘은 DTCore, 토픽 정책은 프로젝트)
```

---

## 13. 주의사항

- `Game.ini` 수정 후 실행 중 즉시 반영되지는 않습니다. 클라이언트 재시작을 권장합니다.
- 패키지 옆 `Config/DefaultGame.ini`를 고쳐도 DTCore 설정은 바뀌지 않습니다. 9절의 "패키지의 설정 파일 동작"을 확인하세요.
- Linux 패키징은 실행 방식에 따라 `LaunchDir`가 달라질 수 있으므로 실제 생성 경로를 확인해야 합니다.
- `ParseToStruct()` 내부에서 Actor/Component/Blueprint 접근을 하면 스레드 안정성 문제가 생길 수 있습니다.
- Public Header에 불필요한 의존성이 늘어나면 DTCore를 사용하는 프로젝트의 빌드 의존성도 커질 수 있습니다.
- Base 클래스(`DataSyncCompBase`, `StatusVisualizerCompBase`, `MechDriverCompBase`, `DxLevelManagerBase`, `InteractableActor`)는 기본적으로 Tick이 꺼진 상태로 시작합니다(`bStartWithTickEnabled = false`). Tick이 필요한 파생 클래스/Blueprint는 `SetComponentTickEnabled(true)` 또는 `SetActorTickEnabled(true)`를 직접 호출해야 합니다.

---

## 14. 문서 안내

| 문서 | 내용 | 상태 |
|---|---|---|
| `README.md` | 현재 구조와 동작 | 현재 기준 |
| [AGENTS.md](AGENTS.md) | AI 에이전트 작업 규칙 | 현재 기준 |
| [docs/QUALITY_90.md](docs/QUALITY_90.md) | 품질 보강 검증 기록, compat host 명령 | 해당 시점 기록 |
| [docs/MINIMAL_MAIN_MIGRATION.md](docs/MINIMAL_MAIN_MIGRATION.md) | 최소 안정화 계약 | 해당 시점 기록 |
| [docs/ANALYSIS.md](docs/ANALYSIS.md) | 코드 분석 보고서 | 해당 시점 기록 |
| [docs/수정요청.md](docs/수정요청.md) | AI 수정 요청 프롬프트 | 해당 시점 기록, 일부 항목 완료 |
