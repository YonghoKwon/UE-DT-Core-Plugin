#pragma once

#include "CoreMinimal.h"
#include "DxDataType.generated.h"

/**
 * @brief 플러그인 공통 데이터 타입 (프로젝트 전용 타입은 각 프로젝트에서 별도 Enum으로 선언)
 */
UENUM()
enum class EDxDataType : uint8
{
	None UMETA(DisplayName = "None"), // 초기화되지 않은 상태를 위해 기본값(0) 세팅
	
	//...
};

// 모든 데이터 구조체의 부모가 될 구조체
struct DTCORE_API FDxDataBase
{
	virtual ~FDxDataBase() = default; // 가상 소멸자 필수 (메모리 누수 방지)

	// 자신이 무슨 타입인지 알려주는 순수 가상 함수
	// int32를 반환하여 프로젝트별 EDxM7atDataType 등 확장 Enum 값도 수용
	virtual int32 GetType() const = 0;
};