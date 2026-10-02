#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputSubsystemInterface.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "DxPlayerControllerBase.generated.h"


class ADxPlayerBase;

UENUM(BlueprintType)
enum class EDxClickActivationPolicy : uint8 { DoublePress, SingleRelease };

UCLASS()
class DTCORE_API ADxPlayerControllerBase : public APlayerController
{
	GENERATED_BODY()

	// 함수
public:
	ADxPlayerControllerBase();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Input")
	EDxClickActivationPolicy ClickActivationPolicy = EDxClickActivationPolicy::DoublePress;
	virtual void BeginPlay() override;
	UFUNCTION()
	void SetUIMoveInput(const FVector2D& MoveInput);
	UFUNCTION()
	void SetUILookInput(const FVector2D& LookInput);
	UFUNCTION()
	void SetUIVerticalInput(float Value);
	UFUNCTION()
	void CyclePlayerControlSpeed();
	UFUNCTION()
	float GetPlayerControlSpeed();
	UFUNCTION()
	void ControlMoveSpeed(const FInputActionValue& Value);
	UFUNCTION()
	void ClickLeftMouseButton(const FInputActionValue& Value);
	UFUNCTION()
	void ClickRightMouseButton(const FInputActionValue& Value);

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FDTCoreInputPolicyTest;
	TFunction<double()> InputClockForTests;
	TFunction<void(AActor*)> ClickSinkForTests;
#endif
	void ActivateHoveredActor();

protected:
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void MoveUpDown(const FInputActionValue& Value);

	// 마우스 호버 감지
	void CheckMouseHover();

	// 변수
public:
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputMappingContext* PlayerContext;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MovementAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseWheelAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* UpDownAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LeftMouseButtonAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* RightMouseButtonAction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector2D UIMoveInput;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector2D UILookInput;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float UIVerticalInput = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	AActor* HitActor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Player|Control")
	int32 ControlSpeedStep = 100;
	UPROPERTY(BlueprintReadWrite)
	bool PossibleClick = true;

private:
	UPROPERTY()
	bool bIsClickRightMouseButton;
	UPROPERTY()
	bool bIsHitDoOnce;
	UPROPERTY()
	bool bIsNotHitDoOnce;
	UPROPERTY()
	bool bIsHitActor;
	UPROPERTY()
	bool bIsWidgetUnderMouse = false;
	
	// 좌클릭 Press+Release 엣지 추적용.
	// PixelStreaming  등 원격 입력 환경에서는 Enhanced Input이 동일한 물리 클릭에 대해
	// Release(value=flase) 상태를 여러 프레임에 걸쳐 중복 전달할 수 있다.
	// 이 플래그로 실제 Press가 있었던 경우에만 Release를 유효한 클릭으로 인정해 중복 호출을 차단한다.
	UPROPERTY()
	bool bWasLeftMouseButtonDown = false;
	
	// 더블클릭 판정: Release가 아닌 Press 타이밍 기준으로 판정한다.
	// PixelStreaming 환경에서는 Release(마우스 뗌) 이벤트의 네트워크 전달 지연이 수십ms~수초까지
	// 매우 불규칙한 반면, Press(누름) 이벤트는 지연이 짧고 일정하게 들어오는 것이 로그로 확인됨.
	UPROPERTY()
	double LastPressTime = -1.0;
	
	// 더블클릭 판정 대상이 된 마지막 Press 시점의 호버 액터 (같은 액터에 대해서만 더블클릭 인정)
	UPROPERTY()
	TWeakObjectPtr<class AInteractableActor> LastPressedActor;
	
	// 더블클릭으로 인정할 Press-Press 최대 간격(초)
	UPROPERTY(EditAnywhere, Category = "Input")
	float DoubleClickPressThreshold = 0.4f;
	
	// 현재 호버된 InteractableActor
	UPROPERTY()
	class AInteractableActor* CurrentHoveredActor = nullptr;
	// 현재 호버된 메쉬 컴포넌트
	UPROPERTY()
	UPrimitiveComponent* CurrentHoveredMesh;

	UPROPERTY()
	ADxPlayerBase* DxPlayerBase;

	//
	// 마지막으로 Hover 체크를 한 시간
	float LastHoverCheckTime = 0.0f;
	// Hover 체크 주기 (초 단위, 0.05 = 초당 20회)
	const float HoverCheckInterval = 0.05f;
	//

protected:
};
