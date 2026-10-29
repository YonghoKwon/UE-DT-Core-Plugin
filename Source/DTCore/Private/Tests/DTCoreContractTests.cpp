#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "DTCoreTestTypes.h"
#include "Misc/AutomationTest.h"
#include "UI/DxWidget.h"
#include "UI/DxWidgetDataType.h"
#include "UObject/UnrealType.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Core/DxApiSubsystem.h"
#include "Core/DTCoreSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "Core/DxWebSocketSubsystem.h"
#include "IStompClient.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Async/TaskGraphInterfaces.h"
#include "Core/DxDataSubsystem.h"
#include "Core/DxLogSubsystem.h"
#include "Core/DxObjectSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/DxPlayerControllerBase.h"
#include "InteractableActor/InteractableActor.h"
#include "Core/DxWidgetSubsystem.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableSet.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Subsystems/SubsystemCollection.h"
#include "Components/CanvasPanel.h"
#include "Engine/World.h"

class FDxCoreContractFakeStomp : public IStompClient
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
	FString Subscribe(const FString&, const FStompSubscriptionEvent&, const FStompRequestCompleted& C) override
	{
		Completions.Add(C);
		return FString::FromInt(Completions.Num());
	}
	void Unsubscribe(FString, const FStompRequestCompleted& C) override
	{
		C.ExecuteIfBound(true, FString());
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

class FDxCoreContractCallbackCommand : public IAutomationLatentCommand
{
	TFunction<bool()> Callback;

public:
	explicit FDxCoreContractCallbackCommand(TFunction<bool()> In)
		: Callback(MoveTemp(In))
	{
	}
	bool Update() override
	{
		return Callback();
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreBlueprintCloseTest, "DTCore.Contract.BlueprintCloseDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreBlueprintCloseTest::RunTest(const FString&)
{
	auto* BP = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDxWidget::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), TEXT("CloseDispatchFixture")), BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
	FEdGraphPinType Type;
	Type.PinCategory = UEdGraphSchema_K2::PC_Int;
	FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("CloseProbe"), Type, TEXT("0"));
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(BP);
	auto* Event = NewObject<UK2Node_Event>(Graph);
	Event->EventReference.SetExternalMember(TEXT("CloseWidgetAddLogic"), UDxWidget::StaticClass());
	Event->bOverrideFunction = true;
	Graph->AddNode(Event, false, false);
	Event->CreateNewGuid();
	Event->PostPlacedNewNode();
	Event->AllocateDefaultPins();
	auto* Set = NewObject<UK2Node_VariableSet>(Graph);
	Set->VariableReference.SetSelfMember(TEXT("CloseProbe"));
	Graph->AddNode(Set, false, false);
	Set->CreateNewGuid();
	Set->PostPlacedNewNode();
	Set->AllocateDefaultPins();
	Set->FindPin(TEXT("CloseProbe"))->DefaultValue = TEXT("77");
	Event->FindPin(UEdGraphSchema_K2::PN_Then)->MakeLinkTo(Set->FindPin(UEdGraphSchema_K2::PN_Execute));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	if (!(FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection), BP->Status != BS_Error && BP->GeneratedClass != nullptr))
	{
		AddError(TEXT("Fixture BP failed to compile"));
		return false;
	}
	auto* Widget = NewObject<UDxWidget>(GetTransientPackage(), BP->GeneratedClass);
	auto* Property = FindFProperty<FIntProperty>(BP->GeneratedClass, TEXT("CloseProbe"));
	Widget->CloseWidget();
	TestEqual(TEXT("Blueprint override runs on close"), Property->GetPropertyValue_InContainer(Widget), 77);
	Property->SetPropertyValue_InContainer(Widget, 19);
	Widget->CloseWidget();
	TestEqual(TEXT("duplicate close does not run hook twice"), Property->GetPropertyValue_InContainer(Widget), 19);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreInputPolicyTest, "DTCore.Contract.ClickPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreInputPolicyTest::RunTest(const FString&)
{
	auto* Controller = NewObject<ADxPlayerControllerBase>();
	Controller->CurrentHoveredActor = NewObject<AInteractableActor>();
	int32 Activations = 0;
	double Now = 1.0;
	Controller->InputClockForTests = [&]
	{ return Now; };
	Controller->ClickSinkForTests = [&](AActor*)
	{ ++Activations; };
	Controller->ClickLeftMouseButton(FInputActionValue(true));
	Controller->ClickLeftMouseButton(FInputActionValue(true));
	Controller->ClickLeftMouseButton(FInputActionValue(false));
	TestEqual(TEXT("one/duplicate press does not activate default double policy"), Activations, 0);
	Now = 1.2;
	Controller->ClickLeftMouseButton(FInputActionValue(true));
	Controller->ClickLeftMouseButton(FInputActionValue(false));
	TestEqual(TEXT("same-actor second press activates once"), Activations, 1);
	Controller->ClickActivationPolicy = EDxClickActivationPolicy::SingleRelease;
	Controller->ClickLeftMouseButton(FInputActionValue(true));
	Controller->ClickLeftMouseButton(FInputActionValue(false));
	Controller->ClickLeftMouseButton(FInputActionValue(false));
	TestEqual(TEXT("single policy ignores duplicate release"), Activations, 2);
	Controller->bIsWidgetUnderMouse = true;
	Controller->ClickLeftMouseButton(FInputActionValue(true));
	Controller->ClickLeftMouseButton(FInputActionValue(false));
	TestEqual(TEXT("UI input cannot activate 3D actor"), Activations, 2);
	auto* GI = NewObject<UGameInstance>();
	auto* Widgets = NewObject<UDxWidgetSubsystem>(GI);
	auto* External = NewObject<UDxWidget>();
	Widgets->RegisterExternalInputBlocker(External);
	Widgets->RegisterExternalInputBlocker(External);
	TestTrue(TEXT("input blocker registration does not take window ownership"), Widgets->GetOpenWidgets().IsEmpty());
	Widgets->UnregisterExternalInputBlocker(External);
	TestTrue(TEXT("input blocker removal leaves other ownership unchanged"), Widgets->GetOpenWidgets().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreWorkerShutdownTest, "DTCore.Contract.ParseShutdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreWorkerShutdownTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxDataSubsystem> Data(NewObject<UDxDataSubsystem>(GI.Get()));
	auto* Handler = NewObject<UDxCoreTestSlowTransaction>(Data.Get());
	auto Probe = MakeShared<FDxCoreTestParseProbe, ESPMode::ThreadSafe>();
	Handler->Probe = Probe;
	Data->TransactionCodeMessageMap.Add(TEXT("slow"), Handler);
	Data->CachedHandlerTransactionCodeMessageMap = MakeShared<TMap<FString, UTransactionCodeMessage*>>();
	Data->CachedHandlerTransactionCodeMessageMap->Add(TEXT("slow"), Handler);
	Data->EnqueueWebSocketData(TEXT("{\"MESSAGE_ID\":\"slow\"}"));
	Data->Tick(0);
	const double Deadline = FPlatformTime::Seconds() + 2.0;
	while (!Probe->Started.Load() && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(0.001f);
	TestTrue(TEXT("slow worker actually starts"), Probe->Started.Load());
	Data->Deinitialize();
	TestTrue(TEXT("shutdown joined parser"), Data->WebSocketWorker.IsReady());
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("late result cannot apply after shutdown"), Probe->Applied.Load(), 0);
	TestTrue(TEXT("handler ownership released after join"), Data->TransactionCodeMessageMap.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreLogFlushTest, "DTCore.Contract.LogFlush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreLogFlushTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxLogSubsystem> Log(NewObject<UDxLogSubsystem>(GI.Get()));
	Log->CachedLogDirectory = FPaths::ProjectSavedDir() / TEXT("Validation/quality90/LogProbe");
	const FString Name = FGuid::NewGuid().ToString() + TEXT(".log");
	for (int32 I = 0; I < 40; ++I)
		Log->WriteLog(FString::Printf(TEXT("sequence=%03d"), I), false, Name);
	TestTrue(TEXT("flush confirms IO completion"), Log->FlushLogs());
	FString Contents;
	FFileHelper::LoadFileToString(Contents, *(Log->CachedLogDirectory / Name));
	int32 Previous = -1;
	for (int32 I = 0; I < 40; ++I)
	{
		const int32 Index = Contents.Find(FString::Printf(TEXT("sequence=%03d"), I));
		TestTrue(TEXT("logs written once and in order"), Index > Previous);
		Previous = Index;
	}
	Log->WriterSinkForTests = [](const FString&, const FString&)
	{ FPlatformProcess::Sleep(0.05f); return false; };
	AddExpectedError(TEXT("DTCore log file write failed"), EAutomationExpectedErrorFlags::Contains, 1);
	Log->WriteLog(TEXT("forced failure"), false, Name);
	TestFalse(TEXT("flush exposes slow writer failure"), Log->FlushLogs());
	Log->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreRegistryGcTest, "DTCore.Contract.RegistryGC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreRegistryGcTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxObjectSubsystem> Registry(NewObject<UDxObjectSubsystem>(GI.Get()));
	auto* Actor = NewObject<AActor>();
	TWeakObjectPtr<AActor> Weak = Actor;
	Registry->RegisterObject(TEXT("test"), TEXT("a"), Actor);
	Actor = nullptr;
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("registry references are explicitly tracked by GC"), Weak.IsValid());
	TestEqual(TEXT("legacy map accessor remains available"), Registry->GetCategoryMap(TEXT("test"))->Num(), 1);
	Weak->MarkAsGarbage();
	CollectGarbage(RF_NoFlags);
	TestNull(TEXT("destroyed/garbage actor cannot be found"), Registry->FindObject(TEXT("test"), TEXT("a")));
	Registry->CompactAllInvalidObjects();
	TestEqual(TEXT("invalid entries safely compact"), Registry->GetRegisteredObjectCount(TEXT("test")), 0);
	auto* Replacement = NewObject<AActor>();
	auto* Removed = NewObject<AActor>();
	Registry->RegisterObject(TEXT("aliases"), TEXT("one"), Removed);
	Registry->RegisterObject(TEXT("aliases"), TEXT("two"), Removed);
	Registry->RegisterObject(TEXT("aliases"), TEXT("one"), Replacement);
	TestEqual(TEXT("ID replacement preserves the other alias"), Registry->FindObject(TEXT("aliases"), TEXT("two")), Removed);
	Removed->OnEndPlay.Broadcast(Removed, EEndPlayReason::RemovedFromWorld);
	TestNull(TEXT("stream-out EndPlay removes remaining aliases"), Registry->FindObject(TEXT("aliases"), TEXT("two")));
	TestEqual(TEXT("stream-out leaves replacement registered"), Registry->FindObject(TEXT("aliases"), TEXT("one")), Replacement);
	TWeakObjectPtr<AActor> Released = Removed;
	Removed = nullptr;
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("stream-out actor is no longer held by registry"), Released.IsValid());
	Registry->ClearAllObjects();
	Registry->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreSubscriptionLifecycleTest, "DTCore.Contract.SubscriptionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreSubscriptionLifecycleTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	TStrongObjectPtr<UDxCoreTestListener> Listener(NewObject<UDxCoreTestListener>());
	auto Fake = MakeShared<FDxCoreContractFakeStomp>();
	auto* S = Ws.Get();
	S->StompClient = Fake;
	S->bWantsConnection = true;
	S->OnConnected.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveReady);
	S->ReceivedMessageEvent.BindDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveMessage);
	const FSTOMPSubscriptionEvent CustomBinding = S->ReceivedMessageEvent;
	S->TopicRouteMap.Add(TEXT("a"), ETopicRouteType::WebSocket);
	S->TopicRouteMap.Add(TEXT("b"), ETopicRouteType::Api);
	S->HandleOnConnected(TEXT("1.2"), TEXT("mixed"), FString());
	TestTrue(TEXT("custom receive binding preserved"), S->ReceivedMessageEvent == CustomBinding);
	Fake->Completions[0].Execute(true, FString());
	Fake->Completions[1].Execute(false, TEXT("denied"));
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("main contract aggregates success and failure callbacks"), Listener->ReadyCount, 1);
	TestEqual(TEXT("batch completion reaches zero"), S->PendingSubscribeCount, 0);
	Fake->Completions[1].Execute(false, TEXT("denied"));
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("duplicate callback cannot rebroadcast"), Listener->ReadyCount, 1);
	++S->ConnectionGeneration;
	S->bSubscriptionBatchStarted = false;
	S->bConnectedBroadcast = false;
	S->HandleOnConnected(TEXT("1.2"), TEXT("next"), FString());
	Fake->Completions[0].Execute(true, FString());
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("old callback preserves new pending count"), S->PendingSubscribeCount, 2);
	Fake->Completions[2].Execute(true, FString());
	Fake->Completions[3].Execute(true, FString());
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("one notification per generation"), Listener->ReadyCount, 2);
	++S->ConnectionGeneration;
	S->bSubscriptionBatchStarted = false;
	S->bConnectedBroadcast = false;
	S->TopicRouteMap.Reset();
	S->HandleOnConnected(TEXT("1.2"), TEXT("empty"), FString());
	TestEqual(TEXT("zero topics completes immediately"), Listener->ReadyCount, 3);
	++S->ConnectionGeneration;
	S->bSubscriptionBatchStarted = false;
	S->bConnectedBroadcast = false;
	S->TopicRouteMap.Add(TEXT("missing"), ETopicRouteType::WebSocket);
	S->HandleOnConnected(TEXT("1.2"), TEXT("pending"), FString());
	TestEqual(TEXT("missing receipt remains pending under main contract"), S->PendingSubscribeCount, 1);
	S->DisconnectStompClient({});
	S->TryReconnect();
	TestFalse(TEXT("manual disconnect clears intent"), S->bWantsConnection);
	TestFalse(TEXT("manual disconnect cancels retry"), GI->GetTimerManager().IsTimerActive(S->ReconnectTimerHandle));
	Fake->Completions.Last().Execute(true, FString());
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("late receipt after disconnect ignored"), Listener->ReadyCount, 3);
	S->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreHttpContractTest, "DTCore.Contract.HttpBodyAndFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreHttpContractTest::RunTest(const FString&)
{
	auto* Api = NewObject<UDxApiSubsystem>(NewObject<UGameInstance>());
	Api->HttpModule = &FHttpModule::Get();
	Api->ApiDataTable = NewObject<UDataTable>();
	Api->ApiDataTable->RowStruct = FApiStruct::StaticStruct();
	FApiStruct Row;
	Row.ApiType = EApiType::Local;
	Row.ApiMethod = EApiMethod::Post;
	Row.ApiUrl = TEXT("/capture");
	Api->ApiDataTable->AddRow(TEXT("capture"), Row);
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	const FString OldLocal = Settings->LocalApiUrl, OldBase = Settings->BaseApiUrl;
	ON_SCOPE_EXIT
	{
		Settings->LocalApiUrl = OldLocal;
		Settings->BaseApiUrl = OldBase;
	};
	Settings->LocalApiUrl.Reset();
	Settings->BaseApiUrl = TEXT("http://settings.invalid");
	TMap<FString, FString> Overrides;
	Overrides.Add(TEXT("BaseApiUrl"), TEXT("http://override.invalid"));
	Api->RuntimeOverrideReaderForTests = [&Overrides](const TCHAR* Key, FString& Out)
	{ if (const auto* Found=Overrides.Find(Key)) { Out=*Found; return true; } return false; };
	TestEqual(TEXT("Base override is used when environment settings absent"), Api->GetServerUrl(EApiType::Local), TEXT("http://override.invalid"));
	Settings->LocalApiUrl = TEXT("http://environment.invalid");
	TestEqual(TEXT("environment setting precedes Base override"), Api->GetServerUrl(EApiType::Local), Settings->LocalApiUrl);
	Overrides.Add(TEXT("LocalApiUrl"), TEXT("http://environment-override.invalid"));
	TestEqual(TEXT("environment override wins"), Api->GetServerUrl(EApiType::Local), TEXT("http://environment-override.invalid"));
	Overrides.Reset();
	Settings->LocalApiUrl.Reset();
	TestEqual(TEXT("Base setting is final configured fallback"), Api->GetServerUrl(EApiType::Local), Settings->BaseApiUrl);
	FString CapturedBody, CapturedUrl;
	int32 Requests = 0;
	Api->RequestProbeForTests = [&](const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request)
	{
		++Requests;
		CapturedUrl = Request->GetURL();
		const auto& Bytes = Request->GetContent();
		CapturedBody = Bytes.IsEmpty() ? FString() : FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num()).Get());
		return false; // 실제 요청 객체를 검사하되 외부 네트워크로 보내지 않는다.
	};
	const FString Body = TEXT("{\"소재\":\"SQ83521 047\",\"frame_no\":580}");
	Api->DxRequestApiWithBody(TEXT("capture"), FDxApiCallback(), Body);
	TestEqual(TEXT("UTF-8 Body reaches actual request"), CapturedBody, Body);
	Api->DxRequestApiWithParameterAndBody(TEXT("capture"), FDxApiCallback(), {TEXT("sensor"), TEXT("42")}, Body);
	TestEqual(TEXT("parameter+Body preserves payload"), CapturedBody, Body);
	TestTrue(TEXT("parameters remain in request URL"), CapturedUrl.EndsWith(TEXT("/capture/sensor/42")));
	Api->DxRequestApiWithBody(TEXT("capture"), FDxApiCallback(), FString());
	TestTrue(TEXT("empty body remains empty"), CapturedBody.IsEmpty());
	TestEqual(TEXT("exactly one request constructed per invocation"), Requests, 3);
	auto* Listener = NewObject<UDxCoreTestListener>();
	FDxApiCallback Callback;
	Callback.BindDynamic(Listener, &UDxCoreTestListener::ReceiveHttp);
	UDxApiSubsystem::FDxHttpRequestContext OldContext;
	OldContext.Generation = Api->RequestGeneration;
	OldContext.Callback = Callback;
	auto Pending = FHttpModule::Get().CreateRequest();
	Pending->OnProcessRequestComplete().BindUObject(Api, &UDxApiSubsystem::InternalOnResponseReceived, OldContext);
	Api->ActiveHttpRequests.Add(Pending);
	Api->Deinitialize();
	TestFalse(TEXT("shutdown unbinds outstanding completion"), Pending->OnProcessRequestComplete().IsBound());
	Api->InternalOnResponseReceived(Pending, nullptr, false, OldContext);
	Api->DxHttpCall(TEXT("http://shutdown.invalid"), TEXT("POST"), Body, {}, Callback);
	TestEqual(TEXT("late completion and shutdown submission cannot apply callbacks"), Listener->ReceivedCount, 0);
	TestEqual(TEXT("shutdown cannot construct new requests"), Requests, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreWidgetContractTest, "DTCore.Contract.WidgetIdentifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreWidgetContractTest::RunTest(const FString&)
{
	const auto* Enum = StaticEnum<EDxWidgetFlag>();
	const TCHAR* Names[] = {TEXT("None"), TEXT("ShipFreeView"), TEXT("ShipTopView"), TEXT("CraneFreeView"), TEXT("CraneTopView"), TEXT("UpdateWidget"), TEXT("CctvWidget")};
	for (int32 Index = 0; Index < 7; ++Index)
		TestEqual(Names[Index], Enum->GetValueByNameString(Names[Index]), static_cast<int64>(Index));
	TestNotNull(TEXT("new widget identifier is a byte property"), FindFProperty<FByteProperty>(UDxWidget::StaticClass(), TEXT("WidgetFlag")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreQueueWorldCleanupTest, "DTCore.Contract.QueueSurvivesWorldCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreQueueWorldCleanupTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxDataSubsystem> Data(NewObject<UDxDataSubsystem>(GI.Get()));
	FSubsystemCollection<UGameInstanceSubsystem> Collection;
	Data->Initialize(Collection);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	World->SetGameInstance(GI.Get());
	auto* Handler = NewObject<UDxCoreTestSlowTransaction>(Data.Get());
	auto Probe = MakeShared<FDxCoreTestParseProbe, ESPMode::ThreadSafe>();
	Handler->Probe = Probe;
	Data->TransactionCodeMessageMap.Add(TEXT("slow"), Handler);
	Data->CachedHandlerTransactionCodeMessageMap->Add(TEXT("slow"), Handler);
	Data->EnqueueWebSocketData(TEXT("{\"MESSAGE_ID\":\"slow\"}"));
	FWorldDelegates::OnWorldCleanup.Broadcast(World, false, false);
	Data->Tick(0);
	if (Data->WebSocketWorker.IsValid())
		Data->WebSocketWorker.Wait();
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	TestEqual(TEXT("GameInstance queue retains ordinary message across World cleanup"), Probe->Applied.Load(), 1);
	Data->Deinitialize();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreRegistryStreamOutTest, "DTCore.Contract.RegistryEngineStreamOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreRegistryStreamOutTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxObjectSubsystem> Registry(NewObject<UDxObjectSubsystem>(GI.Get()));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(GI.Get());
	World->InitializeActorsForPlay(FURL());
	AActor* Actor = World->SpawnActor<AActor>();
	Actor->DispatchBeginPlay(true);
	TestTrue(TEXT("fixture actor has begun play"), Actor->HasActorBegunPlay());
	Registry->RegisterObject(TEXT("stream"), TEXT("one"), Actor);
	Registry->RegisterObject(TEXT("stream"), TEXT("two"), Actor);
	TWeakObjectPtr<AActor> Weak = Actor;
	Actor->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestEqual(TEXT("engine stream-out EndPlay removes all aliases"), Registry->GetRegisteredObjectCount(TEXT("stream")), 0);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	Actor = nullptr;
	World = nullptr;
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("stream-out actor is collectable after World release"), Weak.IsValid());
	Registry->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreWidgetCloseOrderTest, "DTCore.Contract.WidgetCloseOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreWidgetCloseOrderTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWidgetSubsystem> Widgets(NewObject<UDxWidgetSubsystem>(GI.Get()));
	TStrongObjectPtr<UDxCoreTestCloseOrderWidget> Parent(NewObject<UDxCoreTestCloseOrderWidget>());
	TStrongObjectPtr<UDxCoreTestCloseOrderWidget> Child(NewObject<UDxCoreTestCloseOrderWidget>());
	TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>());
	auto Trace = MakeShared<TArray<FString>>();
	Parent->CloseTrace = Trace;
	Child->CloseTrace = Trace;
	Canvas->AddChild(Parent.Get());
	Parent->AddChildWidget(Child.Get());
	Widgets->CloseWidget(Parent.Get());
	TestEqual(TEXT("cascade notifies both widgets once"), Trace->Num(), 2);
	if (Trace->Num() == 2)
	{
		TestEqual(TEXT("child hook precedes parent hook"), (*Trace)[0], Child->GetName());
		TestEqual(TEXT("parent hook runs last"), (*Trace)[1], Parent->GetName());
	}
	TestTrue(TEXT("parent was removed before cleanup hook"), Parent->bRemovedBeforeHook);
	Widgets->CloseWidget(Parent.Get());
	TestEqual(TEXT("reentrant and repeated close do not notify again"), Trace->Num(), 2);
	return true;
}

#endif
