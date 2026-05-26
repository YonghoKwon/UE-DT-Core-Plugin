# UE-DT-Core-Plugin

`UE-DT-Core-Plugin`은 Unreal Engine 기반 디지털 트윈 프로젝트에서 공통으로 사용할 수 있는 Runtime Plugin입니다.

API 통신, WebSocket/STOMP 수신, 데이터 큐 처리, 객체 등록/검색, 로그, UI 기반 기능, 센서 유틸리티를 여러 프로젝트에서 재사용할 수 있도록 분리하는 것을 목표로 합니다.

---

## 1. 핵심 목적

| 목적 | 설명 |
|---|---|
| 공통 기능 분리 | API, WebSocket, 로그, 객체 관리 등 반복 기능을 공통화합니다. |
| 프로젝트 독립성 | 특정 프로젝트 모듈이나 특정 프로젝트 경로에 의존하지 않습니다. |
| 패키징 후 설정 변경 | 다시 패키징하지 않고 API/WebSocket 접속 정보를 바꿀 수 있습니다. |
| 실시간 데이터 처리 | 수신 데이터를 큐에 넣고 배치/비동기 방식으로 처리합니다. |
| 확장성 | 프로젝트별 DataTable, Handler, Widget, Search 구현체를 주입해서 사용합니다. |

---

## 2. 주요 기능

| 기능 | 담당 클래스 | 설명 |
|---|---|---|
| API 통신 | `UDxApiSubsystem` | DataTable 기반 API 요청 및 응답 처리 |
| WebSocket/STOMP | `UDxWebSocketSubsystem` | STOMP 연결, 구독, 재연결, 메시지 라우팅 |
| 데이터 처리 | `UDxDataSubsystem` | API/WebSocket 데이터를 큐에 넣고 비동기 파싱 후 GameThread에서 처리 |
| 객체 관리 | `UDxObjectSubsystem` | Actor를 Category/ID 기준으로 등록하고 조회 |
| 검색 기반 | `UDxSearchSubsystem` | 프로젝트별 검색 기능 구현을 위한 Base Subsystem |
| 로그 | `UDxLogSubsystem` | 런타임 로그 저장, rotation, 오래된 로그 정리 |

---

## 3. 설치 방법

UE 프로젝트의 `Plugins` 폴더 아래에 이 저장소를 복사합니다.

```text
YourProject/Plugins/DTCore
```

이후 아래 순서로 진행합니다.

```text
1. Generate Visual Studio project files 실행
2. Unreal Editor에서 DTCore Plugin 활성화 확인
3. 프로젝트 빌드
4. Project Settings 또는 Game.ini에서 DTCore 설정
```

---

## 4. 기본 설정

설정 클래스는 `UDTCoreSettings`입니다.

Project Settings 경로:

```text
Edit > Project Settings > Plugins > DTCore Settings
```

주요 설정 항목:

| 항목 | 설명 |
|---|---|
| `ApiDataTable` | API 요청 정의 DataTable |
| `WebSocketDataTable` | WebSocket TransactionCode 정의 DataTable |
| `LevelDataTable` | Level 관련 DataTable |
| `ObjectBPClasses` | Object Category별 Blueprint Class Map |
| `WebSocketUrl` | 기본 WebSocket URL |
| `WebSocketLogin` | WebSocket Login 값 |
| `WebSocketPasscode` | WebSocket Passcode 값 |
| `BaseApiUrl` | 기본 API URL |
| `LocalApiUrl` | Local API URL |
| `TestApiUrl` | Test API URL |
| `ProdApiUrl` | Production API URL |
| `WebSocketTopics` | WebSocket 데이터로 처리할 Topic 목록 |
| `ApiTopics` | API 데이터로 처리할 Topic 목록 |
| `DefaultWidgetTheme` | 기본 Widget Theme Data |

---

## 5. 패키징 후 Game.ini Runtime Override

패키징 후에도 API URL, WebSocket URL, WebSocket 계정 정보를 바꿀 수 있도록 `Game.ini` override를 지원합니다.

즉, 클라이언트를 다시 패키징하지 않고 아래 값을 바꿔가며 테스트할 수 있습니다.

```text
API URL
WebSocket URL
WebSocket Login
WebSocket Passcode
```

---

## 6. Runtime Override 우선순위

설정값은 아래 순서로 읽습니다.

| 우선순위 | 위치 | 설명 |
|---:|---|---|
| 1 | 외부 편집용 `Game.ini`의 `[DTCoreRuntimeOverride]` | 패키징 후 직접 수정하기 위한 최우선 설정 |
| 2 | 외부 편집용 `Game.ini`의 `[/Script/DTCore.DTCoreSettings]` | Unreal Settings 섹션 직접 override |
| 3 | Unreal `GGameIni`의 `[DTCoreRuntimeOverride]` | Unreal Config Cache 기반 override |
| 4 | Unreal `GGameIni`의 `[/Script/DTCore.DTCoreSettings]` | 기본 UDeveloperSettings 섹션 |
| 5 | `UDTCoreSettings` 기본값 | Project Settings 또는 DefaultGame.ini 값 |
| 6 | 코드 fallback 값 | 최후의 기본값 |

빈 값은 override로 처리하지 않습니다.

```ini
LocalApiUrl=
```

값이 실제로 들어간 경우에만 override 됩니다.

```ini
LocalApiUrl=http://<api-server>:8090
```

---

## 7. 외부 Game.ini 후보 경로

패키징 방식에 따라 Unreal의 `LaunchDir`, `ProjectDir`가 달라질 수 있습니다.

그래서 DTCore는 하나의 경로만 보지 않고 아래 후보 경로들을 순서대로 확인합니다.

```text
FPaths::ProjectDir() / Config / Game.ini
FPaths::LaunchDir() / Config / Game.ini
FPaths::LaunchDir() / ../../Config/Game.ini
FPaths::LaunchDir() / <ProjectName> / Config / Game.ini
```

Windows 패키징 예시:

```text
Windows/<ProjectName>/Config/Game.ini
Windows/<ProjectName>/Binaries/Win64/../../Config/Game.ini
Windows/Config/Game.ini
```

Linux 패키징 예시:

```text
Linux/<ProjectName>/Config/Game.ini
Linux/<ProjectName>/Binaries/Linux/../../Config/Game.ini
```

파일 위치가 헷갈리면 패키징 폴더에서 확인합니다.

Windows PowerShell:

```powershell
Get-ChildItem -Recurse -Filter Game.ini
```

Linux:

```bash
find . -path "*/Config/Game.ini" -print
```

---

## 8. 자동 생성되는 Game.ini 템플릿

최초 실행 시 DTCore는 외부 편집용 `Game.ini`에 아래 섹션과 키가 없으면 생성/보강합니다.

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

값은 의도적으로 비워둡니다. 필요한 항목만 채워서 사용하면 됩니다.

---

## 9. Game.ini 작성 예시

```ini
[DTCoreRuntimeOverride]
LocalApiUrl=http://<local-api-server>:8090
TestApiUrl=http://<test-api-server>:8000
WebSocketUrl=ws://<websocket-server>:61616
WebSocketLogin=<login>
WebSocketPasscode=<passcode>
```

일부 값만 바꿔도 됩니다.

WebSocket만 바꾸는 예시:

```ini
[DTCoreRuntimeOverride]
WebSocketUrl=ws://<websocket-server>:61616
WebSocketLogin=<login>
WebSocketPasscode=<passcode>
```

API만 바꾸는 예시:

```ini
[DTCoreRuntimeOverride]
LocalApiUrl=http://<local-api-server>:8090
TestApiUrl=http://<test-api-server>:8000
```

수정 후에는 클라이언트를 재시작하는 것을 권장합니다.

---

## 10. API URL 선택 방식

API 요청은 DataTable의 `EApiType`에 따라 URL을 선택합니다.

| ApiType | 우선 적용 Key |
|---|---|
| `Local` | `LocalApiUrl` |
| `Test` | `TestApiUrl` |
| `Prod` | `ProdApiUrl` |
| 기타/default | `BaseApiUrl` |

예를 들어 DataTable Row의 `ApiType`이 `Test`라면 우선 `TestApiUrl`을 찾습니다.

값이 비어 있으면 `UDTCoreSettings`의 `TestApiUrl`, 그다음 `BaseApiUrl` fallback을 사용합니다.

---

## 11. WebSocket 설정 방식

WebSocket은 아래 값을 사용합니다.

| Key | 설명 |
|---|---|
| `WebSocketUrl` | STOMP WebSocket 접속 URL |
| `WebSocketLogin` | STOMP login header |
| `WebSocketPasscode` | STOMP passcode header |

---

## 12. API 통신 흐름

```text
API DataTable Row 선택
        ↓
ApiType 기준 URL 결정
        ↓
HTTP Method 결정
        ↓
HTTP 요청 수행
        ↓
응답 성공 시 UDxDataSubsystem::EnqueueApiData 로 전달
```

주요 함수:

```cpp
DxHttpCall(...)
DxRequestApi(...)
DxRequestApiWithParameter(...)
```

---

## 13. WebSocket 통신 흐름

```text
WebSocket URL / Login / Passcode 결정
        ↓
STOMP Client 생성
        ↓
Connect
        ↓
Topic 구독
        ↓
메시지 수신
        ↓
Topic 종류에 따라 UDxDataSubsystem으로 전달
```

Topic 처리 기준:

| Topic 설정 | 전달 대상 |
|---|---|
| `WebSocketTopics` | `EnqueueWebSocketData()` |
| `ApiTopics` | `EnqueueApiData()` |

---

## 14. 데이터 처리 흐름

```text
API / WebSocket 수신
        ↓
UDxDataSubsystem Queue 적재
        ↓
Tick에서 일정 시간 예산 안에서 Batch 추출
        ↓
Background Thread에서 JSON Parsing
        ↓
GameThread에서 ProcessStructData 실행
```

적용된 안정성 처리:

```text
Tick마다 백그라운드 작업이 중복 생성되지 않도록 guard 적용
종료 중에는 큐 처리 중단
Deinitialize 이후 백그라운드 작업이 UObject를 건드리지 않도록 보호
ParseToStruct는 순수 파싱 전용으로 사용
```

---

## 15. Parser Threading 규칙

`UApiMessage::ParseToStruct()`와 `UTransactionCodeMessage::ParseToStruct()`는 백그라운드 스레드에서 호출될 수 있습니다.

따라서 `ParseToStruct()` 내부에서는 아래 작업을 하지 않는 것을 원칙으로 합니다.

```text
GetWorld() 사용
Actor / Component 접근
Blueprint 호출
NewObject 사용
GameThread 전용 API 호출
UObject mutable 상태 변경
```

역할 분리는 아래와 같습니다.

```text
ParseToStruct      = 순수 파싱
ProcessStructData  = GameThread에서 최종 처리
```

---

## 16. Object Registry

`UDxObjectSubsystem`은 Runtime Actor를 카테고리/ID 기준으로 등록하고 조회하는 공통 Registry입니다.

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

사용 예시:

```cpp
UDxObjectSubsystem* ObjectSubsystem = GetGameInstance()->GetSubsystem<UDxObjectSubsystem>();
ObjectSubsystem->RegisterObject(TEXT("Sensor"), TEXT("LIDAR-001"), SensorActor);
AActor* FoundActor = ObjectSubsystem->FindObject(TEXT("Sensor"), TEXT("LIDAR-001"));
```

---

## 17. Search Subsystem

`UDxSearchSubsystem`은 프로젝트별 검색 기능을 구현하기 위한 Base Subsystem입니다.

기존 `uint8` 기반 검색 API는 호환성을 위해 유지되어 있지만, 신규 코드는 `FName` 기반 검색을 사용하는 것을 권장합니다.

권장 함수:

```cpp
PerformSearchByCategory(FName Category, const FString& SearchText, const FDxSearchOptions& Options);
```

프로젝트별 구현체에서는 아래 함수를 override해서 사용합니다.

```cpp
SearchByCategoryName(...)
```

---

## 18. Log Subsystem

`UDxLogSubsystem`은 Runtime 로그를 파일로 저장합니다.

주요 특징:

```text
비동기 로그 저장
로그 버퍼링
날짜별 로그 파일 생성
로그 파일 크기 기반 rotation
오래된 로그 삭제
Linux에서도 Unreal IFileManager 기반 디렉터리 생성 사용
```

기본 로그 경로 예시:

```text
<LaunchDir>/Logs/CustomLogs
```

---

## 19. 프로젝트 독립성 규칙

DTCore는 여러 프로젝트에서 재사용하기 위한 공통 플러그인입니다.

따라서 아래 원칙을 지킵니다.

```text
특정 프로젝트 모듈에 의존하지 않기
특정 프로젝트명 경로 하드코딩하지 않기
운영 서버 URL을 코드에 직접 박지 않기
프로젝트별 Asset, Map, Widget, DataTable은 소비 프로젝트에 두기
환경별 값은 UDTCoreSettings 또는 Game.ini에서 관리하기
```

---

## 20. 패키징 테스트 체크리스트

```text
1. 실행 후 <ProjectName>/Config/Game.ini 파일이 생성되는지
2. [DTCoreRuntimeOverride] 섹션이 생성되는지
3. LocalApiUrl 또는 TestApiUrl을 채웠을 때 API 요청 URL이 바뀌는지
4. WebSocketUrl을 채웠을 때 WebSocket 접속 URL이 바뀌는지
5. 빈 값은 override되지 않고 기본값으로 fallback 되는지
6. 수정 후 클라이언트를 재시작했을 때 값이 반영되는지
```

---

## 21. 주의사항

- `Game.ini` 수정 후 실행 중 즉시 반영되지는 않습니다. 클라이언트 재시작을 권장합니다.
- Linux에서는 실행 방식에 따라 `LaunchDir`가 달라질 수 있으므로 생성 위치를 확인해야 합니다.
- `Config/DefaultGame.ini`는 템플릿 역할입니다. 패키징 환경에 따라 자동 병합 여부가 다를 수 있어, 코드에서도 외부 `Game.ini`를 생성/보강합니다.
- `ParseToStruct()` 내부에서 UObject/Actor 접근을 하면 스레드 안정성 문제가 생길 수 있습니다.
