# b22 동기화 소비 프로젝트 이전

- `FDxDataBase::GetType()`는 `int32`를 반환한다. 프로젝트 전용 enum 또는 상수의 기존 숫자를 고정하고 생산/소비 코드를 함께 변경한다. 기존 enum 반환 override는 무수정 호환이 아니다.
- Widget API와 DataAsset은 `uint8` 식별자를 사용한다. 구형 `EDxWidgetFlag` 이름/번호 0~6은 읽기 호환용 Hidden 항목으로 보존한다. Blueprint 핀의 enum→byte 변환은 실제 로드·메모리 컴파일 후 필요한 자산만 백업/이관한다.
- URL, 계정, Topic 기본값은 비어 있다. 소비 프로젝트가 환경별 설정을 명시한다. `NoDuplicateCheckFlags`도 소비 프로젝트 정책이며 CCTV 식별자 6을 자동 등록하지 않는다.
- `EnhancedInput`은 플러그인 의존성으로 선언된다. Runtime 공용 타입에 특정 프로젝트의 업무 타입을 다시 추가하지 않는다.
- 검증용 `DTCoreCompatHost`는 소비 프로젝트가 준비하는 별도 호스트다. 다른 실제 프로젝트의 호환성을 대신 보장하지 않는다.
