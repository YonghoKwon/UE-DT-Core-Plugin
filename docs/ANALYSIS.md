# UE-DT-Core-Plugin 저장소 분석 보고서

- 분석 기준 브랜치: `claude/repo-analysis-branch-ui83h6`
- 분석 기준 커밋: `2eec1fe` ("동기화", 2026-06-13)
- 분석일: 2026-07-04

---

## 1. 브랜치 / 저장소 상태

| 항목 | 내용 |
|---|---|
| 현재 브랜치 | `claude/repo-analysis-branch-ui83h6` |
| HEAD 커밋 | `2eec1fe` (동기화) |
| Working Tree | clean (미커밋 변경 없음) |
| 비고 | `claude/digital-twin-core-analysis-1lxlcb` 브랜치와 완전히 동일한 커밋을 가리킴 (두 브랜치 간 diff 없음) |

최근 커밋 이력은 품질 개선 PR(#1, `feature/dtcore-quality-polish-95`) 머지 이후의 후속 폴리싱 작업입니다.

주요 최근 변경:

```text
2eec1fe 동기화 (Api 토픽 라우팅을 WebSocket 큐로 변경 — 1줄)
b6d2ddc HighlightActor에 메시 배열을 const 참조로 전달
2fad940 레지스트리/핸들러 캐시의 스레드 소유권 문서화
ef30faf 위젯 생성 시 null World 방어
5f1030b DataTable 로드를 생성자 → Initialize로 이동
9c9390e 데이터 큐 처리에서 JSON 파싱 실패 로깅
1cf0a5d HTTP 일시 오류에 지수 백오프 재시도
8ea9137 WebSocket 재연결 횟수를 설정으로 제한
1752c08 Base Actor/Component의 유휴 Tick 비활성화
```

---

## 2. 플러그인 개요

Unreal Engine 기반 **디지털 트윈 프로젝트용 공통 런타임 플러그인**입니다. API 통신, WebSocket/STOMP 수신, 데이터 큐 처리, 객체 등록/검색, 위젯 관리, 로그 등 여러 프로젝트에서 반복되는 기반 기능을 하나의 재사용 가능한 Core 계층으로 제공합니다.

| 항목 | 내용 |
|---|---|
| 모듈 구성 | `DTCore` Runtime 모듈 1개 (LoadingPhase: Default) |
| 자체 C++ 코드 | 약 3,600줄 |
| 서드파티 | yyjson (약 11,000줄, `Source/DTCore/Private/Lib/yyjson.c`) |
| 테스트 / CI | 없음 |

### 의존성 구조 (`DTCore.Build.cs`)

| 구분 | 모듈 |
|---|---|
| Public | `Core`, `CoreUObject`, `Engine`, `UMG` |
| Private | `InputCore`, `EnhancedInput`, `HTTP`, `WebSockets`, `Stomp`, `Slate`, `SlateCore`, `DeveloperSettings` |

Public 의존성을 최소화하고 네트워크/UI 프레임워크 계열을 Private으로 격리하여, DTCore를 사용하는 소비 프로젝트로 불필요한 빌드 의존성이 전파되지 않도록 설계되어 있습니다. **잘 관리되고 있는 부분입니다.**

---

## 3. 아키텍처

### 3.1 GameInstanceSubsystem 구성

| 서브시스템 | 역할 | 비고 |
|---|---|---|
| `UDxApiSubsystem` | DataTable(`FApiStruct`) 기반 HTTP 호출, 활성 요청 추적/취소, 지수 백오프 재시도(초기 1초) | 317줄 |
| `UDxWebSocketSubsystem` | STOMP 연결/구독/라우팅, 백오프 재연결(1초→최대 60초, 배수 2.0, `MaxReconnectAttempts` 설정) | 362줄 |
| `UDxDataSubsystem` | API/WS 큐 분리, Tick 배치 처리, 핸들러 디스패치/캐시 | 439줄, 파이프라인 중심 |
| `UDxObjectSubsystem` | Category/ID 기반 런타임 Actor 레지스트리 | 215줄 |
| `UDxWidgetSubsystem` | 위젯 생성/닫기/Z-order/부모-자식 관리 | 302줄 |
| `UDxLogSubsystem` | 비동기 파일 로그 (버퍼링, rotation, 날짜별 파일) | 250줄 |
| `UDxSearchSubsystem` | 프로젝트별 검색 기능 Base (`FName` 기반 권장) | 65줄 |
| `UDxProcessSubsystem` | ComponentId 기반 ActorComponent 레지스트리 | 64줄, **README 미기재** |

### 3.2 데이터 파이프라인 (설계의 핵심)

```text
HTTP 응답 / STOMP 메시지 수신
        ↓
UDxDataSubsystem의 ApiDataQueue / WebSocketDataQueue 적재 (TQueue)
        ↓
Tick에서 시간 예산(5ms) 내 배치 추출 (API 100건 / WS 2000건 Reserve)
        ↓
AnyBackgroundThreadNormalTask에서 yyjson 파싱 + Handler->ParseToStruct (순수 파싱)
        ↓
GameThread AsyncTask에서 Handler->ProcessStructData (최종 반영)
```

스레드 안전 장치:

- `TWeakObjectPtr` + 작업 시작/종료 시 이중 유효성 체크
- `bIsShuttingDown` 플래그로 종료 중 비동기 작업 차단
- `bApiProcessing.AtomicSet(true)` / `bWebSocketProcessing`으로 중복 배치 방지
- 핸들러 맵을 `TSharedPtr<TMap<...>>` 스냅샷으로 백그라운드에 전달 (GameThread 소유권 유지)

### 3.3 확장 모델 (소비 프로젝트 관점)

1. `UApiMessage` / `UTransactionCodeMessage`를 상속한 핸들러 클래스 작성
   - `ParseToStruct(JsonString)` — 백그라운드 스레드, **순수 파싱만** (UObject/GameThread 작업 금지)
   - `ProcessStructData(Data)` — GameThread, Actor/Widget 등 최종 반영
2. DataTable(`FApiStruct` / `FTransactionCodeStruct`)에 핸들러 클래스 등록
3. `UDTCoreSettings`(Project Settings > DTCore Settings)로 DataTable/URL/Topic 주입
4. 패키징 후에는 `Game.ini`의 `[DTCoreRuntimeOverride]` 섹션으로 접속 정보 오버라이드 (`DTCoreRuntimeConfig`가 후보 경로 4곳 탐색, 빈 값은 무시)

디스패치 키:

- API: 응답 JSON의 `meta.resource` + `meta.action` → `FPaths::Combine(Resource, Action)` 키
- WebSocket: 메시지 JSON의 `MESSAGE_ID` → TransactionCode 키

---

## 4. 발견 사항

### 4.1 [High] 최신 커밋으로 인한 Topic 라우팅 분기 무의미화 + README 불일치

- 위치: `Source/DTCore/Private/Core/DxWebSocketSubsystem.cpp:184`
- 내용: 커밋 `2eec1fe`가 `ReceivedMessage()`의 `ETopicRouteType::Api` 분기를 `EnqueueApiData` → `EnqueueWebSocketData`로 변경했습니다.

```cpp
switch (*RouteType)
{
case ETopicRouteType::WebSocket:
    DS->EnqueueWebSocketData(BodyString);
    break;
case ETopicRouteType::Api:
    DS->EnqueueWebSocketData(BodyString);   // ← 2eec1fe에서 EnqueueApiData로부터 변경됨
    break;
}
```

- 영향:
  - `WebSocketTopics` / `ApiTopics` 설정 구분과 `ETopicRouteType` enum 분기가 사실상 무의미해짐 (두 분기 모두 같은 큐로 적재)
  - `ApiTopics`로 수신된 메시지는 이제 `MESSAGE_ID` 키 기반 TransactionCode 핸들러로 디스패치됨. API 형식(`meta.resource`/`meta.action`) 메시지라면 `MESSAGE_ID`가 없어 "'MESSAGE_ID' 노드 없음" 경고와 함께 버려질 수 있음
  - 커밋 메시지("동기화")로 보아 의도적 변경으로 추정되나, 의도라면 라우팅 구조와 문서 정리가 필요함
- 참고: README 4.2절의 라우팅 표는 본 분석과 함께 현재 코드 기준으로 갱신되었습니다. 변경의 의도(임시 조치인지, 확정 구조인지)는 저장소 소유자 확인이 필요합니다.

### 4.2 [Medium] ApiDataTable 이중 로드

- 위치: `Source/DTCore/Private/Core/DxApiSubsystem.cpp:20`, `Source/DTCore/Private/Core/DxDataSubsystem.cpp:27`
- 두 서브시스템이 각각 `Settings->ApiDataTable.LoadSynchronous()`를 호출합니다. UObject 캐시 덕분에 실제 이중 I/O는 아니지만, 로드 실패 처리/소유권이 이원화되어 있습니다. 한쪽(예: DxDataSubsystem)에서 로드하고 다른 쪽은 참조만 하는 구조가 더 명확합니다.

### 4.3 [Low] PascalCase 변환 죽은 코드

- 위치: `Source/DTCore/Private/Core/DxDataSubsystem.cpp:48-51`

```cpp
// Resource와 Action을 PascalCase로 변환하여 키 생성
FString PascalResource = Row.ApiResource;   // 변환 없음, 복사만
FString PascalAction = Row.ApiAction;       // 변환 없음, 복사만
FString Key = FPaths::Combine(PascalResource, PascalAction);
```

- 주석과 달리 실제 변환 로직이 없습니다. 변환이 불필요하면 주석/중간 변수 제거, 필요하면 변환 구현이 필요합니다. 현재는 DataTable의 표기와 서버 응답 `meta.resource`/`meta.action` 표기가 **대소문자까지 정확히 일치해야** 핸들러가 매칭됩니다.

### 4.4 [Medium] ParseToStruct 스레드 규칙이 문서로만 강제됨

- `ParseToStruct`는 백그라운드 스레드에서 호출되며 "순수 파싱만" 해야 한다는 규칙이 README/주석으로만 존재합니다. 소비 프로젝트의 파생 클래스가 규칙을 어기면(예: 내부에서 `NewObject`, `GetWorld()` 호출) 비결정적 크래시가 발생할 수 있습니다.
- 완화책 예시: `ParseToStruct` 내에서 `check(!IsInGameThread())` + 개발 빌드 한정 가드, 또는 API 문서화 강화.

### 4.5 [Low] WebSocket 인증 기본값 하드코딩

- 위치: `Source/DTCore/Public/Core/DTCoreSettings.h:34-37` (`WebSocketLogin/Passcode = "artemis"`)
- 개발 편의용 기본값이지만, README 12절의 "운영 서버 정보를 cpp에 직접 입력하지 않기" 원칙과 결이 다릅니다. 운영 배포 시 `Game.ini` 오버라이드로 반드시 교체되는지 확인이 필요합니다.

### 4.6 [Medium] 테스트 / CI 부재

- 자동화 테스트(Unreal Automation Test)와 CI 파이프라인이 없습니다. 파싱/디스패치 로직(키 생성, 큐 배치, Game.ini 오버라이드 우선순위)은 순수 로직이라 테스트 작성 난이도가 낮은 편입니다.

### 4.7 [정보] README 미기재 항목

- `UDxProcessSubsystem` (ComponentId 기반 ActorComponent 레지스트리)가 README 4절 서브시스템 목록에 없습니다.
- `MaxReconnectAttempts` 설정이 README 8절 설정 표에 없습니다. (본 분석과 함께 README에 반영됨)

---

## 5. 강점 평가

1. **스레드 안전 패턴의 일관성** — WeakThis 이중 체크, 종료 플래그, 핸들러 맵 스냅샷 공유, STOMP 콜백 데이터를 복사 후 GameThread로 마샬링하는 패턴이 코드 전반에 일관되게 적용되어 있습니다.
2. **의존성 관리** — Public 의존성 4개로 최소화, 네트워크/UI 스택 Private 격리.
3. **설정 주입 설계** — DeveloperSettings + Game.ini 런타임 오버라이드(우선순위 6단계, 후보 경로 4곳)로 패키징 후 환경 변경을 지원. 빈 값 무시 규칙도 실용적입니다.
4. **문서 품질** — README가 아키텍처/규칙/체크리스트까지 갖춘 높은 수준입니다 (4.1의 불일치 제외).
5. **성능 고려** — Tick 시간 예산(5ms), 배치 처리, 핸들러 인스턴스 캐시, Base 클래스 Tick 기본 비활성화(`bStartWithTickEnabled = false`).

---

## 6. 권장 후속 조치 요약

| 우선순위 | 항목 | 관련 절 |
|---|---|---|
| High | Api 토픽 라우팅 변경(2eec1fe)의 의도 확인 및 구조/문서 정리 | 4.1 |
| Medium | ParseToStruct 스레드 규칙 코드 수준 가드 | 4.4 |
| Medium | ApiDataTable 로드 책임 일원화 | 4.2 |
| Medium | 핵심 순수 로직에 Automation Test 도입 | 4.6 |
| Low | PascalCase 죽은 코드 정리 | 4.3 |
| Low | 인증 기본값 정책 확인 | 4.5 |

구체적인 수정 요청 방법(AI에게 전달할 프롬프트 포함)은 [`docs/수정요청.md`](./수정요청.md)를 참고하세요.
