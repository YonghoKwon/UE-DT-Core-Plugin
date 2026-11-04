#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "DTCoreTestTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Core/DTCoreRuntimeConfig.h"
#include "Async/Async.h"
#include "Core/DTCoreSettings.h"
#include "Core/DxWebSocketSubsystem.h"
#include "Core/DxDataChannelBase.h"
#include "Core/DxDataChannelLibrary.h"
#include "Core/DxDefaultWebSocketChannel.h"
#include "Core/DxStompChannel.h"
#include "IStompClient.h"
#include "IStompMessage.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Async/TaskGraphInterfaces.h"

class FDTCoreChannelFakeStomp : public IStompClient
{
public:
	struct FSent
	{
		FString Destination;
		TArray<uint8> Body;
		FStompRequestCompleted Completion;
	};
	struct FSubscribed
	{
		FString Destination;
		FStompSubscriptionEvent Event;
		FStompRequestCompleted Completion;
	};
	bool Connected = false;
	int32 ConnectCalls = 0;
	TMap<FName, FString> LastConnectHeader;
	TArray<FSent> Sent;
	TArray<FSubscribed> Subscribed;
	TArray<FString> Unsubscribed;
	FStompClientConnectedEvent ConnectedEvent;
	FStompClientConnectionErrorEvent ConnectionErrorEvent;
	FStompClientErrorEvent ErrorEvent;
	FStompClientClosedEvent ClosedEvent;
	void Connect(const FStompHeader& Header) override
	{
		++ConnectCalls;
		LastConnectHeader = Header;
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
	FString Subscribe(const FString& Destination, const FStompSubscriptionEvent& Event, const FStompRequestCompleted& Completion) override
	{
		Subscribed.Add({Destination, Event, Completion});
		return FString::Printf(TEXT("sub-%d"), Subscribed.Num());
	}
	void Unsubscribe(FString Id, const FStompRequestCompleted& Completion) override
	{
		Unsubscribed.Add(Id);
		Completion.ExecuteIfBound(true, FString());
	}
	void Send(const FString& Destination, const FStompBuffer& Body, const FStompHeader&, const FStompRequestCompleted& Completion) override
	{
		Sent.Add({Destination, Body, Completion});
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

class FDTCoreChannelFakeMessage : public IStompMessage
{
public:
	virtual ~FDTCoreChannelFakeMessage() = default;
	FStompHeader Header;
	TArray<uint8> Body;
	FString Destination;
	void Ack(const FStompHeader&, const FStompRequestCompleted&) const override
	{
	}
	void Nack(const FStompHeader&, const FStompRequestCompleted&) const override
	{
	}
	const FStompHeader& GetHeader() const override
	{
		return Header;
	}
	FString GetBodyAsString() const override
	{
		return FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Body.GetData())));
	}
	const uint8* GetRawBody() const override
	{
		return Body.GetData();
	}
	SIZE_T GetRawBodyLength() const override
	{
		return Body.Num();
	}
	FStompSubscriptionId GetSubscriptionId() const override
	{
		return TEXT("sub-1");
	}
	FString GetDestination() const override
	{
		return Destination;
	}
	FString GetMessageId() const override
	{
		return TEXT("m-1");
	}
	FString GetAckId() const override
	{
		return FString();
	}
};

namespace
{
	void PumpGameThread()
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	}

	struct FScopedDataChannelSettings
	{
		TArray<FDxDataChannelSettings> Saved;
		FScopedDataChannelSettings()
		{
			Saved = GetMutableDefault<UDTCoreSettings>()->DataChannels;
			GetMutableDefault<UDTCoreSettings>()->DataChannels.Reset();
		}
		~FScopedDataChannelSettings()
		{
			GetMutableDefault<UDTCoreSettings>()->DataChannels = Saved;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreChannelOptInTest, "DTCore.Channel.OptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreChannelOptInTest::RunTest(const FString&)
{
	FScopedDataChannelSettings ScopedSettings;
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	auto Fake = MakeShared<FDTCoreChannelFakeStomp>();
	Ws->StompClient = Fake;
	Ws->bWantsConnection = true;

	// Initialize가 부르는 설정 기반 생성과 같은 경로이다.
	Ws->CreateConfiguredChannels();
	TestEqual(TEXT("no settings, no channels"), Ws->DataChannels.Num(), 0);
	TestNull(TEXT("default adapter not created implicitly"), Ws->DefaultChannelAdapter.Get());
	TestTrue(TEXT("channel names empty"), Ws->GetChannelNames().IsEmpty());
	TestFalse(TEXT("no listener added to default connection"), Ws->OnConnectionStateChanged.IsBound());
	TestNull(TEXT("unknown channel"), Ws->FindChannel(TEXT("Bulk")));
	TestNull(TEXT("lookup does not create adapter"), Ws->DefaultChannelAdapter.Get());

	// 기본 연결 흐름은 기존 그대로이다.
	Ws->TopicRouteMap.Add(TEXT("opt-in"), ETopicRouteType::WebSocket);
	Ws->HandleOnConnected(TEXT("1.2"), TEXT("fixture"), FString());
	TestEqual(TEXT("default subscribing"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Subscribing);
	if (TestEqual(TEXT("default subscribes topic"), Fake->Subscribed.Num(), 1))
	{
		Fake->Subscribed[0].Completion.Execute(true, FString());
	}
	PumpGameThread();
	TestEqual(TEXT("default ready"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Ready);
	Fake->Connected = true;
	TestTrue(TEXT("default publish unchanged"), Ws->PublishMessage(TEXT("opt-in"), TEXT("x"), {}, FSTOMPRequestCompleted()));
	TestEqual(TEXT("default publish sent once"), Fake->Sent.Num(), 1);
	Ws->Deinitialize();
	TestEqual(TEXT("shutdown with no channels"), Ws->DataChannels.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreChannelRegistryTest, "DTCore.Channel.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreChannelRegistryTest::RunTest(const FString&)
{
	FScopedDataChannelSettings ScopedSettings;
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	auto Fake = MakeShared<FDTCoreChannelFakeStomp>();
	Ws->StompClient = Fake;

	auto* A = NewObject<UDxCoreTestChannel>(Ws.Get());
	TestFalse(TEXT("null rejected"), Ws->RegisterChannel(TEXT("A"), nullptr));
	TestFalse(TEXT("None rejected"), Ws->RegisterChannel(NAME_None, A));
	TestFalse(TEXT("Default reserved"), Ws->RegisterChannel(UDxWebSocketSubsystem::DefaultChannelName, A));
	TestEqual(TEXT("rejected registrations do not initialize"), A->InitializedCount, 0);

	TestTrue(TEXT("register A"), Ws->RegisterChannel(TEXT("A"), A));
	TestEqual(TEXT("initialized once"), A->InitializedCount, 1);
	TestEqual(TEXT("auto connect by default"), A->ConnectCount, 1);
	TestEqual(TEXT("name assigned"), A->GetChannelName(), FName(TEXT("A")));
	TestFalse(TEXT("duplicate name rejected"), Ws->RegisterChannel(TEXT("A"), NewObject<UDxCoreTestChannel>(Ws.Get())));
	TestFalse(TEXT("same channel under other name rejected"), Ws->RegisterChannel(TEXT("B"), A));

	FDxDataChannelSettings Manual;
	Manual.ChannelName = TEXT("Manual");
	Manual.bAutoConnect = false;
	auto* ManualChannel = NewObject<UDxCoreTestChannel>(Ws.Get());
	TestTrue(TEXT("register with settings"), Ws->RegisterChannelWithSettings(ManualChannel, Manual));
	TestEqual(TEXT("no auto connect"), ManualChannel->ConnectCount, 0);

	FDxDataChannelSettings Configured;
	Configured.ChannelName = TEXT("Configured");
	Configured.MaxMessageBytes = 4;
	Configured.bAutoConnect = false;
	Settings->DataChannels.Add(Configured);
	auto* ConfiguredChannel = NewObject<UDxCoreTestChannel>(Ws.Get());
	TestTrue(TEXT("register picks settings by name"), Ws->RegisterChannel(TEXT("Configured"), ConfiguredChannel));
	TestEqual(TEXT("settings applied"), ConfiguredChannel->GetChannelSettings().MaxMessageBytes, static_cast<int64>(4));
	TestEqual(TEXT("configured auto connect honored"), ConfiguredChannel->ConnectCount, 0);

	TestEqual(TEXT("find A"), Ws->FindChannel(TEXT("A")), static_cast<UDxDataChannelBase*>(A));
	TestNull(TEXT("find unknown"), Ws->FindChannel(TEXT("Z")));
	TestEqual(TEXT("three names"), Ws->GetChannelNames().Num(), 3);

	TestTrue(TEXT("unregister A"), Ws->UnregisterChannel(TEXT("A")));
	TestEqual(TEXT("unregister shuts down"), A->ShutdownCount, 1);
	TestTrue(TEXT("unregistered channel is shut down"), A->IsShutdown());
	TestFalse(TEXT("second unregister fails"), Ws->UnregisterChannel(TEXT("A")));
	TestFalse(TEXT("Default cannot be unregistered"), Ws->UnregisterChannel(UDxWebSocketSubsystem::DefaultChannelName));
	TestFalse(TEXT("shut down channel cannot be re-registered"), Ws->RegisterChannel(TEXT("A2"), A));
	TestFalse(TEXT("shut down channel stays out"), Ws->GetChannelNames().Contains(FName(TEXT("A2"))));

	// 기본 연결 adapter
	auto* Default = Ws->FindChannel(UDxWebSocketSubsystem::DefaultChannelName);
	if (TestNotNull(TEXT("default adapter"), Default))
	{
		TestTrue(TEXT("adapter type"), Default->IsA<UDxDefaultWebSocketChannel>());
		TestEqual(TEXT("adapter reused"), Ws->FindChannel(UDxWebSocketSubsystem::DefaultChannelName), Default);
		TestEqual(TEXT("disconnected maps"), Default->GetStatus().State, EDxDataChannelState::Disconnected);
		TestFalse(TEXT("adapter publish follows default rule"), Default->PublishText(TEXT("t"), TEXT("x"), {}, FSTOMPRequestCompleted()));
		TestEqual(TEXT("default drop counted once"), Default->GetStatus().DroppedCount, static_cast<int64>(1));
		TestEqual(TEXT("default drop visible in existing counter"), Ws->GetPublishDroppedCount(), 1);
		Ws->bWantsConnection = true;
		Ws->SetConnectionState(EDxWebSocketConnectionState::Ready, TEXT("fixture"));
		TestEqual(TEXT("adapter follows default state event"), Default->GetState(), EDxDataChannelState::Connected);
		Ws->SetConnectionState(EDxWebSocketConnectionState::Subscribing, TEXT("fixture"));
		TestEqual(TEXT("Subscribing is Connected"), Default->GetStatus().State, EDxDataChannelState::Connected);
		Fake->Connected = true;
		TestTrue(TEXT("adapter publish forwards"), Default->PublishText(TEXT("t"), TEXT("x"), {}, FSTOMPRequestCompleted()));
		TestEqual(TEXT("forwarded to default client"), Fake->Sent.Num(), 1);
		TestEqual(TEXT("adapter counts publish"), Default->GetStatus().PublishedCount, static_cast<int64>(1));
	}
	const TArray<FDxDataChannelStatus> All = Ws->GetAllChannelStatus(true);
	TestEqual(TEXT("status includes default and two channels"), All.Num(), 3);
	TestEqual(TEXT("default first"), All.Num() > 0 ? All[0].ChannelName : NAME_None, UDxWebSocketSubsystem::DefaultChannelName);
	TestEqual(TEXT("status without default"), Ws->GetAllChannelStatus(false).Num(), 2);

	// 설정 기반 생성: 잘못된 항목은 건너뛴다.
	Settings->DataChannels.Reset();
	FDxDataChannelSettings Cfg;
	Cfg.ChannelName = TEXT("Cfg");
	Cfg.bAutoConnect = false;
	Settings->DataChannels.Add(Cfg);
	FDxDataChannelSettings Unnamed;
	Unnamed.bAutoConnect = false;
	Settings->DataChannels.Add(Unnamed);
	Settings->DataChannels.Add(Cfg);
	FDxDataChannelSettings AbstractClass;
	AbstractClass.ChannelName = TEXT("Abstract");
	AbstractClass.ChannelClass = UDxDataChannelBase::StaticClass();
	AbstractClass.bAutoConnect = false;
	Settings->DataChannels.Add(AbstractClass);
	Ws->CreateConfiguredChannels();
	auto* Created = Ws->FindChannel(TEXT("Cfg"));
	TestTrue(TEXT("configured channel defaults to STOMP"), Created && Created->GetClass() == UDxStompChannel::StaticClass());
	TestNull(TEXT("abstract class skipped"), Ws->FindChannel(TEXT("Abstract")));
	TestEqual(TEXT("only valid entries created"), Ws->GetChannelNames().Num(), 3);

	Ws->Deinitialize();
	TestEqual(TEXT("deinitialize shuts down channels"), ManualChannel->ShutdownCount + ConfiguredChannel->ShutdownCount, 2);
	TestTrue(TEXT("configured channel shut down"), Created && Created->IsShutdown());
	TestNull(TEXT("adapter released"), Ws->DefaultChannelAdapter.Get());
	TestEqual(TEXT("registry empty"), Ws->DataChannels.Num(), 0);
	TestFalse(TEXT("register after shutdown rejected"), Ws->RegisterChannel(TEXT("Late"), NewObject<UDxCoreTestChannel>(Ws.Get())));
	TestNull(TEXT("no adapter after shutdown"), Ws->FindChannel(UDxWebSocketSubsystem::DefaultChannelName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreChannelSettingsLoadTest, "DTCore.Channel.SettingsLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreChannelSettingsLoadTest::RunTest(const FString&)
{
	TestEqual(TEXT("struct default auto connect"), FDxDataChannelSettings().bAutoConnect, true);
	TestEqual(TEXT("struct default attempts follow default"), FDxDataChannelSettings().MaxReconnectAttempts, -1);

	auto LoadFrom = [this](const FString& Text) -> UDTCoreSettings*
	{
		const FString FileName = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DTCoreChannelSettingsTest.ini"));
		FConfigFile File;
		File.CombineFromBuffer(Text, FileName);
		GConfig->SetFile(FileName, &File);
		ON_SCOPE_EXIT
		{
			GConfig->UnloadFile(FileName);
		};
		UDTCoreSettings* Loaded = NewObject<UDTCoreSettings>(GetTransientPackage());
		Loaded->DataChannels.Reset();
		Loaded->LoadConfig(UDTCoreSettings::StaticClass(), *FileName);
		return Loaded;
	};

	UDTCoreSettings* Legacy = LoadFrom(TEXT("[/Script/DTCore.DTCoreSettings]\nWebSocketUrl=\"ws://legacy.fixture:1\"\nMaxReconnectAttempts=3\n"));
	TestEqual(TEXT("legacy key loaded"), Legacy->WebSocketUrl, FString(TEXT("ws://legacy.fixture:1")));
	TestEqual(TEXT("legacy attempts loaded"), Legacy->MaxReconnectAttempts, 3);
	TestEqual(TEXT("legacy config has no channels"), Legacy->DataChannels.Num(), 0);

	UDTCoreSettings* Empty = LoadFrom(TEXT("[/Script/DTCore.DTCoreSettings]\n"));
	TestEqual(TEXT("empty config has no channels"), Empty->DataChannels.Num(), 0);

	UDTCoreSettings* WithChannel = LoadFrom(TEXT("[/Script/DTCore.DTCoreSettings]\n+DataChannels=(ChannelName=\"Bulk\",Url=\"ws://bulk.fixture:2\",MaxMessageBytes=1048576,ReconnectInitialDelaySeconds=0.5)\n"));
	if (TestEqual(TEXT("channel entry loaded"), WithChannel->DataChannels.Num(), 1))
	{
		const FDxDataChannelSettings& Item = WithChannel->DataChannels[0];
		TestEqual(TEXT("name"), Item.ChannelName, FName(TEXT("Bulk")));
		TestEqual(TEXT("url"), Item.Url, FString(TEXT("ws://bulk.fixture:2")));
		TestEqual(TEXT("max bytes"), Item.MaxMessageBytes, static_cast<int64>(1048576));
		TestEqual(TEXT("initial delay"), Item.ReconnectInitialDelaySeconds, 0.5f);
		TestTrue(TEXT("unspecified auto connect default"), Item.bAutoConnect);
		TestEqual(TEXT("unspecified attempts default"), Item.MaxReconnectAttempts, -1);
		TestTrue(TEXT("unspecified class defaults to STOMP at creation"), Item.ChannelClass.IsNull());
	}

	// URL·인증 해석: 채널 값이 없으면 기본 채널 값을 쓴다.
	auto* Settings = GetMutableDefault<UDTCoreSettings>();
	const FString OldUrl = Settings->WebSocketUrl;
	const FString OldLogin = Settings->WebSocketLogin;
	ON_SCOPE_EXIT
	{
		Settings->WebSocketUrl = OldUrl;
		Settings->WebSocketLogin = OldLogin;
	};
	FString RuntimeValue;
	const bool bRuntimeUrl = DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketUrl"), RuntimeValue);
	const bool bRuntimeLogin = DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketLogin"), RuntimeValue);
	Settings->WebSocketUrl = TEXT("ws://default.fixture:3");
	Settings->WebSocketLogin = TEXT("fixture-user");
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxCoreTestChannel> Channel(NewObject<UDxCoreTestChannel>());
	FDxDataChannelSettings Inherit;
	Channel->InitializeChannel(TEXT("Inherit"), Inherit, GI.Get());
	if (!bRuntimeUrl)
	{
		TestEqual(TEXT("empty url uses default url"), Channel->TestResolveUrl(), FString(TEXT("ws://default.fixture:3")));
	}
	if (!bRuntimeLogin)
	{
		TestEqual(TEXT("default credentials inherited"), Channel->TestResolveCredentials().FindRef(TEXT("login")), FString(TEXT("fixture-user")));
	}
	TStrongObjectPtr<UDxCoreTestChannel> Own(NewObject<UDxCoreTestChannel>());
	FDxDataChannelSettings OwnSettings;
	OwnSettings.Url = TEXT("ws://own.fixture:4");
	OwnSettings.bUseDefaultCredentials = false;
	Own->InitializeChannel(TEXT("Own"), OwnSettings, GI.Get());
	TestEqual(TEXT("channel url wins"), Own->TestResolveUrl(), FString(TEXT("ws://own.fixture:4")));
	TestTrue(TEXT("credentials not inherited when disabled"), Own->TestResolveCredentials().IsEmpty());
	Channel->ShutdownChannel();
	Own->ShutdownChannel();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreChannelReconnectTest, "DTCore.Channel.ReconnectPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreChannelReconnectTest::RunTest(const FString&)
{
	TestEqual(TEXT("first"), UDxDataChannelBase::ComputeReconnectDelay(0.5f, 1.5f, 2.0f, 0), 0.5f);
	TestEqual(TEXT("second"), UDxDataChannelBase::ComputeReconnectDelay(0.5f, 1.5f, 2.0f, 1), 1.0f);
	TestEqual(TEXT("capped"), UDxDataChannelBase::ComputeReconnectDelay(0.5f, 1.5f, 2.0f, 5), 1.5f);
	TestEqual(TEXT("initial floor"), UDxDataChannelBase::ComputeReconnectDelay(0.0f, 0.0f, 0.5f, 3), 0.1f);
	TestEqual(TEXT("max raised to initial"), UDxDataChannelBase::ComputeReconnectDelay(2.0f, 1.0f, 3.0f, 2), 2.0f);

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
	Settings->MaxReconnectAttempts = 7;
	Settings->ReconnectInitialDelaySeconds = 0.25f;
	Settings->ReconnectMaxDelaySeconds = 4.0f;
	Settings->ReconnectBackoffMultiplier = 3.0f;

	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxCoreTestChannel> Inherit(NewObject<UDxCoreTestChannel>());
	Inherit->InitializeChannel(TEXT("Inherit"), FDxDataChannelSettings(), GI.Get());
	TestEqual(TEXT("inherits default initial"), Inherit->TestReconnectDelay(0), 0.25f);
	TestEqual(TEXT("inherits default multiplier"), Inherit->TestReconnectDelay(1), 0.75f);
	TestEqual(TEXT("inherits default max"), Inherit->TestReconnectDelay(10), 4.0f);
	TestEqual(TEXT("inherits default attempts"), Inherit->TestMaxAttempts(), 7);

	FDxDataChannelSettings Own;
	Own.MaxReconnectAttempts = 1;
	Own.ReconnectInitialDelaySeconds = 0.5f;
	Own.ReconnectMaxDelaySeconds = 0.5f;
	Own.ReconnectBackoffMultiplier = 2.0f;
	TStrongObjectPtr<UDxCoreTestChannel> Channel(NewObject<UDxCoreTestChannel>());
	TStrongObjectPtr<UDxCoreTestListener> Listener(NewObject<UDxCoreTestListener>());
	Channel->OnStateChanged.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveChannelState);
	Channel->InitializeChannel(TEXT("Own"), Own, GI.Get());
	TestEqual(TEXT("own initial"), Channel->TestReconnectDelay(0), 0.5f);
	TestEqual(TEXT("own attempts"), Channel->TestMaxAttempts(), 1);
	TestTrue(TEXT("first reconnect scheduled"), Channel->TestScheduleReconnect());
	TestEqual(TEXT("reconnecting"), Channel->GetState(), EDxDataChannelState::Reconnecting);
	TestEqual(TEXT("attempt counted"), Channel->GetStatus().ReconnectAttemptCount, 1);
	TestTrue(TEXT("same timer not duplicated"), Channel->TestScheduleReconnect());
	TestEqual(TEXT("still one attempt"), Channel->GetStatus().ReconnectAttemptCount, 1);
	// 실제 TimerManager로 Connect 재호출을 확인한다.
	GI->GetTimerManager().Tick(0.01f);
	++GFrameCounter;
	GI->GetTimerManager().Tick(0.6f);
	TestEqual(TEXT("timer calls Connect"), Channel->ConnectCount, 1);
	TestFalse(TEXT("limit reached"), Channel->TestScheduleReconnect());
	TestEqual(TEXT("gave up"), Channel->GetState(), EDxDataChannelState::GaveUp);
	TestEqual(TEXT("GaveUp broadcast once"), Listener->ChannelStates.FilterByPredicate([](EDxDataChannelState State) { return State == EDxDataChannelState::GaveUp; }).Num(), 1);

	Channel->ShutdownChannel();
	TestFalse(TEXT("no reconnect after shutdown"), Channel->TestScheduleReconnect());
	Inherit->ShutdownChannel();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreChannelStatusTest, "DTCore.Channel.Status",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreChannelStatusTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxCoreTestChannel> Channel(NewObject<UDxCoreTestChannel>());
	FDxDataChannelSettings Limited;
	Limited.MaxMessageBytes = 4;
	Channel->InitializeChannel(TEXT("Limited"), Limited, GI.Get());

	TestFalse(TEXT("empty destination rejected"), Channel->PublishBinary(FString(), {1}, {}, FSTOMPRequestCompleted()));
	TestEqual(TEXT("empty destination not a drop"), Channel->GetStatus().DroppedCount, static_cast<int64>(0));
	TestTrue(TEXT("small message passes"), Channel->PublishBinary(TEXT("a"), {1, 2, 3, 4}, {}, FSTOMPRequestCompleted()));
	TestFalse(TEXT("large message rejected"), Channel->PublishBinary(TEXT("a"), {1, 2, 3, 4, 5}, {}, FSTOMPRequestCompleted()));
	TestTrue(TEXT("text uses utf-8 size"), Channel->PublishText(TEXT("a"), TEXT("가"), {}, FSTOMPRequestCompleted()));
	TestFalse(TEXT("text over limit rejected"), Channel->PublishText(TEXT("a"), TEXT("가가"), {}, FSTOMPRequestCompleted()));
	TestTrue(TEXT("only allowed sizes reach transport"), Channel->PublishedSizes == TArray<int32>({4, 3}));
	Channel->TestRecord(10, 1, 5);
	Channel->TestSetLastError(TEXT("fixture error"));
	const FDxDataChannelStatus Status = Channel->GetStatus();
	TestEqual(TEXT("name"), Status.ChannelName, FName(TEXT("Limited")));
	TestEqual(TEXT("published"), Status.PublishedCount, static_cast<int64>(12));
	TestEqual(TEXT("dropped"), Status.DroppedCount, static_cast<int64>(3));
	TestEqual(TEXT("backpressure"), Status.BackpressureCount, static_cast<int64>(5));
	TestEqual(TEXT("last error"), Status.LastError, FString(TEXT("fixture error")));
	Channel->ShutdownChannel();
	TestFalse(TEXT("publish after shutdown rejected"), Channel->PublishBinary(TEXT("a"), {1}, {}, FSTOMPRequestCompleted()));
	TestEqual(TEXT("shutdown publish counted as drop"), Channel->GetStatus().DroppedCount, static_cast<int64>(4));

	// 공통 규약 도우미
	TestEqual(TEXT("destination join"), UDxDataChannelLibrary::MakeDestination({TEXT("topic"), TEXT("virtual"), TEXT("sensor"), TEXT("export"), TEXT("0")}), FString(TEXT("topic.virtual.sensor.export.0")));
	TestTrue(TEXT("invalid segment rejected"), UDxDataChannelLibrary::MakeDestination({TEXT("topic"), TEXT("Bad Name")}).IsEmpty());
	TestTrue(TEXT("valid destination"), UDxDataChannelLibrary::IsValidDestination(TEXT("topic.scenario")));
	TestFalse(TEXT("empty segment"), UDxDataChannelLibrary::IsValidDestination(TEXT("topic..scenario")));
	TestFalse(TEXT("trailing dot"), UDxDataChannelLibrary::IsValidDestination(TEXT("topic.")));
	TestFalse(TEXT("stomp path style"), UDxDataChannelLibrary::IsValidDestination(TEXT("/topic/a")));
	TMap<FName, FString> Existing;
	Existing.Add(UDxDataChannelLibrary::MessageIdHeader, TEXT("keep"));
	const TMap<FName, FString> Headers = UDxDataChannelLibrary::MakeStandardHeaders(TEXT("v1"), TEXT("ignored"), Existing);
	TestEqual(TEXT("schema header"), Headers.FindRef(UDxDataChannelLibrary::SchemaVersionHeader), FString(TEXT("v1")));
	TestEqual(TEXT("existing message id kept"), Headers.FindRef(UDxDataChannelLibrary::MessageIdHeader), FString(TEXT("keep")));
	FDateTime Parsed;
	TestTrue(TEXT("sent-at is ISO-8601"), FDateTime::ParseIso8601(*Headers.FindRef(UDxDataChannelLibrary::SentAtHeader), Parsed));
	const TMap<FName, FString> Generated = UDxDataChannelLibrary::MakeStandardHeaders(FString(), FString(), {});
	TestFalse(TEXT("generated message id"), Generated.FindRef(UDxDataChannelLibrary::MessageIdHeader).IsEmpty());
	TestFalse(TEXT("empty schema omitted"), Generated.Contains(UDxDataChannelLibrary::SchemaVersionHeader));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreChannelThreadSafetyTest, "DTCore.Channel.ThreadSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreChannelThreadSafetyTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxCoreTestChannel> Channel(NewObject<UDxCoreTestChannel>());
	TStrongObjectPtr<UDxCoreTestListener> Listener(NewObject<UDxCoreTestListener>());
	Channel->OnStateChanged.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveChannelState);
	Channel->InitializeChannel(TEXT("Worker"), FDxDataChannelSettings(), GI.Get());

	static constexpr int32 Workers = 4;
	static constexpr int32 Iterations = 2000;
	TArray<TFuture<void>> Futures;
	UDxCoreTestChannel* Raw = Channel.Get();
	for (int32 Worker = 0; Worker < Workers; ++Worker)
	{
		Futures.Add(Async(EAsyncExecution::Thread, [Raw, Worker]()
		{
			for (int32 Index = 0; Index < Iterations; ++Index)
			{
				Raw->TestRecord(1, 1, 1);
				Raw->TestSetLastError(FString::Printf(TEXT("worker-%d"), Worker));
				if (Index % 100 == 0)
				{
					Raw->TestUpdateState(Index % 200 == 0 ? EDxDataChannelState::Connected : EDxDataChannelState::Reconnecting);
				}
			}
		}));
	}
	// 작업 중 GameThread에서 읽어도 안전해야 한다.
	while (Futures.ContainsByPredicate([](const TFuture<void>& Future) { return !Future.IsReady(); }))
	{
		const FDxDataChannelStatus Snapshot = Channel->GetStatus();
		TestTrue(TEXT("snapshot counters monotonic"), Snapshot.PublishedCount >= 0 && Snapshot.PublishedCount <= Workers * Iterations);
		FPlatformProcess::Sleep(0.001f);
	}
	for (TFuture<void>& Future : Futures)
	{
		Future.Wait();
	}
	PumpGameThread();
	const FDxDataChannelStatus Status = Channel->GetStatus();
	TestEqual(TEXT("published exact"), Status.PublishedCount, static_cast<int64>(Workers * Iterations));
	TestEqual(TEXT("dropped exact"), Status.DroppedCount, static_cast<int64>(Workers * Iterations));
	TestEqual(TEXT("backpressure exact"), Status.BackpressureCount, static_cast<int64>(Workers * Iterations));
	TestTrue(TEXT("last error from a worker"), Status.LastError.StartsWith(TEXT("worker-")));
	TestTrue(TEXT("state changes broadcast"), Listener->ChannelStates.Num() > 0);
	TestEqual(TEXT("broadcast only on GameThread"), Listener->ChannelStatesOffGameThread, 0);

	const int32 Before = Listener->ChannelStates.Num();
	Channel->ShutdownChannel();
	Async(EAsyncExecution::Thread, [Raw]() { Raw->TestUpdateState(EDxDataChannelState::Connected); }).Wait();
	PumpGameThread();
	TestEqual(TEXT("no broadcast after shutdown"), Listener->ChannelStates.Num(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTCoreStompChannelTest, "DTCore.Channel.StompChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTCoreStompChannelTest::RunTest(const FString&)
{
	FScopedDataChannelSettings ScopedSettings;
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>());
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws(NewObject<UDxWebSocketSubsystem>(GI.Get()));
	TStrongObjectPtr<UDxCoreTestListener> Listener(NewObject<UDxCoreTestListener>());
	auto DefaultFake = MakeShared<FDTCoreChannelFakeStomp>();
	Ws->StompClient = DefaultFake;
	auto Fake = MakeShared<FDTCoreChannelFakeStomp>();
	auto* Channel = NewObject<UDxCoreTestStompChannel>(Ws.Get());
	Channel->FakeClient = Fake;
	Channel->OnStateChanged.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveChannelState);
	FSTOMPRequestCompleted Callback;
	Callback.BindDynamic(Listener.Get(), &UDxCoreTestListener::ReceivePublish);
	FSTOMPSubscriptionEvent Event;
	Event.BindDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveMessage);

	FDxDataChannelSettings Settings;
	Settings.ChannelName = TEXT("Bulk");
	Settings.Url = TEXT("ws://bulk.fixture:1");
	Settings.MaxMessageBytes = 8;
	Settings.ReceiptTimeoutSeconds = 0.1f;
	Settings.ReconnectInitialDelaySeconds = 0.5f;
	TestTrue(TEXT("register stomp channel"), Ws->RegisterChannelWithSettings(Channel, Settings));
	TestEqual(TEXT("own client created"), Channel->CreateCount, 1);
	TestEqual(TEXT("own client connect called"), Fake->ConnectCalls, 1);
	TestEqual(TEXT("default client untouched"), DefaultFake->ConnectCalls, 0);
	TestEqual(TEXT("connecting until transport event"), Channel->GetState(), EDxDataChannelState::Connecting);

	const FString Key = Channel->Subscribe(TEXT("topic.bulk"), Event, Callback);
	TestFalse(TEXT("subscription key returned"), Key.IsEmpty());
	TestEqual(TEXT("not issued before connect"), Fake->Subscribed.Num(), 0);
	TestFalse(TEXT("publish before connect rejected"), Channel->PublishBinary(TEXT("topic.bulk"), {1}, {}, Callback));
	TestEqual(TEXT("drop counted"), Channel->GetStatus().DroppedCount, static_cast<int64>(1));

	Fake->ConnectedEvent.Broadcast(TEXT("1.2"), TEXT("s"), FString());
	PumpGameThread();
	TestEqual(TEXT("connected"), Channel->GetState(), EDxDataChannelState::Connected);
	if (!TestEqual(TEXT("subscription issued on connect"), Fake->Subscribed.Num(), 1))
	{
		return false;
	}
	Fake->Subscribed[0].Completion.Execute(true, FString());
	PumpGameThread();
	TestEqual(TEXT("subscribe completion delivered"), Listener->PublishSuccessCount, 1);

	FDTCoreChannelFakeMessage Message;
	Message.Destination = TEXT("topic.bulk");
	Message.Body = {'o', 'k', 0};
	Fake->Subscribed[0].Event.Execute(Message);
	TestEqual(TEXT("frame deferred to GameThread"), Listener->ReceivedCount, 0);
	PumpGameThread();
	TestEqual(TEXT("frame delivered"), Listener->ReceivedCount, 1);

	TestTrue(TEXT("publish without receipt"), Channel->PublishBinary(TEXT("topic.bulk"), {1, 2, 3}, {}, FSTOMPRequestCompleted()));
	TestFalse(TEXT("publish over max bytes rejected"), Channel->PublishBinary(TEXT("topic.bulk"), {1, 2, 3, 4, 5, 6, 7, 8, 9}, {}, Callback));
	TestTrue(TEXT("publish with receipt"), Channel->PublishText(TEXT("topic.bulk"), TEXT("r"), {}, Callback));
	TestTrue(TEXT("publish for timeout"), Channel->PublishText(TEXT("topic.bulk"), TEXT("t"), {}, Callback));
	if (TestEqual(TEXT("three frames sent"), Fake->Sent.Num(), 3))
	{
		TestFalse(TEXT("no receipt requested"), Fake->Sent[0].Completion.IsBound());
		TestTrue(TEXT("receipt requested"), Fake->Sent[1].Completion.IsBound());
		Fake->Sent[1].Completion.Execute(true, FString());
		PumpGameThread();
		TestEqual(TEXT("receipt delivered"), Listener->PublishSuccessCount, 2);
		GI->GetTimerManager().Tick(0.01f);
		++GFrameCounter;
		GI->GetTimerManager().Tick(0.2f);
		TestEqual(TEXT("timeout delivered as failure"), Listener->PublishFailureCount, 1);
		TestEqual(TEXT("timeout recorded"), Channel->GetStatus().LastError, FString(TEXT("Receipt timeout: topic.bulk")));
		Fake->Sent[2].Completion.Execute(true, FString());
		PumpGameThread();
		TestEqual(TEXT("late receipt ignored"), Listener->PublishSuccessCount, 2);
	}
	TestEqual(TEXT("published count"), Channel->GetStatus().PublishedCount, static_cast<int64>(3));
	TestEqual(TEXT("drops: before connect + oversize"), Channel->GetStatus().DroppedCount, static_cast<int64>(2));

	// 연결 종료 → 채널 설정 대기 시간으로 재연결 예약
	TestTrue(TEXT("publish before close"), Channel->PublishText(TEXT("topic.bulk"), TEXT("stale"), {}, Callback));
	Fake->ClosedEvent.Broadcast(TEXT("fixture close"));
	PumpGameThread();
	TestEqual(TEXT("reconnecting"), Channel->GetState(), EDxDataChannelState::Reconnecting);
	TestEqual(TEXT("close reason recorded"), Channel->GetStatus().LastError, FString(TEXT("fixture close")));
	TestEqual(TEXT("attempt counted"), Channel->GetStatus().ReconnectAttemptCount, 1);
	TestEqual(TEXT("default connection unaffected"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Disconnected);
	GI->GetTimerManager().Tick(0.01f);
	++GFrameCounter;
	GI->GetTimerManager().Tick(0.6f);
	TestEqual(TEXT("timer reconnects with new client"), Channel->CreateCount, 2);
	Fake->Sent.Last().Completion.Execute(true, FString());
	PumpGameThread();
	TestEqual(TEXT("receipt from previous connection dropped"), Listener->PublishSuccessCount, 2);
	Fake->ConnectedEvent.Broadcast(TEXT("1.2"), TEXT("s2"), FString());
	PumpGameThread();
	TestEqual(TEXT("reconnected"), Channel->GetState(), EDxDataChannelState::Connected);
	TestEqual(TEXT("attempts reset on connect"), Channel->GetStatus().ReconnectAttemptCount, 0);
	if (TestEqual(TEXT("subscription reissued"), Fake->Subscribed.Num(), 2))
	{
		Fake->Subscribed[1].Completion.Execute(true, FString());
		PumpGameThread();
		TestEqual(TEXT("completion reported only once"), Listener->PublishSuccessCount, 2);
		// 이전 연결의 구독 콜백은 버린다.
		Fake->Subscribed[0].Event.Execute(Message);
		PumpGameThread();
		TestEqual(TEXT("stale frame dropped"), Listener->ReceivedCount, 1);
	}

	Channel->Unsubscribe(Key, Callback);
	PumpGameThread();
	TestEqual(TEXT("server unsubscribe requested"), Fake->Unsubscribed.Num(), 1);
	TestEqual(TEXT("unsubscribe completion"), Listener->PublishSuccessCount, 3);
	Channel->Unsubscribe(TEXT("unknown"), Callback);
	TestEqual(TEXT("unknown key fails"), Listener->PublishFailureCount, 2);

	Channel->Disconnect();
	TestEqual(TEXT("manual disconnect"), Channel->GetState(), EDxDataChannelState::Disconnected);
	Fake->ClosedEvent.Broadcast(TEXT("after disconnect"));
	PumpGameThread();
	TestEqual(TEXT("no reconnect after manual disconnect"), Channel->GetState(), EDxDataChannelState::Disconnected);
	TestFalse(TEXT("publish after disconnect rejected"), Channel->PublishBinary(TEXT("topic.bulk"), {1}, {}, Callback));

	TestTrue(TEXT("unregister"), Ws->UnregisterChannel(TEXT("Bulk")));
	TestTrue(TEXT("shut down"), Channel->IsShutdown());
	TestFalse(TEXT("connect after shutdown rejected"), Channel->Connect());
	TestFalse(TEXT("own client released"), Fake->Connected);
	const int32 Events = Listener->ChannelStates.Num();
	Ws->Deinitialize();
	TestEqual(TEXT("no events after shutdown"), Listener->ChannelStates.Num(), Events);
	return true;
}

#endif
