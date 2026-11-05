#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "DTCoreTestTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Core/DTCoreSettings.h"
#include "Core/DxWebSocketSubsystem.h"
#include "Core/DxWidgetSubsystem.h"
#include "UI/DxWidgetConfigData.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "DTCore.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "InteractableActor/InteractableActor.h"
#include "IStompClient.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Async/TaskGraphInterfaces.h"
#include "Widgets/SOverlay.h"

class FDTCorePublishStomp : public IStompClient
{
public:
	struct FSent
	{
		FString Destination;
		TArray<uint8> Body;
		FStompHeader Headers;
		FStompRequestCompleted Completion;
	};
	bool Connected = true;
	TArray<FSent> Sent;
	FStompClientConnectedEvent ConnectedEvent;
	FStompClientConnectionErrorEvent ConnectionErrorEvent;
	FStompClientErrorEvent ErrorEvent;
	FStompClientClosedEvent ClosedEvent;
	void Connect(const FStompHeader&) override
	{
		Connected = true;
	}
	void Disconnect(const FStompHeader&) override
	{
		Connected = false;
	}
	bool IsConnected() const override
	{
		return Connected;
	}
	FString Subscribe(const FString&, const FStompSubscriptionEvent&, const FStompRequestCompleted&) override
	{
		return FString();
	}
	void Unsubscribe(FString, const FStompRequestCompleted&) override
	{
	}
	void Send(const FString& Destination, const FStompBuffer& Body, const FStompHeader& Headers, const FStompRequestCompleted& Completion) override
	{
		Sent.Add({Destination, Body, Headers, Completion});
	}
	FStompClientConnectedEvent& OnConnected() override
	{
		return ConnectedEvent;
	}
	FStompClientConnectionErrorEvent& OnConnectionError() override
	{
		return ConnectionErrorEvent;
	}
	FStompClientErrorEvent& OnError() override
	{
		return ErrorEvent;
	}
	FStompClientClosedEvent& OnClosed() override
	{
		return ClosedEvent;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCorePublishTest, "DTCore.Functional.Publish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCorePublishTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	TStrongObjectPtr<UDxCoreTestListener> Listener(NewObject<UDxCoreTestListener>());
	auto Fake = MakeShared<FDTCorePublishStomp>();
	Ws->StompClient = Fake;
	FSTOMPRequestCompleted Callback;
	Callback.BindDynamic(Listener.Get(), &UDxCoreTestListener::ReceivePublish);

	// 연결 전에는 보내지 않고 누적 수만 올린다.
	TestFalse(TEXT("disconnected publish rejected"), Ws->PublishMessage(TEXT("/topic/a"), TEXT("x"), {}, Callback));
	TestFalse(TEXT("disconnected binary publish rejected"), Ws->PublishBinaryMessage(TEXT("/topic/a"), {1, 2}, {}, Callback));
	TestEqual(TEXT("drops counted"), Ws->GetPublishDroppedCount(), 2);
	TestEqual(TEXT("nothing sent while disconnected"), Fake->Sent.Num(), 0);
	TestTrue(TEXT("drop warning latched"), Ws->bPublishDropLogged);
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("dropped publish never calls back"), Listener->PublishSuccessCount + Listener->PublishFailureCount, 0);

	Ws->bWantsConnection = true;
	Ws->SetConnectionState(EDxWebSocketConnectionState::Ready, TEXT("fixture"));
	TestFalse(TEXT("Ready re-arms drop warning"), Ws->bPublishDropLogged);
	TestFalse(TEXT("empty destination rejected"), Ws->PublishMessage(FString(), TEXT("x"), {}, Callback));
	TestEqual(TEXT("empty destination is not a drop"), Ws->GetPublishDroppedCount(), 2);

	Fake->Connected = false;
	TestFalse(TEXT("client not connected rejected"), Ws->PublishMessage(TEXT("/topic/a"), TEXT("x"), {}, Callback));
	TestEqual(TEXT("client disconnect counted"), Ws->GetPublishDroppedCount(), 3);
	Fake->Connected = true;

	// 문자열은 UTF-8로 그대로, 헤더는 그대로 전달한다.
	const FString Text = TEXT("가A");
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Expected(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	TMap<FName, FString> Headers;
	Headers.Add(TEXT("content-type"), TEXT("text/plain"));
	TestTrue(TEXT("connected publish accepted"), Ws->PublishMessage(TEXT("/topic/a"), Text, Headers, FSTOMPRequestCompleted()));
	if (TestEqual(TEXT("one frame sent"), Fake->Sent.Num(), 1))
	{
		TestEqual(TEXT("destination"), Fake->Sent[0].Destination, FString(TEXT("/topic/a")));
		TestTrue(TEXT("utf-8 body"), Fake->Sent[0].Body == Expected);
		TestEqual(TEXT("headers forwarded"), Fake->Sent[0].Headers.FindRef(TEXT("content-type")), FString(TEXT("text/plain")));
		TestFalse(TEXT("no receipt without callback"), Fake->Sent[0].Completion.IsBound());
	}

	const TArray<uint8> Binary = {0, 255, 7};
	Ws->SetConnectionState(EDxWebSocketConnectionState::Subscribing, TEXT("fixture"));
	TestTrue(TEXT("Subscribing allows publish"), Ws->PublishBinaryMessage(TEXT("/topic/b"), Binary, {}, Callback));
	if (TestEqual(TEXT("binary frame sent"), Fake->Sent.Num(), 2))
	{
		TestTrue(TEXT("binary body unchanged"), Fake->Sent[1].Body == Binary);
		TestTrue(TEXT("receipt requested with callback"), Fake->Sent[1].Completion.IsBound());
		Fake->Sent[1].Completion.Execute(true, FString());
		TestEqual(TEXT("callback deferred to GameThread task"), Listener->PublishSuccessCount, 0);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestEqual(TEXT("success delivered once"), Listener->PublishSuccessCount, 1);
	}

	Ws->SetConnectionState(EDxWebSocketConnectionState::Ready, TEXT("fixture"));
	TestTrue(TEXT("publish for failure receipt"), Ws->PublishMessage(TEXT("/topic/a"), TEXT("f"), {}, Callback));
	TestTrue(TEXT("publish for stale receipt"), Ws->PublishMessage(TEXT("/topic/a"), TEXT("s"), {}, Callback));
	if (TestEqual(TEXT("two more frames"), Fake->Sent.Num(), 4))
	{
		Fake->Sent[2].Completion.Execute(false, TEXT("fixture denied"));
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestEqual(TEXT("failure delivered"), Listener->PublishFailureCount, 1);
		// 재연결로 세대가 바뀌면 이전 연결의 receipt는 버린다.
		++Ws->ConnectionGeneration;
		Fake->Sent[3].Completion.Execute(true, FString());
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestEqual(TEXT("stale receipt dropped"), Listener->PublishSuccessCount, 1);
	}

	Ws->SetConnectionState(EDxWebSocketConnectionState::Reconnecting, TEXT("fixture"));
	TestFalse(TEXT("Reconnecting rejects publish"), Ws->PublishMessage(TEXT("/topic/a"), TEXT("x"), {}, Callback));
	Ws->Deinitialize();
	TestFalse(TEXT("shutdown rejects publish"), Ws->PublishMessage(TEXT("/topic/a"), TEXT("x"), {}, Callback));
	TestEqual(TEXT("frames unchanged after rejects"), Fake->Sent.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreReconnectDelayTest, "DTCore.Functional.ReconnectDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreReconnectDelayTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	const int32 OldAttempts = Settings->MaxReconnectAttempts;
	const float OldInitial = Settings->ReconnectInitialDelaySeconds;
	const float OldMax = Settings->ReconnectMaxDelaySeconds;
	const float OldMultiplier = Settings->ReconnectBackoffMultiplier;
	ON_SCOPE_EXIT
	{
		Settings->MaxReconnectAttempts = OldAttempts;
		Settings->ReconnectInitialDelaySeconds = OldInitial;
		Settings->ReconnectMaxDelaySeconds = OldMax;
		Settings->ReconnectBackoffMultiplier = OldMultiplier;
	};
	Ws->bWantsConnection = true;
	FTimerManager& Timers = GI->GetTimerManager();
	auto NextDelay = [&]()
	{
		Ws->TryReconnect();
		const float Rate = Timers.GetTimerRate(Ws->ReconnectTimerHandle);
		Timers.ClearTimer(Ws->ReconnectTimerHandle);
		return Rate;
	};

	Settings->MaxReconnectAttempts = 0;
	Settings->ReconnectInitialDelaySeconds = 0.5f;
	Settings->ReconnectMaxDelaySeconds = 1.5f;
	Settings->ReconnectBackoffMultiplier = 2.0f;
	Ws->RetryCount = 0;
	TestEqual(TEXT("first delay uses initial"), NextDelay(), 0.5f);
	TestEqual(TEXT("second delay doubles"), NextDelay(), 1.0f);
	TestEqual(TEXT("third delay capped at max"), NextDelay(), 1.5f);
	TestEqual(TEXT("unlimited attempts keep scheduling"), Ws->GetReconnectAttemptCount(), 3);

	// ini 값은 ClampMin을 거치지 않으므로 런타임 하한을 확인한다.
	Settings->ReconnectInitialDelaySeconds = 0.0f;
	Settings->ReconnectMaxDelaySeconds = 0.0f;
	Settings->ReconnectBackoffMultiplier = 0.5f;
	Ws->RetryCount = 0;
	TestEqual(TEXT("initial floored to 0.1"), NextDelay(), 0.1f);
	TestEqual(TEXT("multiplier below 1 treated as 1 and max raised to initial"), NextDelay(), 0.1f);

	Settings->ReconnectInitialDelaySeconds = 2.0f;
	Settings->ReconnectMaxDelaySeconds = 1.0f;
	Settings->ReconnectBackoffMultiplier = 3.0f;
	Ws->RetryCount = 0;
	TestEqual(TEXT("max below initial raised to initial"), NextDelay(), 2.0f);
	Ws->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreWidgetNavigationTest, "DTCore.Functional.WidgetNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreWidgetNavigationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UDxWidget> Plain(NewObject<UDxWidget>());
	TestFalse(TEXT("base widget does not handle navigation"), Plain->NavigateTo(TEXT("A")));
	TestTrue(TEXT("base widget current stays None"), Plain->GetCurrentSubTarget().IsNone());

	TStrongObjectPtr<UDxCoreTestNavigationWidget> Widget(NewObject<UDxCoreTestNavigationWidget>());
	TestFalse(TEXT("None target rejected"), Widget->NavigateTo(NAME_None));
	TestTrue(TEXT("first screen without history"), Widget->NavigateTo(TEXT("Home"), false));
	TestFalse(TEXT("first screen not recorded"), Widget->CanNavigateBack());
	TestTrue(TEXT("same target succeeds"), Widget->NavigateTo(TEXT("Home")));
	TestEqual(TEXT("same target skips handler"), Widget->HandledTargets.Num(), 1);
	TestFalse(TEXT("same target not recorded"), Widget->CanNavigateBack());

	TestTrue(TEXT("move to detail"), Widget->NavigateTo(TEXT("Detail")));
	TestEqual(TEXT("current detail"), Widget->GetCurrentSubTarget(), FName(TEXT("Detail")));
	TestTrue(TEXT("home recorded"), Widget->CanNavigateBack());

	Widget->RejectedTargets.Add(TEXT("Blocked"));
	TestFalse(TEXT("rejected target fails"), Widget->NavigateTo(TEXT("Blocked")));
	TestEqual(TEXT("rejected keeps current"), Widget->GetCurrentSubTarget(), FName(TEXT("Detail")));

	TestTrue(TEXT("back to home"), Widget->NavigateBack());
	TestEqual(TEXT("current home"), Widget->GetCurrentSubTarget(), FName(TEXT("Home")));
	TestFalse(TEXT("history consumed"), Widget->CanNavigateBack());
	TestFalse(TEXT("back with empty history fails"), Widget->NavigateBack());

	TestTrue(TEXT("move to detail again"), Widget->NavigateTo(TEXT("Detail")));
	Widget->RejectedTargets.Add(TEXT("Home"));
	TestFalse(TEXT("rejected back fails"), Widget->NavigateBack());
	TestTrue(TEXT("rejected back keeps history"), Widget->CanNavigateBack());
	TestEqual(TEXT("rejected back keeps current"), Widget->GetCurrentSubTarget(), FName(TEXT("Detail")));
	Widget->RejectedTargets.Remove(TEXT("Home"));

	TestTrue(TEXT("unrecorded move"), Widget->NavigateTo(TEXT("Side"), false));
	TestTrue(TEXT("back skips unrecorded detail"), Widget->NavigateBack());
	TestEqual(TEXT("back returns to recorded home"), Widget->GetCurrentSubTarget(), FName(TEXT("Home")));

	for (int32 Index = 0; Index < 40; ++Index)
	{
		Widget->NavigateTo(FName(TEXT("Page"), Index + 1));
	}
	int32 BackCount = 0;
	while (Widget->NavigateBack())
	{
		++BackCount;
	}
	TestEqual(TEXT("history capped at 32"), BackCount, 32);

	Widget->NavigateTo(TEXT("Detail"));
	Widget->NavigateTo(TEXT("Other"));
	Widget->ClearNavigationHistory();
	TestFalse(TEXT("clear empties history"), Widget->CanNavigateBack());
	TestEqual(TEXT("clear keeps current"), Widget->GetCurrentSubTarget(), FName(TEXT("Other")));

	Widget->NavigateTo(TEXT("Detail"));
	TestTrue(TEXT("close starts"), Widget->TryBeginClose());
	Widget->NotifyClosedOnce();
	TestTrue(TEXT("close resets current"), Widget->GetCurrentSubTarget().IsNone());
	TestFalse(TEXT("close resets history"), Widget->CanNavigateBack());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreFocusOrOpenWidgetTest, "DTCore.Functional.FocusOrOpenWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreFocusOrOpenWidgetTest::RunTest(const FString&)
{
	TStrongObjectPtr<UDxCoreTestGameInstance> GI(NewObject<UDxCoreTestGameInstance>());
	TStrongObjectPtr<UDxWidgetSubsystem> Widgets(NewObject<UDxWidgetSubsystem>(GI.Get()));
	TestNull(TEXT("null actor find"), Widgets->FindOpenWidget(nullptr, 0));
	TestNull(TEXT("null actor focus"), Widgets->FocusOrOpenWidget(nullptr, TEXT("A")));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	Context.OwningGameInstance = GI.Get();
	GI->AttachWorldContext(&Context);
	World->SetGameInstance(GI.Get());
	Context.GameViewport = NewObject<UGameViewportClient>(GEngine);
	auto Overlay = SNew(SOverlay);
	Context.GameViewport->SetViewportOverlayWidget(nullptr, Overlay);
	auto* Main = NewObject<UDxWidget>(World);
	Main->WidgetTree = NewObject<UWidgetTree>(Main);
	auto* Canvas = Main->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AddWidgetPanel"));
	Main->WidgetTree->RootWidget = Canvas;
	Main->AddToViewport();
	Widgets->MainWidgetInstance = Main;
	ON_SCOPE_EXIT
	{
		Main->RemoveFromParent();
		Widgets->Deinitialize();
		GI->AttachWorldContext(nullptr);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	auto* Actor = NewObject<AInteractableActor>(World);
	Actor->WidgetFlag = 7;
	Actor->WidgetConfig = NewObject<UDxWidgetConfigData>(Actor);
	FWidgetInfo Info;
	Info.WidgetClass = UDxCoreTestNavigationWidget::StaticClass();
	Info.Position = FVector2D(10, 20);
	FWidgetInfoList List;
	List.Widgets.Add(Info);
	Actor->WidgetConfig->WidgetMap.Add(Actor->WidgetFlag, List);

	TestNull(TEXT("nothing open yet"), Widgets->FindOpenWidget(Actor, Actor->WidgetFlag));
	auto* Opened = Cast<UDxCoreTestNavigationWidget>(Widgets->FocusOrOpenWidget(Actor, TEXT("Tab1")));
	if (!TestNotNull(TEXT("focus opens widget"), Opened))
	{
		return false;
	}
	TestEqual(TEXT("open navigates to sub target"), Opened->GetCurrentSubTarget(), FName(TEXT("Tab1")));
	TestEqual(TEXT("find returns opened widget"), Widgets->FindOpenWidget(Actor, Actor->WidgetFlag), static_cast<UDxWidget*>(Opened));
	TestNull(TEXT("other flag not matched"), Widgets->FindOpenWidget(Actor, 8));
	const int32 OpenCount = Widgets->GetOpenWidgets().Num();

	TestEqual(TEXT("second focus reuses widget"), Widgets->FocusOrOpenWidget(Actor, TEXT("Tab2")), static_cast<UDxWidget*>(Opened));
	TestEqual(TEXT("no duplicate opened"), Widgets->GetOpenWidgets().Num(), OpenCount);
	TestEqual(TEXT("reuse navigates"), Opened->GetCurrentSubTarget(), FName(TEXT("Tab2")));
	TestTrue(TEXT("reuse records previous tab"), Opened->CanNavigateBack());

	TestEqual(TEXT("None sub target only focuses"), Widgets->FocusOrOpenWidget(Actor, NAME_None), static_cast<UDxWidget*>(Opened));
	TestEqual(TEXT("None keeps current tab"), Opened->GetCurrentSubTarget(), FName(TEXT("Tab2")));

	Opened->RemoveFromParent();
	TestNull(TEXT("detached widget not found"), Widgets->FindOpenWidget(Actor, Actor->WidgetFlag));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreOnScreenDebugToggleTest, "DTCore.Functional.OnScreenDebugToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreOnScreenDebugToggleTest::RunTest(const FString&)
{
	// UEngine의 화면 메시지 목록은 비공개라 출력 자체는 관찰하지 못한다. 설정 값 전달과 매크로 실행만 확인한다.
	UDTCoreSettings* Settings = GetMutableDefault<UDTCoreSettings>();
	const bool bSavedSetting = Settings->bShowOnScreenDebugMessages;
	ON_SCOPE_EXIT
	{
		Settings->bShowOnScreenDebugMessages = bSavedSetting;
	};

	Settings->bShowOnScreenDebugMessages = true;
	TestTrue(TEXT("helper follows enabled setting"), UDxLogSubsystem::ShouldShowOnScreenDebugMessages());
	DX_LOG(nullptr, TEXT("DTCore on-screen toggle test: on"));

	Settings->bShowOnScreenDebugMessages = false;
	TestFalse(TEXT("helper follows disabled setting"), UDxLogSubsystem::ShouldShowOnScreenDebugMessages());
	DX_LOG(nullptr, TEXT("DTCore on-screen toggle test: off"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreFreeviewWindowTest, "DTCore.Functional.FreeviewWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreFreeviewWindowTest::RunTest(const FString&)
{
	const FVector2D Window(200.0, 100.0);
	const FVector2D Bounds(1000.0, 600.0);
	TestEqual(TEXT("inside stays"), UDxWidgetFreeview::ClampWindowPosition(FVector2D(10.0, 20.0), Window, Bounds), FVector2D(10.0, 20.0));
	TestEqual(TEXT("left top clamps"), UDxWidgetFreeview::ClampWindowPosition(FVector2D(-5.0, -5.0), Window, Bounds, 4.0f), FVector2D(4.0, 4.0));
	TestEqual(TEXT("right bottom clamps"), UDxWidgetFreeview::ClampWindowPosition(FVector2D(5000.0, 5000.0), Window, Bounds, 4.0f), FVector2D(796.0, 496.0));
	TestEqual(TEXT("oversized keeps title visible"), UDxWidgetFreeview::ClampWindowPosition(FVector2D(50.0, 50.0), FVector2D(2000.0, 2000.0), Bounds), FVector2D::ZeroVector);

	TStrongObjectPtr<UDxCoreTestFreeviewWidget> Unparented(NewObject<UDxCoreTestFreeviewWidget>());
	TestFalse(TEXT("no parent cannot move"), Unparented->SetWindowPosition(FVector2D(1.0, 1.0)));

	TStrongObjectPtr<UWidgetTree> Tree(NewObject<UWidgetTree>());
	UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	TStrongObjectPtr<UDxCoreTestFreeviewWidget> Widget(NewObject<UDxCoreTestFreeviewWidget>());
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Canvas->AddChild(Widget.Get()));
	if (!TestNotNull(TEXT("canvas slot"), CanvasSlot)) return false;
	CanvasSlot->SetAnchors(FAnchors(0.0f));
	CanvasSlot->SetAlignment(FVector2D::ZeroVector);

	TestTrue(TEXT("set position"), Widget->SetWindowPosition(FVector2D(100.0, 50.0)));
	TestEqual(TEXT("window position"), Widget->GetWindowPosition(), FVector2D(100.0, 50.0));
	TestEqual(TEXT("slot position"), CanvasSlot->GetPosition(), FVector2D(100.0, 50.0));
	TestTrue(TEXT("set outside"), Widget->SetWindowPosition(FVector2D(5000.0, -10.0)));
	TestEqual(TEXT("set position clamps"), Widget->GetWindowPosition(), FVector2D(800.0, 0.0));

	// 앵커·정렬이 있어도 위치는 창 왼쪽 위 기준이다.
	CanvasSlot->SetAnchors(FAnchors(0.5f));
	CanvasSlot->SetAlignment(FVector2D(0.5, 0.5));
	TestTrue(TEXT("anchored position"), Widget->SetWindowPosition(FVector2D(100.0, 50.0)));
	TestEqual(TEXT("anchored slot offset"), CanvasSlot->GetPosition(), FVector2D(100.0 - 500.0 + 100.0, 50.0 - 300.0 + 50.0));
	TestEqual(TEXT("anchored window position"), Widget->GetWindowPosition(), FVector2D(100.0, 50.0));

	CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	TestFalse(TEXT("stretched anchors cannot move"), Widget->SetWindowPosition(FVector2D(1.0, 1.0)));
	CanvasSlot->SetAnchors(FAnchors(0.0f));
	CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	TestTrue(TEXT("reset position"), Widget->SetWindowPosition(FVector2D(100.0, 50.0)));

	// GetCachedWidget은 약한 참조이므로 테스트 동안 Slate 위젯을 붙잡아 둔다.
	const TSharedRef<SWidget> HeldSlateWidget = Widget->TakeWidget();
	Widget->OnWindowMoved.AddDynamic(Widget.Get(), &UDxCoreTestFreeviewWidget::HandleMoved);
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(200.0f, 100.0f), FSlateLayoutTransform(FVector2f(100.0f, 50.0f)));
	const TSet<FKey> Pressed{EKeys::LeftMouseButton};
	const TSet<FKey> Released;
	auto MakeEvent = [](const FVector2D& Position, const TSet<FKey>& Buttons, const FKey& Effecting)
	{
		return FPointerEvent(0, Position, Position, Buttons, Effecting, 0.0f, FModifierKeysState());
	};

	Widget->TestDown(Geometry, MakeEvent(FVector2D(150.0, 120.0), Pressed, EKeys::LeftMouseButton));
	TestFalse(TEXT("body press does not drag"), Widget->IsDraggingWindow());
	Widget->TestDown(Geometry, MakeEvent(FVector2D(150.0, 60.0), Pressed, EKeys::RightMouseButton));
	TestFalse(TEXT("right press does not drag"), Widget->IsDraggingWindow());

	Widget->bMovable = false;
	Widget->TestDown(Geometry, MakeEvent(FVector2D(150.0, 60.0), Pressed, EKeys::LeftMouseButton));
	TestFalse(TEXT("not movable does not drag"), Widget->IsDraggingWindow());
	Widget->bMovable = true;

	const FReply DownReply = Widget->TestDown(Geometry, MakeEvent(FVector2D(150.0, 60.0), Pressed, EKeys::LeftMouseButton));
	TestTrue(TEXT("title press drags"), Widget->IsDraggingWindow());
	TestTrue(TEXT("title press captures mouse"), DownReply.GetMouseCaptor().IsValid());
	TestTrue(TEXT("title press sets focus"), DownReply.GetUserFocusRecepient().IsValid());

	Widget->TestMove(Geometry, MakeEvent(FVector2D(250.0, 160.0), Pressed, EKeys::Invalid));
	TestEqual(TEXT("drag keeps grab offset"), Widget->GetWindowPosition(), FVector2D(200.0, 150.0));
	Widget->TestMove(Geometry, MakeEvent(FVector2D(9000.0, 9000.0), Pressed, EKeys::Invalid));
	TestEqual(TEXT("drag clamps"), Widget->GetWindowPosition(), FVector2D(800.0, 500.0));
	TestEqual(TEXT("no notification during drag"), Widget->MovedCount, 0);

	const FReply UpReply = Widget->TestUp(Geometry, MakeEvent(FVector2D(9000.0, 9000.0), Released, EKeys::LeftMouseButton));
	TestFalse(TEXT("release ends drag"), Widget->IsDraggingWindow());
	TestTrue(TEXT("release frees mouse"), UpReply.ShouldReleaseMouse());
	TestEqual(TEXT("moved notified once"), Widget->MovedCount, 1);
	TestEqual(TEXT("moved position"), Widget->LastMovedPosition, FVector2D(800.0, 500.0));

	const FVector2D ReleasedPosition = Widget->GetWindowPosition();
	Widget->TestMove(Geometry, MakeEvent(FVector2D(10.0, 10.0), Pressed, EKeys::Invalid));
	TestEqual(TEXT("move without drag does nothing"), Widget->GetWindowPosition(), ReleasedPosition);

	Widget->TestDown(Geometry, MakeEvent(FVector2D(150.0, 60.0), Pressed, EKeys::LeftMouseButton));
	Widget->TestCaptureLost();
	TestFalse(TEXT("capture lost ends drag"), Widget->IsDraggingWindow());
	TestEqual(TEXT("unmoved drag is not notified"), Widget->MovedCount, 1);

	Widget->TestDown(Geometry, MakeEvent(FVector2D(150.0, 60.0), Pressed, EKeys::LeftMouseButton));
	const FReply MissedUp = Widget->TestMove(Geometry, MakeEvent(FVector2D(150.0, 60.0), Released, EKeys::Invalid));
	TestFalse(TEXT("move without button ends drag"), Widget->IsDraggingWindow());
	TestTrue(TEXT("missed release frees mouse"), MissedUp.ShouldReleaseMouse());

	Widget->TestBoundsSize = FVector2D(500.0, 300.0);
	Widget->TestTick(Geometry);
	TestEqual(TEXT("smaller bounds clamp again"), Widget->GetWindowPosition(), FVector2D(300.0, 200.0));
	Widget->bClampToBounds = false;
	Widget->TestBoundsSize = FVector2D(100.0, 100.0);
	Widget->TestTick(Geometry);
	TestEqual(TEXT("clamp off keeps slot position"), CanvasSlot->GetPosition(), FVector2D(300.0, 200.0));
	return true;
}

#endif
