#include "UI/DxWidgetFreeview.h"

#include "DTCore.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"

UDxWidgetFreeview::UDxWidgetFreeview(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 드래그 중 포커스를 받아야 SViewport로 포커스가 넘어가며 마우스 capture를 빼앗기지 않는다.
	SetIsFocusable(true);
}

bool UDxWidgetFreeview::SetWindowPosition(FVector2D Position)
{
	if (!CanPositionWindow()) return false;

	const FVector2D BoundsSize = GetBoundsSize();
	if (bClampToBounds && BoundsSize.X > 0.0f && BoundsSize.Y > 0.0f)
	{
		Position = ClampWindowPosition(Position, GetWindowSize(), BoundsSize, BoundsMargin);
	}
	if (Position.Equals(GetWindowPosition(), UE_KINDA_SMALL_NUMBER)) return true;
	return ApplyWindowPosition(Position);
}

FVector2D UDxWidgetFreeview::GetWindowPosition() const
{
	if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		return CanvasSlot->GetAnchors().Minimum * GetBoundsSize() + CanvasSlot->GetPosition() - CanvasSlot->GetAlignment() * GetWindowSize();
	}
	return ScreenToBounds(GetCachedGeometry().GetAbsolutePosition());
}

FVector2D UDxWidgetFreeview::ClampWindowPosition(FVector2D Position, FVector2D WindowSize, FVector2D BoundsSize, float Margin)
{
	const double SafeMargin = FMath::Max(Margin, 0.0f);
	return FVector2D(
		FMath::Clamp(Position.X, SafeMargin, FMath::Max(SafeMargin, BoundsSize.X - WindowSize.X - SafeMargin)),
		FMath::Clamp(Position.Y, SafeMargin, FMath::Max(SafeMargin, BoundsSize.Y - WindowSize.Y - SafeMargin)));
}

void UDxWidgetFreeview::NativeDestruct()
{
	EndWindowDrag(false);
	Super::NativeDestruct();
}

void UDxWidgetFreeview::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bDraggingWindow || !bClampToBounds) return;

	const FVector2D BoundsSize = GetBoundsSize();
	const FVector2D WindowSize = GetWindowSize();
	if (BoundsSize == LastBoundsSize && WindowSize == LastWindowSize) return;

	LastBoundsSize = BoundsSize;
	LastWindowSize = WindowSize;
	SetWindowPosition(GetWindowPosition());
}

FReply UDxWidgetFreeview::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bMovable && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && CanPositionWindow() && IsOverTitle(InGeometry, InMouseEvent))
	{
		const TSharedPtr<SWidget> Widget = GetCachedWidget();
		if (Widget.IsValid())
		{
			bDraggingWindow = true;
			DragStartPosition = GetWindowPosition();
			DragGrabOffset = ScreenToBounds(InMouseEvent.GetScreenSpacePosition()) - DragStartPosition;
			return FReply::Handled().CaptureMouse(Widget.ToSharedRef()).SetUserFocus(Widget.ToSharedRef(), EFocusCause::Mouse);
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UDxWidgetFreeview::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingWindow && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		EndWindowDrag(true);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UDxWidgetFreeview::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bDraggingWindow)
	{
		return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
	}
	// 창 밖에서 버튼을 놓친 경우
	if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		EndWindowDrag(true);
		return FReply::Handled().ReleaseMouseCapture();
	}
	SetWindowPosition(ScreenToBounds(InMouseEvent.GetScreenSpacePosition()) - DragGrabOffset);
	return FReply::Handled();
}

void UDxWidgetFreeview::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	EndWindowDrag(true);
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

FVector2D UDxWidgetFreeview::GetWindowSize() const
{
	const FVector2D CachedSize = GetCachedGeometry().GetLocalSize();
	return CachedSize.X > 0.0f && CachedSize.Y > 0.0f ? CachedSize : GetDesiredSize();
}

FVector2D UDxWidgetFreeview::GetBoundsSize() const
{
	if (Slot)
	{
		return Slot->Parent ? FVector2D(Slot->Parent->GetCachedGeometry().GetLocalSize()) : FVector2D::ZeroVector;
	}
	return FVector2D(UWidgetLayoutLibrary::GetViewportWidgetGeometry(const_cast<UDxWidgetFreeview*>(this)).GetLocalSize());
}

FVector2D UDxWidgetFreeview::ScreenToBounds(FVector2D ScreenPosition) const
{
	if (Slot)
	{
		return Slot->Parent ? FVector2D(Slot->Parent->GetCachedGeometry().AbsoluteToLocal(ScreenPosition)) : ScreenPosition;
	}
	return FVector2D(UWidgetLayoutLibrary::GetViewportWidgetGeometry(const_cast<UDxWidgetFreeview*>(this)).AbsoluteToLocal(ScreenPosition));
}

bool UDxWidgetFreeview::IsOverTitle(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) const
{
	const UWidget* TitleArea = TitleDragArea ? TitleDragArea.Get() : Cast<UWidget>(Img_TitleBg.Get());
	if (TitleArea)
	{
		return TitleArea->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition());
	}
	return FVector2D(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())).Y <= TitleBarHeight;
}

bool UDxWidgetFreeview::CanPositionWindow() const
{
	if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		const FAnchors Anchors = CanvasSlot->GetAnchors();
		return !Anchors.IsStretchedHorizontal() && !Anchors.IsStretchedVertical();
	}
	return !Slot && IsInViewport();
}

bool UDxWidgetFreeview::ApplyWindowPosition(FVector2D Position)
{
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		CanvasSlot->SetPosition(Position - CanvasSlot->GetAnchors().Minimum * GetBoundsSize() + CanvasSlot->GetAlignment() * GetWindowSize());
		return true;
	}

	// AddToViewport 기본 상태는 화면 전체를 채우므로 처음 옮길 때 내용 크기로 줄인다.
	if (GetWindowSize().Equals(GetBoundsSize(), 1.0f))
	{
		SetDesiredSizeInViewport(GetDesiredSize());
	}
	SetAlignmentInViewport(FVector2D::ZeroVector);
	SetPositionInViewport(Position, false);
	return true;
}

void UDxWidgetFreeview::EndWindowDrag(bool bNotify)
{
	if (!bDraggingWindow) return;
	bDraggingWindow = false;

	if (bNotify)
	{
		const FVector2D Position = GetWindowPosition();
		if (!Position.Equals(DragStartPosition, UE_KINDA_SMALL_NUMBER))
		{
			OnWindowMoved.Broadcast(this, Position);
		}
	}
}
