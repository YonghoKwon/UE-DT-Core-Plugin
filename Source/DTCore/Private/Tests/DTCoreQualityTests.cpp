#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "DTCoreTestTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Core/DTCoreSettings.h"
#include "Core/DxApiSubsystem.h"
#include "Core/DxDataSubsystem.h"
#include "Core/DxLogSubsystem.h"
#include "Core/DxWidgetSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/StaticMeshComponent.h"
#include "InteractableActor/InteractableActor.h"
#include "Player/DxPlayerControllerBase.h"
#include "Manager/DxLevelStruct.h"
#include "IStompClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Async/TaskGraphInterfaces.h"
#include "Subsystems/SubsystemCollection.h"
#include "Widgets/SOverlay.h"

class FDTCoreQualityStomp : public IStompClient
{
public:
	bool Connected = true;
	TArray<FStompRequestCompleted> Completions;
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
	FString Subscribe(const FString&, const FStompSubscriptionEvent&, const FStompRequestCompleted& Callback) override
	{
		Completions.Add(Callback);
		return FString::FromInt(Completions.Num());
	}
	void Unsubscribe(FString, const FStompRequestCompleted&) override
	{
	}
	void Send(const FString&, const FStompBuffer&, const FStompHeader&, const FStompRequestCompleted&) override
	{
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreConnectionStateTest, "DTCore.Quality.ConnectionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreConnectionStateTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	TStrongObjectPtr<UDxCoreTestListener> Listener(NewObject<UDxCoreTestListener>());
	auto Fake = MakeShared<FDTCoreQualityStomp>();
	Ws->StompClient = Fake;
	Ws->bWantsConnection = true;
	Ws->OnConnectionStateChanged.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveState);
	Ws->OnConnected.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveReady);
	TestEqual(TEXT("initial state"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Disconnected);
	// 실제 공개 진입점을 쓰되 이미 주입된 fake를 연결한다.
	Ws->ConnectStompClient({});
	TestEqual(TEXT("connect requested"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Connecting);
	Ws->TopicRouteMap.Add(TEXT("quality"), ETopicRouteType::WebSocket);
	Ws->HandleOnConnected(TEXT("1.2"), TEXT("fixture"), FString());
	TestEqual(TEXT("receipt pending"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Subscribing);
	Fake->Completions[0].Execute(true, FString());
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("receipt complete"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Ready);
	TestEqual(TEXT("legacy connected once"), Listener->ReadyCount, 1);
	Ws->HandleOnClosed(TEXT("fixture close"));
	TestEqual(TEXT("close schedules reconnect"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Reconnecting);
	TestEqual(TEXT("scheduled attempts observable"), Ws->GetReconnectAttemptCount(), 1);
	GI->GetTimerManager().ClearTimer(Ws->ReconnectTimerHandle);
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	const int32 OldAttempts = Settings->MaxReconnectAttempts;
	ON_SCOPE_EXIT
	{
		Settings->MaxReconnectAttempts = OldAttempts;
	};
	Settings->MaxReconnectAttempts = 1;
	Ws->TryReconnect();
	Ws->TryReconnect();
	TestEqual(TEXT("exhausted"), Ws->GetConnectionState(), EDxWebSocketConnectionState::GaveUp);
	TestEqual(TEXT("GaveUp event exactly once"), Listener->States.FilterByPredicate([](auto State)
																	 { return State == EDxWebSocketConnectionState::GaveUp; })
													 .Num(),
		1);
	Ws->DisconnectStompClient({});
	TestEqual(TEXT("manual disconnect"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Disconnected);
	Ws->bWantsConnection = true;
	Listener->DisconnectTarget = Ws.Get();
	Listener->DisconnectAtState = EDxWebSocketConnectionState::Connecting;
	Ws->ConnectStompClient({});
	TestEqual(TEXT("Connecting event can cancel connection"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Disconnected);
	TestFalse(TEXT("cancelled fake client is not connected again"), Fake->IsConnected());
	Listener->DisconnectAtState = EDxWebSocketConnectionState::Subscribing;
	Ws->bWantsConnection = true;
	Ws->bSubscriptionBatchStarted = false;
	const int32 OldReceipts = Fake->Completions.Num();
	Ws->HandleOnConnected(TEXT("1.2"), TEXT("cancel"), FString());
	TestEqual(TEXT("Subscribing event can cancel batch"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Disconnected);
	TestEqual(TEXT("cancelled batch does not issue receipts"), Fake->Completions.Num(), OldReceipts);
	const int32 Events = Listener->States.Num();
	Ws->Deinitialize();
	TestEqual(TEXT("shutdown never broadcasts"), Listener->States.Num(), Events);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreReceiptTimeoutTest, "DTCore.Quality.ReceiptTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreReceiptTimeoutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	TStrongObjectPtr<UDxCoreTestListener> Listener(NewObject<UDxCoreTestListener>());
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	const float OldTimeout = Settings->SubscribeReceiptTimeoutSeconds;
	ON_SCOPE_EXIT
	{
		Settings->SubscribeReceiptTimeoutSeconds = OldTimeout;
	};
	auto Fake = MakeShared<FDTCoreQualityStomp>();
	Ws->StompClient = Fake;
	Ws->bWantsConnection = true;
	Ws->OnConnected.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveReady);
	Ws->TopicRouteMap.Add(TEXT("a"), ETopicRouteType::Api);
	Ws->TopicRouteMap.Add(TEXT("b"), ETopicRouteType::WebSocket);
	Settings->SubscribeReceiptTimeoutSeconds = 0;
	Ws->HandleOnConnected(TEXT("1.2"), TEXT("disabled"), FString());
	TestFalse(TEXT("default does not arm timeout"), GI->GetTimerManager().IsTimerActive(Ws->SubscribeTimeoutHandle));
	TestEqual(TEXT("zero timeout retains waiting"), Listener->ReadyCount, 0);
	TestEqual(TEXT("all receipts remain pending"), Ws->PendingSubscribeCount, 2);
	Ws->DisconnectStompClient({});
	Ws->bWantsConnection = true;
	Ws->bSubscriptionBatchStarted = false;
	Ws->bConnectedBroadcast = false;
	Settings->SubscribeReceiptTimeoutSeconds = 0.1f;
	Ws->HandleOnConnected(TEXT("1.2"), TEXT("timed"), FString());
	TestTrue(TEXT("positive timeout arms timer"), GI->GetTimerManager().IsTimerActive(Ws->SubscribeTimeoutHandle));
	Fake->Completions[2].Execute(false, TEXT("fixture denied"));
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	// 실제 TimerManager delegate를 실행하여 설정 적용도 검증한다.
	GI->GetTimerManager().Tick(0.01f);
	++GFrameCounter;
	GI->GetTimerManager().Tick(0.2f);
	TestEqual(TEXT("one failure plus one timeout"), Ws->GetLastSubscribeFailureCount(), 2);
	TestEqual(TEXT("timeout completes batch once"), Listener->ReadyCount, 1);
	TestEqual(TEXT("Ready includes timed-out batch"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Ready);
	Fake->Completions[3].Execute(true, FString());
	Fake->Completions[3].Execute(false, TEXT("duplicate"));
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("late receipts do not rebroadcast"), Listener->ReadyCount, 1);
	TestEqual(TEXT("late receipts do not change failure count"), Ws->GetLastSubscribeFailureCount(), 2);
	Ws->DisconnectStompClient({});
	Ws->HandleSubscribeTimeout(Ws->ConnectionGeneration - 1);
	TestEqual(TEXT("invalidated timeout cannot complete"), Listener->ReadyCount, 1);
	Ws->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreHttpSettingsTest, "DTCore.Quality.HttpSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreHttpSettingsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxApiSubsystem> Api(NewObject<UDxApiSubsystem>(GI.Get()));
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	const float OldTimeout = Settings->HttpRequestTimeoutSeconds;
	const float OldDelay = Settings->HttpInitialRetryDelaySeconds;
	const int32 OldAttempts = Settings->HttpMaxAttempts;
	ON_SCOPE_EXIT
	{
		Settings->HttpRequestTimeoutSeconds = OldTimeout;
		Settings->HttpInitialRetryDelaySeconds = OldDelay;
		Settings->HttpMaxAttempts = OldAttempts;
	};
	Api->HttpModule = &FHttpModule::Get();
	TOptional<float> ObservedTimeout;
	Api->RequestProbeForTests = [&ObservedTimeout](const auto& Request)
	{
		ObservedTimeout = Request->GetTimeout();
		return false;
	};
	Settings->HttpRequestTimeoutSeconds = 0;
	Api->DxHttpCall(TEXT("http://fixture.invalid"), TEXT("GET"), FString(), {}, {});
	TestFalse(TEXT("zero preserves engine default"), ObservedTimeout.IsSet());
	Settings->HttpRequestTimeoutSeconds = 2.5f;
	Api->DxHttpCall(TEXT("http://fixture.invalid"), TEXT("GET"), FString(), {}, {});
	TestTrue(TEXT("positive timeout applied to request"), ObservedTimeout.IsSet() && ObservedTimeout.GetValue() == 2.5f);
	Settings->HttpMaxAttempts = 2;
	Settings->HttpInitialRetryDelaySeconds = 0.25f;
	UDxApiSubsystem::FDxHttpRequestContext Context;
	Context.Generation = Api->RequestGeneration;
	Api->InternalOnResponseReceived(nullptr, nullptr, false, Context);
	TestEqual(TEXT("retry scheduled"), Api->PendingRetryTimers.Num(), 1);
	TestEqual(TEXT("configured delay applied"), GI->GetTimerManager().GetTimerRate(Api->PendingRetryTimers[0]), 0.25f);
	Context.AttemptIndex = 1;
	Api->InternalOnResponseReceived(nullptr, nullptr, false, Context);
	TestEqual(TEXT("configured attempt cap stops retry"), Api->PendingRetryTimers.Num(), 1);
	Api->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreDataStatisticsTest, "DTCore.Quality.DataStatistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreDataStatisticsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxDataSubsystem> Data(NewObject<UDxDataSubsystem>(GI.Get()));
	auto* Api = NewObject<UDxCoreTestApiMessage>(Data.Get());
	auto* Ws = NewObject<UDxCoreTestSlowTransaction>(Data.Get());
	Data->ApiMessageMap.Add(TEXT("fixture/read"), Api);
	Data->TransactionCodeMessageMap.Add(TEXT("fixture"), Ws);
	Data->CachedHandlerApiMessageMap = MakeShared<TMap<FString, UApiMessage*>>();
	Data->CachedHandlerApiMessageMap->Add(TEXT("fixture/read"), Api);
	Data->CachedHandlerTransactionCodeMessageMap = MakeShared<TMap<FString, UTransactionCodeMessage*>>();
	Data->CachedHandlerTransactionCodeMessageMap->Add(TEXT("fixture"), Ws);
	Data->EnqueueApiData(TEXT("{\"meta\":{\"resource\":\"fixture\",\"action\":\"read\"}}"));
	Data->EnqueueApiData(TEXT("invalid"));
	Data->EnqueueApiData(TEXT("{}"));
	Data->EnqueueApiData(TEXT("{\"meta\":{}}"));
	Data->EnqueueWebSocketData(TEXT("{\"MESSAGE_ID\":\"fixture\"}"));
	Data->EnqueueWebSocketData(TEXT("invalid"));
	Data->EnqueueWebSocketData(TEXT("{}"));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Data->EnqueueApiData(TEXT("{\"meta\":{\"resource\":\"unknown\",\"action\":\"read\"}}"));
		Data->EnqueueWebSocketData(TEXT("{\"MESSAGE_ID\":\"unknown\"}"));
	}
	TestEqual(TEXT("API queue before extraction"), Data->GetStatistics().QueuedApi, int64(6));
	TestEqual(TEXT("WS queue before extraction"), Data->GetStatistics().QueuedWebSocket, int64(5));
	AddExpectedError(TEXT("등록 핸들러 없음"), EAutomationExpectedErrorFlags::Contains, 2);
	Data->Tick(0);
	Data->ApiWorker.Wait();
	Data->WebSocketWorker.Wait();
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	const auto Stats = Data->GetStatistics();
	TestEqual(TEXT("API applied"), Stats.ProcessedApi, int64(1));
	TestEqual(TEXT("WS applied"), Stats.ProcessedWebSocket, int64(1));
	TestEqual(TEXT("JSON failures"), Stats.JsonParseFailures, int64(2));
	TestEqual(TEXT("missing dispatch fields"), Stats.MissingDispatchKeys, int64(3));
	TestEqual(TEXT("unknown keys count every discarded message"), Stats.UnknownHandlerKeys, int64(4));
	TestEqual(TEXT("legacy count retains WS meaning"), Data->TotalProcessedCount.GetValue(), 1);
	TestEqual(TEXT("API queue drained"), Stats.QueuedApi, int64(0));
	TestEqual(TEXT("WS queue drained"), Stats.QueuedWebSocket, int64(0));
	Data->EnqueueApiData(TEXT("{}"));
	Data->Deinitialize();
	TestEqual(TEXT("shutdown discard decrements queue"), Data->GetStatistics().QueuedApi, int64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreLogSettingsTest, "DTCore.Quality.LogSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreLogSettingsTest::RunTest(const FString&)
{
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	const FString OldDirectory = Settings->LogDirectory;
	const int32 OldRetention = Settings->LogRetentionDays;
	ON_SCOPE_EXIT
	{
		Settings->LogDirectory = OldDirectory;
		Settings->LogRetentionDays = OldRetention;
	};
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxLogSubsystem> Log(NewObject<UDxLogSubsystem>(GI.Get()));
	Settings->LogDirectory.Reset();
	TestEqual(TEXT("empty retains legacy directory"), Log->GetLogDirectory(), FPaths::LaunchDir() / TEXT("Logs/CustomLogs"));
	Settings->LogDirectory = TEXT("Logs/Quality90");
	TestEqual(TEXT("relative uses LaunchDir"), Log->GetLogDirectory(), FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), Settings->LogDirectory));
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/quality90/Retention") / FGuid::NewGuid().ToString());
	Settings->LogDirectory = Directory;
	Settings->LogRetentionDays = 2;
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString OldFile = Directory / (TEXT("DxLog_") + (FDateTime::Now() - FTimespan::FromDays(3)).ToString(TEXT("%Y-%m-%d")) + TEXT(".log"));
	const FString RecentFile = Directory / (TEXT("DxLog_") + FDateTime::Now().ToString(TEXT("%Y-%m-%d")) + TEXT(".log"));
	FFileHelper::SaveStringToFile(TEXT("old"), *OldFile);
	FFileHelper::SaveStringToFile(TEXT("recent"), *RecentFile);
	FSubsystemCollection<UGameInstanceSubsystem> Collection;
	Log->Initialize(Collection);
	TestTrue(TEXT("configured directory cached"), Log->CachedLogDirectory == Directory);
	TestTrue(TEXT("flush completes initialization and cleanup"), Log->FlushLogs());
	TestFalse(TEXT("old named log removed"), IFileManager::Get().FileExists(*OldFile));
	TestTrue(TEXT("recent named log retained"), IFileManager::Get().FileExists(*RecentFile));
	Log->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreWidgetPruningTest, "DTCore.Quality.WidgetPruning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreWidgetPruningTest::RunTest(const FString&)
{
	TStrongObjectPtr<UDxCoreTestGameInstance> GI(NewObject<UDxCoreTestGameInstance>());
	TStrongObjectPtr<UDxWidgetSubsystem> Widgets(NewObject<UDxWidgetSubsystem>(GI.Get()));
	TStrongObjectPtr<UDxCoreTestCloseOrderWidget> Detached(NewObject<UDxCoreTestCloseOrderWidget>());
	auto Trace = MakeShared<TArray<FString>>();
	Detached->CloseTrace = Trace;
	Widgets->RegisterWidget(Detached.Get());
	Widgets->BringToFront(Detached.Get());
	TestTrue(TEXT("BringToFront removes detached entry"), Widgets->GetOpenWidgets().IsEmpty());
	Widgets->RegisterWidget(Detached.Get());
	Widgets->IsMouseOverAnyWidget();
	TestTrue(TEXT("hover query removes detached entry"), Widgets->GetOpenWidgets().IsEmpty());
	Widgets->RegisterWidget(Detached.Get());
	TestNull(TEXT("duplicate check never returns old offscreen instance"), Widgets->CreateWidgetInternal(Detached->GetClass(), FVector2D::ZeroVector, nullptr, nullptr, 0));
	TestTrue(TEXT("duplicate lookup pruned stale widget"), Widgets->GetOpenWidgets().IsEmpty());
	Widgets->RegisterWidget(Detached.Get());
	Widgets->SwitchUIMode(EDxViewMode::None);
	TestTrue(TEXT("mode switch prunes stale widget"), Widgets->GetOpenWidgets().IsEmpty());
	TestEqual(TEXT("pruning never calls close hook"), Trace->Num(), 0);

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
	auto* StaticWidget = Main->WidgetTree->ConstructWidget<UDxCoreTestCloseOrderWidget>();
	StaticWidget->CloseTrace = Trace;
	Canvas->AddChild(StaticWidget);
	Main->AddToViewport();
	Widgets->MainWidgetInstance = Main;
	TestTrue(TEXT("fixture main is in viewport under NullRHI"), Main->IsInViewport());
	Widgets->RegisterWidget(StaticWidget);
	Widgets->IsMouseOverAnyWidget();
	TestTrue(TEXT("registered static child retained in active Main tree"), Widgets->GetOpenWidgets().Contains(StaticWidget));
	StaticWidget->RemoveFromParent();
	Widgets->IsMouseOverAnyWidget();
	TestTrue(TEXT("removed static child is pruned even though its Outer tree is still active"), Widgets->GetOpenWidgets().IsEmpty());
	Canvas->AddChild(StaticWidget);
	Widgets->RegisterWidget(StaticWidget);
	auto* First = Widgets->CreateWidgetInternal(UDxWidget::StaticClass(), FVector2D(10, 20), nullptr, nullptr, 42);
	TestNotNull(TEXT("fixture opens dynamic widget"), First);
	if (First)
	{
		TestEqual(TEXT("live duplicate returns existing widget"), Widgets->CreateWidgetInternal(UDxWidget::StaticClass(), FVector2D(10, 20), nullptr, nullptr, 42), First);
		First->RemoveFromParent();
		auto* Replacement = Widgets->CreateWidgetInternal(UDxWidget::StaticClass(), FVector2D(10, 20), nullptr, nullptr, 42);
		TestTrue(TEXT("direct RemoveFromParent allows a fresh widget"), Replacement && Replacement != First);
	}
	Widgets->LevelDataTable = NewObject<UDataTable>();
	Widgets->LevelDataTable->RowStruct = FDxLevelStruct::StaticStruct();
	FDxLevelStruct Row;
	Row.DxViewMode = EDxViewMode::None;
	Row.UseMainWidget = UDxWidget::StaticClass();
	Widgets->LevelDataTable->AddRow(TEXT("first"), Row);
	Row.UseMainWidget = UDxCoreTestCloseOrderWidget::StaticClass();
	Widgets->LevelDataTable->AddRow(TEXT("second"), Row);
	TSubclassOf<UDxWidget> ExpectedLastClass;
	Widgets->LevelDataTable->ForeachRow<FDxLevelStruct>(TEXT("fixture order"), [&ExpectedLastClass](const FName&, const FDxLevelStruct& Item)
		{ ExpectedLastClass = Item.UseMainWidget; });
	AddExpectedError(TEXT("일치 행 2개, 마지막 행 적용"), EAutomationExpectedErrorFlags::Contains, 1);
	Widgets->SwitchUIMode(EDxViewMode::None);
	TestTrue(TEXT("removed Main descendants pruned"), Widgets->GetOpenWidgets().IsEmpty());
	TestTrue(TEXT("last matching table iteration wins"), Widgets->GetMainWidget() && Widgets->GetMainWidget()->GetClass() == ExpectedLastClass);
	TestEqual(TEXT("mode replacement never calls close hooks"), Trace->Num(), 0);
	if (Widgets->GetMainWidget())
	{
		Widgets->GetMainWidget()->RemoveFromParent();
	}
	Widgets->Deinitialize();
	GI->AttachWorldContext(nullptr);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreInputSettingsTest, "DTCore.Quality.InputSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreInputSettingsTest::RunTest(const FString&)
{
	auto* Actor = NewObject<AInteractableActor>();
	auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
	TestEqual(TEXT("default highlight"), Actor->HighlightStencilValue, 100);
	TestEqual(TEXT("default error"), Actor->ErrorStencilValue, 254);
	Actor->HighlightStencilValue = 42;
	Actor->ErrorStencilValue = 43;
	AInteractableActor::HighlightActor(true, {Mesh}, false);
	TestEqual(TEXT("static API resolves owner setting"), Mesh->CustomDepthStencilValue, 42);
	Actor->HighlightSingleMesh(true, Mesh, true);
	TestEqual(TEXT("single mesh setting"), Mesh->CustomDepthStencilValue, 43);
	const auto* Controller = GetDefault<ADxPlayerControllerBase>();
	TestEqual(TEXT("default priority channel"), Controller->PriorityTraceChannel.GetValue(), ECC_GameTraceChannel1);
	TestEqual(TEXT("default object type count"), Controller->TraceObjectTypes.Num(), 5);
	return true;
}

#endif
