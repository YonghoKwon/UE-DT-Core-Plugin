#pragma once

UENUM(BlueprintType)
enum class EDxWidgetFlag : uint8
{
	None = 0,
	// 저장된 구형 enum 이름/번호를 읽기 위한 호환 항목. 신규 API는 uint8 식별자를 사용한다.
	ShipFreeView = 1 UMETA(Hidden),
	ShipTopView = 2 UMETA(Hidden),
	CraneFreeView = 3 UMETA(Hidden),
	CraneTopView = 4 UMETA(Hidden),
	UpdateWidget = 5 UMETA(Hidden),
	CctvWidget = 6 UMETA(Hidden),
};
