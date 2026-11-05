#pragma once

#include "CoreMinimal.h"
#include "UI/DxWidget.h"
#include "DxWidgetFreeview.generated.h"

class UWidget;
class UDxWidgetFreeview;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDxWidgetFreeviewMoved, UDxWidgetFreeview*, Widget, FVector2D, Position);

/**
 * 떠 있는 패널의 공통 기반. 이 클래스를 상속한 위젯만 이동 기능을 쓴다(UDxWidget 동작은 그대로).
 * - 제목 영역을 드래그해 옮긴다. 제목 영역은 TitleDragArea, Img_TitleBg, 위쪽 TitleBarHeight 순으로 정한다.
 * - 부모 영역(Canvas 또는 Viewport) 밖으로 나가지 않게 제한한다. 영역·창 크기가 바뀌면 다시 제한한다.
 * - 닫기는 UDxWidget의 Btn_Close와 CloseWidget을 그대로 쓴다.
 * 위치는 부모 영역 왼쪽 위 기준의 창 왼쪽 위 좌표이다. 저장은 하지 않으며 필요하면 OnWindowMoved로 받아 프로젝트에서 저장한다.
 * EPlayerViewType::FreeView(카메라 시점)와는 무관하다.
 */
UCLASS(Blueprintable)
class DTCORE_API UDxWidgetFreeview : public UDxWidget
{
	GENERATED_BODY()

public:
	UDxWidgetFreeview(const FObjectInitializer& ObjectInitializer);

	// 위치를 옮긴다. bClampToBounds이면 제한한 위치로 옮기며, 부모가 Canvas·Viewport가 아니거나 Canvas 앵커가 늘어나는 형태이면 false.
	UFUNCTION(BlueprintCallable, Category = "DxWidget|Freeview")
	bool SetWindowPosition(FVector2D Position);
	UFUNCTION(BlueprintPure, Category = "DxWidget|Freeview")
	FVector2D GetWindowPosition() const;
	UFUNCTION(BlueprintPure, Category = "DxWidget|Freeview")
	bool IsDraggingWindow() const { return bDraggingWindow; }

	// 창이 영역보다 크면 왼쪽 위(Margin)에 맞춰 제목 영역이 보이게 한다.
	UFUNCTION(BlueprintPure, Category = "DxWidget|Freeview")
	static FVector2D ClampWindowPosition(FVector2D Position, FVector2D WindowSize, FVector2D BoundsSize, float Margin = 0.0f);

	// 드래그로 위치가 바뀌어 끝났을 때만 호출한다. SetWindowPosition과 자동 제한에서는 호출하지 않는다.
	UPROPERTY(BlueprintAssignable, Category = "DxWidget|Freeview")
	FDxWidgetFreeviewMoved OnWindowMoved;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DxWidget|Freeview")
	bool bMovable = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DxWidget|Freeview")
	bool bClampToBounds = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DxWidget|Freeview", meta = (ClampMin = "0"))
	float BoundsMargin = 0.0f;
	// TitleDragArea와 Img_TitleBg가 모두 없을 때 쓰는 위쪽 드래그 영역 높이
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DxWidget|Freeview", meta = (ClampMin = "0"))
	float TitleBarHeight = 38.0f;

	UPROPERTY(meta = (BindWidgetOptional), VisibleAnywhere, BlueprintReadOnly, Category = "DxWidget|Freeview")
	TObjectPtr<UWidget> TitleDragArea;

protected:
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	virtual FVector2D GetWindowSize() const;
	virtual FVector2D GetBoundsSize() const;
	// 화면 좌표를 부모 영역 좌표로 바꾼다.
	virtual FVector2D ScreenToBounds(FVector2D ScreenPosition) const;

private:
	bool IsOverTitle(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) const;
	bool CanPositionWindow() const;
	bool ApplyWindowPosition(FVector2D Position);
	void EndWindowDrag(bool bNotify);

	bool bDraggingWindow = false;
	FVector2D DragGrabOffset = FVector2D::ZeroVector;
	FVector2D DragStartPosition = FVector2D::ZeroVector;
	FVector2D LastBoundsSize = FVector2D::ZeroVector;
	FVector2D LastWindowSize = FVector2D::ZeroVector;
};
