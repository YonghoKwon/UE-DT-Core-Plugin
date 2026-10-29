# main 기반 최소 안정화 계약

기준: `b22af0b`에서 시작한 `codex/dtcore-minimal-fixes`. 소비 프로젝트의 업무 규칙은 포함하지 않는다.

## 반영 이유와 기존 계약

| 영역 | 실제 문제와 수정 | 유지한 계약 |
|---|---|---|
| 빌드 | 잘못된 변수·함수·include·Shipping 매크로, EnhancedInput 의존 선언을 수정 | 모듈명, 장비/업무 타입, public dependency 구조 |
| HTTP | Body가 빈 문자열로 전달되던 두 API, 환경 설정 부재 시 Base override 누락을 수정. 종료 전 delegate 해제와 generation 검사로 늦은 완료/retry 차단 | URL·헤더·재시도 횟수·공개 함수 시그니처 |
| STOMP | 수동 Disconnect의 재연결 의도를 해제. 이전 연결의 메시지/완료/해제 callback을 폐기하고 동일 완료를 한 번만 전달 | custom ReceivedMessageEvent binding, Topic 라우팅, OnConnected의 완료 집계 의미 |
| Data worker | parse 작업을 추적하고 join한 뒤 Handler 참조 해제. 종료 이후 GameThread 결과 적용 차단 | ParseToStruct/ProcessStructData, 기존 순서·추출 예산. World cleanup에서 GI 전역 큐를 버리지 않음 |
| 로그 | cleanup과 writer의 파일 I/O 직렬화, 종료 전에 접수된 로그 drain, 쓰기 실패 기록 | WriteLog API·기존 경로·7일 보관·rotation. 공개 Flush API/새 로그 경로 Settings 없음 |
| Registry | 등록 Actor의 GC 참조 추적. EndPlay/Destroy/World cleanup/명시적 해제에서 alias와 delegate 정리 | GetCategoryMap 반환형. 내부 map 조회 포인터이며 복사 snapshot이 아님 |
| Widget | BlueprintNativeEvent를 제거 후 호출하고 cascade/reentry/반복 close에서 한 번만 정리 | 자식부터 제거하는 순서, 기존 Main·모드·persistent 정책 |
| 저장·초깃값 | 구형 Widget 식별자 0~6의 Hidden 이름 보존, reflected 필드 초기화 | byte API와 기존 숫자 의미 |
| 입력 선택 | DoublePress/SingleRelease 및 weak 외부 UI 입력 차단 등록을 명시적 opt-in으로 제공 | 기본 DoublePress. 등록이 Widget 생성·close·style·Z-order 소유권을 넘기지 않음 |

## 구독 이벤트

`OnConnected`는 설정된 구독 완료 callback이 모두 돌아왔을 때 한 번 발생한다. 성공과 실패가 섞여도 발생하며, Topic 0개이면 즉시 발생한다. 모든 구독 성공이나 소비자 업무 처리 완료를 뜻하지 않는다. 구독별 성공 여부는 해당 CompletionCallback에서 판단한다.

공통 5초 receipt timeout, all-success readiness 상태기계와 전송 연결 전용 이벤트는 이번 최소 수정에 포함하지 않았다. receipt callback이 오지 않으면 기존처럼 완료 집계가 대기할 수 있다. 프로젝트 adapter가 필요한 시간 제한·재시도·UI 정책을 소유한다. Binary PCD/JPEG 지원을 BodyString 경로에 추가한 것도 아니다.

## 등록과 종료 수명

Registry는 등록 기간 동안 Actor를 GC 참조로 보유한다. EndPlay(RemovedFromWorld 포함), Destroy, World cleanup, Unregister/Clear에서 해제한다. BeginPlay 전 등록했다가 실행하지 않는 Actor는 소유자가 명시적으로 해제해야 한다. map 포인터는 등록/해제 후 다시 조회하며 GameThread에서만 사용한다.

Data worker 종료는 parsing worker만 기다린다. 예약된 GameThread 적용 callback을 GameThread에서 기다리지 않는다. parsing이나 파일 I/O가 끝나기 전에 timeout을 이유로 참조를 해제하지 않는다. 느린 외부 작업의 종료 지연 상한은 별도 운영 정책이며 이번 수정이 강제 취소를 제공하지 않는다.

## 프로젝트 확장

서버·인증·Topic·업무 Handler·타입 ID·자산·로그 배포 위치·장비·Slab·출력 정책은 프로젝트가 소유한다. Controller/Widget/Component의 공개 속성·virtual/Blueprint 이벤트는 프로젝트 자식에서 적용한다. GameInstanceSubsystem 자식이 기본 Subsystem을 자동 교체하거나 Settings 자식이 GetDefault 기본형을 자동 대체한다고 가정하지 않는다.

`067195b`의 실험적 안정화 API를 사용하던 소비 코드는 all-ready/status/timeout/공개 Flush 호출을 자체 adapter와 기존 callback 계약으로 이관해야 한다. main(`b22af0b`) 소비자는 그 API가 원래 없었으므로 해당 이관 대상이 아니다. `GetType(): int32`와 Widget byte API는 b22 동기화본의 계약을 유지한다.

검증 보고서는 소비 프로젝트의 `Saved/Reports/DTCoreMinimalAcceptance`에 보관한다. 다른 실제 소비 프로젝트의 무수정 호환, Linux, 60분 soak를 이번 호스트 시험으로 보장하지 않는다.
