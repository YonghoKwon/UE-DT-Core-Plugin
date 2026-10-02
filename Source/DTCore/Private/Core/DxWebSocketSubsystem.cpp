#include "Core/DxWebSocketSubsystem.h"
#include "DTCore.h"
#include "Core/DxDataSubsystem.h"
#include "Core/DTCoreRuntimeConfig.h"
#include "Core/DTCoreSettings.h"
#include "IStompClient.h"
#include "IStompMessage.h"
#include "StompModule.h"
#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "HAL/ThreadSafeBool.h"

struct FDxSubscriptionOperation
{
	FThreadSafeBool bCompleted=false;
	uint64 Generation=0;
	FTimerHandle Timeout;
	TFunction<void(bool,const FString&)> Completion;
};

void UDxWebSocketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UDxDataSubsystem>();
	bIsShuttingDown=false;
	DTCoreRuntimeConfig::EnsureRuntimeOverrideTemplate();
	LoginInfo.Empty(); TopicRouteMap.Empty();
	const auto* S=GetDefault<UDTCoreSettings>();
	FString Value;
	const FString Login=DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketLogin"),Value)?Value:S->WebSocketLogin;
	const FString Password=DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketPasscode"),Value)?Value:S->WebSocketPasscode;
	if (!Login.IsEmpty()) LoginInfo.Add("login",Login);
	if (!Password.IsEmpty()) LoginInfo.Add("passcode",Password);
	for (const auto& Topic:S->WebSocketTopics) if (!Topic.IsEmpty()) TopicRouteMap.Add(Topic,ETopicRouteType::WebSocket);
	for (const auto& Topic:S->ApiTopics) if (!Topic.IsEmpty()) TopicRouteMap.Add(Topic,ETopicRouteType::Api);
	ConnectWebSocket();
}

void UDxWebSocketSubsystem::CancelSubscriptionOperations()
{
	if (auto* GI=GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(ReconnectTimerHandle);
		for (auto& Op:SubscriptionOperations) { Op->bCompleted=true; GI->GetTimerManager().ClearTimer(Op->Timeout); }
	}
	SubscriptionOperations.Reset();
}

void UDxWebSocketSubsystem::Deinitialize()
{
	bIsShuttingDown=true; bWantsConnection=false; ++ConnectionGeneration;
	bTransportConnected=false; bSubscriptionsReady=false; CancelSubscriptionOperations();
	if (StompClient.IsValid())
	{
		StompClient->OnConnected().RemoveAll(this); StompClient->OnConnectionError().RemoveAll(this);
		StompClient->OnClosed().RemoveAll(this); StompClient->OnError().RemoveAll(this);
		if (StompClient->IsConnected()) StompClient->Disconnect(LoginInfo);
		StompClient.Reset();
	}
	SubscriptionIds.Reset(); SubscriptionStatuses.Reset(); Super::Deinitialize();
}

void UDxWebSocketSubsystem::ConnectWebSocket()
{
	if (bIsShuttingDown) return;
	bWantsConnection=true; RetryCount=0; BeginConnectionAttempt();
}

void UDxWebSocketSubsystem::BeginConnectionAttempt()
{
	if (bIsShuttingDown || !bWantsConnection) return;
	++ConnectionGeneration; CancelSubscriptionOperations();
	SubscriptionIds.Reset(); SubscriptionStatuses.Reset(); PendingSubscribeCount=0;
	bTransportConnected=false; bSubscriptionsReady=false; bReadyBroadcast=false;
	if (StompClient.IsValid())
	{
		StompClient->OnConnected().RemoveAll(this); StompClient->OnConnectionError().RemoveAll(this);
		StompClient->OnClosed().RemoveAll(this); StompClient->OnError().RemoveAll(this);
		if (StompClient->IsConnected()) StompClient->Disconnect(LoginInfo);
		StompClient.Reset();
	}
	FString Override;
	const FString Url=DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketUrl"),Override)?Override:GetDefault<UDTCoreSettings>()->WebSocketUrl;
	if (Url.IsEmpty()) { UE_LOG(LogBase,Warning,TEXT("DTCore WebSocket URL is not configured; no connection attempted.")); return; }
	auto* Module=FModuleManager::LoadModulePtr<FStompModule>("Stomp");
	if (!Module) { TryReconnect(); return; }
	StompClient=Module->CreateClient(Url);
	if (!StompClient.IsValid()) { TryReconnect(); return; }
	const uint64 Generation=ConnectionGeneration;
	StompClient->OnConnected().AddWeakLambda(this,[this,Generation](const FString& P,const FString& S,const FString& Server)
	{ if (!bIsShuttingDown && Generation==ConnectionGeneration) HandleOnConnected(P,S,Server); });
	StompClient->OnConnectionError().AddWeakLambda(this,[this,Generation](const FString& E)
	{ if (!bIsShuttingDown && Generation==ConnectionGeneration) HandleOnConnectionError(E); });
	StompClient->OnClosed().AddWeakLambda(this,[this,Generation](const FString& R)
	{ if (!bIsShuttingDown && Generation==ConnectionGeneration) HandleOnClosed(R); });
	StompClient->OnError().AddWeakLambda(this,[this,Generation](const FString& E)
	{ if (!bIsShuttingDown && Generation==ConnectionGeneration) HandleOnError(E); });
	ConnectStompClient(LoginInfo);
}

void UDxWebSocketSubsystem::ConnectStompClient(const TMap<FName,FString>& Header)
{
	if (bIsShuttingDown || !StompClient.IsValid()) return;
	if (!bWantsConnection) { LoginInfo=Header; bWantsConnection=true; RetryCount=0; BeginConnectionAttempt(); return; }
	bWantsConnection=true; StompClient->Connect(Header);
}

void UDxWebSocketSubsystem::DisconnectStompClient(const TMap<FName,FString>& Header)
{
	bWantsConnection=false; ++ConnectionGeneration; bTransportConnected=false; bSubscriptionsReady=false; PendingSubscribeCount=0;
	CancelSubscriptionOperations(); SubscriptionIds.Reset(); SubscriptionStatuses.Reset();
	if (StompClient.IsValid()) StompClient->Disconnect(Header);
	OnSubscriptionsChanged.Broadcast();
}

void UDxWebSocketSubsystem::ReceivedMessage(const FWebSocketMessage& Message)
{
	if (bIsShuttingDown || Message.BodyString.IsEmpty() || !TopicRouteMap.Contains(Message.Destination)) return;
	auto* GI=GetGameInstance(); auto* Data=GI?GI->GetSubsystem<UDxDataSubsystem>():nullptr;
	// ApiTopics의 기존 TC 라우팅 계약도 보존한다.
	if (Data) Data->EnqueueWebSocketData(Message.BodyString);
}

void UDxWebSocketSubsystem::CompleteOperation(const TSharedPtr<FDxSubscriptionOperation,ESPMode::ThreadSafe>& Op,bool Success,const FString& Error)
{
	if (bIsShuttingDown || Op->Generation!=ConnectionGeneration || Op->bCompleted.AtomicSet(true)) return;
	if (auto* GI=GetGameInstance()) GI->GetTimerManager().ClearTimer(Op->Timeout);
	SubscriptionOperations.Remove(Op); Op->Completion(Success,Error);
}

FString UDxWebSocketSubsystem::SubscribeInternal(const FString& Destination,const FSTOMPSubscriptionEvent& Event,TFunction<void(bool,const FString&)> Completion)
{
	if (bIsShuttingDown || !StompClient.IsValid() || !StompClient->IsConnected())
	{ Completion(false,TEXT("STOMP transport is not connected.")); return FString(); }
	const uint64 Generation=ConnectionGeneration; TWeakObjectPtr<UDxWebSocketSubsystem> Weak(this);
	auto Op=MakeShared<FDxSubscriptionOperation,ESPMode::ThreadSafe>();
	Op->Generation=Generation; Op->Completion=MoveTemp(Completion); SubscriptionOperations.Add(Op);
	const auto Finish=[Weak,Op](bool Success,const FString& Error)
	{
		AsyncTask(ENamedThreads::GameThread,[Weak,Op,Success,Error]()
		{ if (auto* Self=Weak.Get()) Self->CompleteOperation(Op,Success,Error); });
	};
	if (auto* GI=GetGameInstance())
		GI->GetTimerManager().SetTimer(Op->Timeout,FTimerDelegate::CreateLambda([Finish]()
		{ Finish(false,TEXT("Subscription receipt timeout")); }),FMath::Max(0.1f,GetDefault<UDTCoreSettings>()->SubscriptionReceiptTimeoutSeconds),false);
	return StompClient->Subscribe(Destination,FStompSubscriptionEvent::CreateLambda([Weak,Generation,Event](const IStompMessage& Message)
	{
		FWebSocketMessage Copy; Copy.BodyString=Message.GetBodyAsString(); Copy.Headers=Message.GetHeader();
		Copy.SubscriptionId=Message.GetSubscriptionId(); Copy.Destination=Message.GetDestination();
		Copy.MessageId=Message.GetMessageId(); Copy.AckId=Message.GetAckId();
		AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Event,Copy=MoveTemp(Copy)]()
		{ if (auto* Self=Weak.Get(); Self && !Self->bIsShuttingDown && Generation==Self->ConnectionGeneration) Event.ExecuteIfBound(Copy); });
	}),FStompRequestCompleted::CreateLambda(Finish));
}

FString UDxWebSocketSubsystem::Subscribe(const FString& D,const FSTOMPSubscriptionEvent& E,const FSTOMPRequestCompleted& C)
{ return SubscribeInternal(D,E,[C](bool S,const FString& Error) { C.ExecuteIfBound(S,Error); }); }

void UDxWebSocketSubsystem::Unsubscribe(const FString& Id,const FSTOMPRequestCompleted& Completion)
{
	if (bIsShuttingDown || !StompClient.IsValid() || Id.IsEmpty())
	{ Completion.ExecuteIfBound(false,TEXT("Invalid STOMP subscription/client.")); return; }
	TWeakObjectPtr<UDxWebSocketSubsystem> Weak(this); const uint64 Generation=ConnectionGeneration;
	StompClient->Unsubscribe(Id,FStompRequestCompleted::CreateLambda([Weak,Generation,Completion](bool Success,const FString& Error)
	{
		AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Completion,Success,Error]()
		{ if (auto* Self=Weak.Get(); Self && !Self->bIsShuttingDown && Generation==Self->ConnectionGeneration) Completion.ExecuteIfBound(Success,Error); });
	}));
}

void UDxWebSocketSubsystem::HandleOnConnected(const FString& Protocol,const FString& Session,const FString& Server)
{
	if (bIsShuttingDown || !bWantsConnection) return;
	bTransportConnected=true; RetryCount=0; bReadyBroadcast=false;
	if (auto* GI=GetGameInstance()) GI->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	PendingProtocolVersion=Protocol; PendingSessionId=Session; PendingServerString=Server;
	OnTransportConnected.Broadcast(Protocol,Session,Server);
	ReceivedMessageEvent.BindDynamic(this,&UDxWebSocketSubsystem::ReceivedMessage);
	SubscriptionStatuses.Reset(); PendingSubscribeCount=TopicRouteMap.Num();
	for (const auto& Pair:TopicRouteMap)
	{ FDTCoreSubscriptionStatus Status; Status.Topic=Pair.Key; SubscriptionStatuses.Add(Pair.Key,Status); }
	if (!PendingSubscribeCount) { FinishSubscriptionBatch(); return; }
	const uint64 Generation=ConnectionGeneration; TWeakObjectPtr<UDxWebSocketSubsystem> Weak(this);
	for (const auto& Pair:TopicRouteMap)
	{
		const FString Topic=Pair.Key;
		const FString Id=SubscribeInternal(Topic,ReceivedMessageEvent,[Weak,Generation,Topic](bool Success,const FString& Error)
		{ if (auto* Self=Weak.Get()) Self->RecordSubscriptionResult(Topic,Generation,Success,Error); });
		SubscriptionIds.Add(Topic,Id);
	}
}

void UDxWebSocketSubsystem::RecordSubscriptionResult(const FString& Topic,uint64 Generation,bool Success,const FString& Error)
{
	if (bIsShuttingDown || Generation!=ConnectionGeneration) return;
	auto* Status=SubscriptionStatuses.Find(Topic);
	if (!Status || Status->State!=EDxSubscriptionState::Pending) return;
	Status->State=Success?EDxSubscriptionState::Ready:(Error.Contains(TEXT("timeout"))?EDxSubscriptionState::TimedOut:EDxSubscriptionState::Failed);
	Status->Error=Error; PendingSubscribeCount=FMath::Max(0,PendingSubscribeCount-1);
	OnSubscriptionsChanged.Broadcast(); if (!PendingSubscribeCount) FinishSubscriptionBatch();
}

void UDxWebSocketSubsystem::FinishSubscriptionBatch()
{
	bSubscriptionsReady=bTransportConnected && !bIsShuttingDown && bWantsConnection;
	for (const auto& Pair:SubscriptionStatuses) bSubscriptionsReady &= Pair.Value.State==EDxSubscriptionState::Ready;
	if (bSubscriptionsReady && !bReadyBroadcast)
	{ bReadyBroadcast=true; OnConnected.Broadcast(PendingProtocolVersion,PendingSessionId,PendingServerString); }
}

void UDxWebSocketSubsystem::HandleSubscribeComplete(bool Success,FString Error)
{
	// 구형 콜백은 보존하지만 topic/generation 없는 결과로 준비 상태를 바꾸지 않는다.
	CompletedMessageEvent.ExecuteIfBound(Success,Error);
}

TArray<FDTCoreSubscriptionStatus> UDxWebSocketSubsystem::GetSubscriptionStatuses() const
{
	TArray<FDTCoreSubscriptionStatus> Result; SubscriptionStatuses.GenerateValueArray(Result);
	Result.Sort([](const auto& A,const auto& B) { return A.Topic<B.Topic; }); return Result;
}
void UDxWebSocketSubsystem::HandleOnConnectionError(const FString& Error)
{ bTransportConnected=false; bSubscriptionsReady=false; ++ConnectionGeneration; CancelSubscriptionOperations(); TryReconnect(); }
void UDxWebSocketSubsystem::HandleOnError(const FString& Error)
{ UE_LOG(LogBase,Warning,TEXT("DTCore STOMP error: %s"),*Error); }
void UDxWebSocketSubsystem::HandleOnClosed(const FString& Reason)
{ bTransportConnected=false; bSubscriptionsReady=false; ++ConnectionGeneration; CancelSubscriptionOperations(); TryReconnect(); }
void UDxWebSocketSubsystem::TryReconnect()
{
	if (bIsShuttingDown || !bWantsConnection) return;
	auto* GI=GetGameInstance(); if (!GI || GI->GetTimerManager().IsTimerActive(ReconnectTimerHandle)) return;
	const int32 Max=GetDefault<UDTCoreSettings>()->MaxReconnectAttempts;
	if (Max>0 && RetryCount>=Max) return;
	const float Delay=FMath::Min(InitialRetryDelay*FMath::Pow(BackoffMultiplier,static_cast<float>(RetryCount++)),MaxRetryDelay);
	TWeakObjectPtr<UDxWebSocketSubsystem> Weak(this);
	GI->GetTimerManager().SetTimer(ReconnectTimerHandle,FTimerDelegate::CreateLambda([Weak]()
	{ if (auto* Self=Weak.Get(); Self && Self->bWantsConnection && !Self->bIsShuttingDown) Self->BeginConnectionAttempt(); }),Delay,false);
}
