#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/DxWidgetInfo.h"
#include "DxWidgetSubsystem.generated.h"

class AInteractableActor;
class UDxWidget;
class UWidget;
enum class EDxViewMode : uint8;

UCLASS()
class DTCORE_API UDxWidgetSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

	// Function
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate(); }

	// DxLevelManager 통해서 UI 모드 변경
	UFUNCTION(BlueprintCallable, Category = "UI")
	void SwitchUIMode(EDxViewMode NewMode);
	// 위젯 생성 및 열기
	UFUNCTION(BlueprintCallable, Category = "UI")
	UDxWidget* OpenWidget(AInteractableActor* InteractableActor);
	// 자식 위젯 생성 및 열기
	UFUNCTION(BlueprintCallable, Category = "UI")
	UDxWidget* OpenWidgetFromWidget(UDxWidget* DxWidget, uint8 TargetFlag);
	// 위젯 닫기
	UFUNCTION(BlueprintCallable, Category = "UI")
	void CloseWidget(UDxWidget* CloseWidget);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void CloseWidgetFromWidget(UDxWidget* DxWidget, uint8 TargetFlag);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void BringToFront(UDxWidget* Widget);
	
	/** Blueprint에 정적으로 배치된 위젯을  OpenWidget에 수동 등록 (BringToFront 대상에 포함시키기 위함) */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void RegisterWidget(UDxWidget* Widget);
	
	/** 수동으로 등록된 위젯을 OpenWidgets에서 제거 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UnregisterWidget(UDxWidget* Widget);

	TArray<TObjectPtr<UDxWidget>> GetOpenWidgets();

	UDxWidget* GetMainWidget() const { return MainWidgetInstance; }

	/** 현재 마우스 커서가 오픈된 DxWidget 위에 있는지 여부 */
	UFUNCTION(BlueprintCallable, Category = "UI")
	bool IsMouseOverAnyWidget() const;
	// 입력 차단 등록은 창 닫기/스타일/ZOrder 소유권을 넘기지 않는다.
	UFUNCTION(BlueprintCallable, Category="UI|Input") void RegisterExternalInputBlocker(UWidget* Widget);
	UFUNCTION(BlueprintCallable, Category="UI|Input") void UnregisterExternalInputBlocker(UWidget* Widget);
	
	// MainWidget에서 canvasPanel을 찾는 헬퍼 함수
	class UPanelWidget* GetAddWidgetPanel() const;

private:
	TArray<TWeakObjectPtr<UWidget>> ExternalInputBlockers;
	// 위젯 생성, 위치 설정, 리스트 추가 등 공통 로직을 처리
	UDxWidget* CreateWidgetInternal(TSubclassOf<UDxWidget> WidgetClass, const FVector2D& Position, AInteractableActor* OwnerActor, UDxWidget* ParentWidget, uint8 Flag);

protected:

	// Variable
public:
	/**
	 * 중복 체크를 건너뜀 WidgetFlag 값 목록
	 * 프로젝트에서 중복 허용 위젯(동일 위젯 2개 이상 오픈 시)의 uint8 값을 등록하세요.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TArray<uint8> NoDuplicateCheckFlags;
private:
	UPROPERTY()
	TObjectPtr<UDataTable> LevelDataTable;
	UPROPERTY()
	TObjectPtr<UDxWidget> MainWidgetInstance;

	// 열린 위젯들을 관리하는 배열
	UPROPERTY()
	TArray<TObjectPtr<UDxWidget>> OpenWidgets;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UDxWidget> MainWidgetClass;
};
