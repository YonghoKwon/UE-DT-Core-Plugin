#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "DTCoreTestTypes.h"
#include "Misc/AutomationTest.h"
#include "Core/DTCoreSettings.h"
#include "Core/DxWebSocketSubsystem.h"
#include "Engine/GameInstance.h"
#include "Subsystems/SubsystemCollection.h"
#include "IStompClient.h"
#include "StompModule.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMemory.h"

class FDTCoreStompSoakCommand : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	TStrongObjectPtr<UGameInstance> GI;
	TStrongObjectPtr<UDxWebSocketSubsystem> Ws;
	TStrongObjectPtr<UDxCoreTestListener> Listener;
	TSharedPtr<IStompClient> Publisher;
	FStompHeader Login;
	FString OldUrl;
	FString OldLogin;
	FString OldPasscode;
	TArray<FString> OldTopics;
	TArray<FString> OldApiTopics;
	int32 OldAttempts;
	FString Url;
	FString Evidence;
	double Start = FPlatformTime::Seconds();
	double ReadyStart = 0;
	double PreviousTick = Start;
	double LastSend = 0;
	double LastPublisherConnect = 0;
	double LastSample = 0;
	double LastReceived = Start;
	int32 PreviousMessages = 0;
	int32 PreviousReady = 0;
	int32 ResumeChecks = 0;
	bool bAwaitingMessage = false;

public:
	explicit FDTCoreStompSoakCommand(FAutomationTestBase* InTest)
		: Test(InTest), GI(NewObject<UGameInstance>()), Ws(NewObject<UDxWebSocketSubsystem>(GI.Get())), Listener(NewObject<UDxCoreTestListener>())
	{
		auto* Settings = GetMutableDefault<UDTCoreSettings>();
		OldUrl = Settings->WebSocketUrl;
		OldLogin = Settings->WebSocketLogin;
		OldPasscode = Settings->WebSocketPasscode;
		OldTopics = Settings->WebSocketTopics;
		OldApiTopics = Settings->ApiTopics;
		OldAttempts = Settings->MaxReconnectAttempts;
		Url = FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_URL"));
		Login.Add(TEXT("login"), FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_USER")));
		Login.Add(TEXT("passcode"), FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_PASSWORD")));
		Settings->WebSocketUrl = Url;
		Settings->WebSocketLogin = Login[TEXT("login")];
		Settings->WebSocketPasscode = Login[TEXT("passcode")];
		Settings->WebSocketTopics = {TEXT("topic.dtcore.quality90")};
		Settings->ApiTopics.Reset();
		Settings->MaxReconnectAttempts = 0;
		Evidence = FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_EVIDENCE"));
		IFileManager::Get().MakeDirectory(*Evidence, true);
		FFileHelper::SaveStringToFile(TEXT("elapsed_seconds,state,ready_count,messages,used_physical_bytes\n"), *(Evidence / TEXT("soak-samples.csv")));
		Ws->ReceivedMessageEvent.BindDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveMessage);
		Ws->OnConnected.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveReady);
		Ws->OnConnectionStateChanged.AddDynamic(Listener.Get(), &UDxCoreTestListener::ReceiveState);
		FSubsystemCollection<UGameInstanceSubsystem> Collection;
		Ws->Initialize(Collection);
	}
	~FDTCoreStompSoakCommand()
	{
		Ws->DisconnectStompClient({});
		Ws->Deinitialize();
		if (Publisher)
		{
			Publisher->Disconnect();
		}
		auto* Settings = GetMutableDefault<UDTCoreSettings>();
		Settings->WebSocketUrl = OldUrl;
		Settings->WebSocketLogin = OldLogin;
		Settings->WebSocketPasscode = OldPasscode;
		Settings->WebSocketTopics = OldTopics;
		Settings->ApiTopics = OldApiTopics;
		Settings->MaxReconnectAttempts = OldAttempts;
	}
	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		GI->GetTimerManager().Tick(Now - PreviousTick);
		PreviousTick = Now;
		if (Listener->ReadyCount != PreviousReady)
		{
			PreviousReady = Listener->ReadyCount;
			bAwaitingMessage = true;
			if (ReadyStart == 0)
			{
				ReadyStart = Now;
				UE_LOG(LogTemp, Display, TEXT("DTCORE_SOAK_READY"));
			}
		}
		if (Listener->ReceivedCount > PreviousMessages)
		{
			PreviousMessages = Listener->ReceivedCount;
			LastReceived = Now;
			if (bAwaitingMessage)
			{
				++ResumeChecks;
				bAwaitingMessage = false;
			}
		}
		if ((!Publisher || !Publisher->IsConnected()) && Now - LastPublisherConnect > 5)
		{
			LastPublisherConnect = Now;
			if (Publisher)
			{
				Publisher->Disconnect();
			}
			Publisher = FModuleManager::LoadModuleChecked<FStompModule>(TEXT("Stomp")).CreateClient(Url);
			Publisher->Connect(Login);
		}
		if (Publisher && Publisher->IsConnected() && Now - LastSend >= 1)
		{
			LastSend = Now;
			Publisher->Send(TEXT("topic.dtcore.quality90"), TEXT("{\"MESSAGE_ID\":\"quality90\"}"));
		}
		if (Now - LastSample >= 30)
		{
			LastSample = Now;
			const FString Sample = FString::Printf(TEXT("%.1f,%d,%d,%d,%llu\n"), Now - Start,
				static_cast<int32>(Ws->GetConnectionState()), Listener->ReadyCount, Listener->ReceivedCount,
				static_cast<uint64>(FPlatformMemory::GetStats().UsedPhysical));
			FFileHelper::SaveStringToFile(Sample, *(Evidence / TEXT("soak-samples.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		}
		if (Now - LastReceived > 120)
		{
			Test->AddError(TEXT("Soak did not receive a broker message for 120 seconds."));
			return true;
		}
		if (ReadyStart > 0 && Now - ReadyStart >= 3600)
		{
			Test->TestTrue(TEXT("five broker restarts recovered"), Listener->ReadyCount >= 6);
			Test->TestTrue(TEXT("message delivery resumed after each connection"), ResumeChecks >= 6);
			Test->TestTrue(TEXT("received throughout real transport soak"), Listener->ReceivedCount >= 3000);
			Test->TestEqual(TEXT("final state Ready"), Ws->GetConnectionState(), EDxWebSocketConnectionState::Ready);
			const int32 Reconnects = Listener->States.FilterByPredicate([](auto State)
														 { return State == EDxWebSocketConnectionState::Reconnecting; })
										 .Num();
			Test->TestTrue(TEXT("reconnect states observed"), Reconnects >= 5);
			Test->AddInfo(FString::Printf(TEXT("SOAK_COMPLETE seconds=%.1f ready=%d messages=%d resumed=%d reconnects=%d"), Now - ReadyStart, Listener->ReadyCount, Listener->ReceivedCount, ResumeChecks, Reconnects));
			return true;
		}
		return false;
	}
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDTCoreStompSoakTest, "DTCore.Soak", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FDTCoreStompSoakTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	// 비활성 시험을 성공으로 반환하지 않고 opt-in 때에만 검색 결과에 등록한다.
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_STOMP_SOAK")) == TEXT("1"))
	{
		Names.Add(TEXT("Stomp60Minutes"));
		Commands.Add(TEXT("Stomp60Minutes"));
	}
}
bool FDTCoreStompSoakTest::RunTest(const FString&)
{
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_URL")).IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_USER")).IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_PASSWORD")).IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("DTCORE_SOAK_EVIDENCE")).IsEmpty())
	{
		AddError(TEXT("Soak requires DTCORE_SOAK_URL, USER, PASSWORD and EVIDENCE environment values."));
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FDTCoreStompSoakCommand>(this));
	return true;
}

#endif
