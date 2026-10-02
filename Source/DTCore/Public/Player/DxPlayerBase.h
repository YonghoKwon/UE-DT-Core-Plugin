#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DxPlayerBase.generated.h"
UENUM(BlueprintType)
enum class EDirectionType : uint8
{
	Left       UMETA(DisplayName = "Left"),
	Right      UMETA(DisplayName = "Right"),
	Forward    UMETA(DisplayName = "Forward"),
	Backward   UMETA(DisplayName = "Backward"),
	Up         UMETA(DisplayName = "Up"),
	Down       UMETA(DisplayName = "Down"),
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnControlSpeedChanged, float, NewSpeed);

class UCameraComponent;
class USpringArmComponent;

UCLASS()
class DTCORE_API ADxPlayerBase : public APawn
{
	GENERATED_BODY()

public:
	ADxPlayerBase();

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
protected:
	virtual void BeginPlay() override;

	// 함수 모음
public:
	UFUNCTION()
	void SetControlSpeed(float NewSpeed);
	UFUNCTION()
	float GetControlSpeed();
	UFUNCTION()
	void CycleControlSpeed(); // 속도 레벨 순환
	UFUNCTION()
	virtual void Look(const FVector2D& LookVector);
	UFUNCTION()
	virtual void Move(const FVector2D& MovementVector);
	UFUNCTION()
	virtual void MoveUpDown(float Value);
	// 마우스 휠 입력 처리 (기본: 이동 속도 조절, Dock 등에서 오버라이드하여 줌으로 변경 가능)
	UFUNCTION()
	virtual void HandleMouseWheel(float RawValue, float Step);
private:

protected:

	// 변수 모음
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	USpringArmComponent* SpringArmComponent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	UCameraComponent* CameraComponent;

	UPROPERTY(BlueprintAssignable, Category = "Movement")
	FOnControlSpeedChanged OnControlSpeedChanged;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float ControlSpeed = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float ControlMinSpeed = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float ControlMaxSpeed = 1000.0f;
private:

protected:
};
