# b22 동기화 소비 프로젝트 이전

- `FDxDataBase::GetType()`는 `int32`를 반환한다. 프로젝트 전용 enum 또는 상수의 기존 숫자를 고정하고 생산/소비 코드를 함께 변경한다. 기존 enum 반환 override는 무수정 호환이 아니다.
- Widget API와 DataAsset은 `uint8` 식별자를 사용한다. 구형 `EDxWidgetFlag` 이름/번호 0~6은 읽기 호환용 Hidden 항목으로 보존한다. Blueprint 핀의 enum→byte 변환은 실제 로드·메모리 컴파일 후 필요한 자산만 백업/이관한다.
- URL, 계정, Topic 기본값은 비어 있다. 소비 프로젝트가 환경별 설정을 명시한다. `NoDuplicateCheckFlags`도 소비 프로젝트 정책이며 CCTV 식별자 6을 자동 등록하지 않는다.
- `EnhancedInput`은 플러그인 의존성으로 선언된다. Runtime 공용 타입에 특정 프로젝트의 업무 타입을 다시 추가하지 않는다.
- 검증용 `DTCoreCompatHost`는 소비 프로젝트가 준비하는 별도 호스트다. 다른 실제 프로젝트의 호환성을 대신 보장하지 않는다.

## 동기화 안정화 계약

- HTTP Body wrapper는 입력 UTF-8 본문을 그대로 전달한다. 환경 URL이 비어 있으면 Base override를 먼저 확인한다.
- HTTP 종료는 신규 접수·retry를 막고 완료 delegate를 해제한다. 이전 generation의 완료가 호출돼도 업무 callback을 적용하지 않는다.
- `OnTransportConnected`는 전송 연결, `OnConnected`는 설정된 Topic이 모두 준비됐음을 의미한다. 실패/timeout은 `GetSubscriptionStatuses()`로 확인한다. 기본 구독 receipt timeout은 5초다.
- 수동 Disconnect는 자동 재접속 의도를 취소한다. 재개하려면 Connect를 명시적으로 호출한다. 이전 연결의 예약 콜백은 폐기한다.
- Data 종료는 parse worker를 join한 후 handler를 정리한다. GT callback을 GT에서 기다리지 않는다. World cleanup은 파싱 generation과 남은 큐를 정리한다.
- 로그는 단일 writer와 `FlushLogs()` 완료 결과를 사용한다. `LogDirectory` 빈 값은 기존 경로이며 상대 경로는 Project Saved 기준이다. 실패 결과를 숨기지 않는다.
- `GetCategoryMap` 반환형은 유지한다. 레지스트리는 명시적 GC 참조로 등록 객체를 보유하며 Unregister/Destroy/World cleanup/Deinitialize에서 정리한다. 반환 포인터는 다음 registry 변경까지의 조회용이다.
- `ClickActivationPolicy` 기본은 DoublePress다. 단일클릭을 사용하는 소비 controller가 SingleRelease를 선택한다. 중복 Press/Release는 한 번만 처리한다.
- 외부 UI의 입력 차단은 `RegisterExternalInputBlocker`에 등록한다. 이 등록은 close/style/z-order 소유권을 이전하지 않는다.
- 닫기는 BlueprintNativeEvent를 통해 전달하고 반복 close는 한 번만 처리한다. 숨김은 close가 아니다. `bPresistent`는 아직 모드 전환 정책에 연결되지 않았으므로 해당 기능을 보장하지 않는다.
- `stat fps`는 `bShowFpsOnBeginPlay`를 명시적으로 켰을 때만 실행한다.

## 검증 범위 (2026-10-02)

- source 기준 `67d3605`: 소비 프로젝트 UE5.3 Editor Development/Shipping, Windows Development Build/Cook/Stage/Archive, 격리 DTCoreCompatHost Editor Development/Shipping을 확인했다. 현재 소비 프로젝트 전용 테스트는 parent 저장소에 있으며 plugin 단독 CI가 새로 생긴 것은 아니다.
- 공통 계약10개, HTTP Body/URL/종료·구독 intent/timeout·parse join·log flush·registry GC·클릭/close·식별자 검사. 실제 topic.scenario MESSAGE가 TC/DataSync/Slab 이동까지 도달하는 opt-in 시험도 통과했다.
- 소비 프로젝트의 소유Blueprint16개는 메모리 컴파일, 저장0. 기존 Widget enum0~6은 보존했으나 모든 다른 프로젝트의 Blueprint 핀 이관이 끝났다는 의미가 아니다.
- DataTable 구조체의 미초기화7필드는 명시적0/Local/Get/None 기본값으로 보강했다. enum 순서와 저장된 행은 변경하지 않았다.
- 기존 ApiTopics 라우팅과 Binary PCD/JPEG 경계는 유지했다. BodyString 경로를 Binary 호환이라고 표기하지 않는다.
- parent 전체 회귀는 기존 운영맵 높이fixture1실패를 포함한다. 실제 마우스는 일부 항목만 확인했으며 Main/Viewport 모든 조합·다른 실제 소비 프로젝트·Linux·60분soak는 미완료다.
- `Saved/Reports/DTCoreSync`의 raw 로그·실패/재시도 기록과 소비 프로젝트 README를 함께 참조한다. 격리 호스트 통과를 타 프로젝트 무수정 호환으로 해석하지 않는다.
