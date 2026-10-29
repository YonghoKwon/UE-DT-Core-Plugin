#include "Core/DxWebSocketSubsystem.h"

#include "DTCore.h"
#include "Core/DxDataSubsystem.h"
#include "Core/DTCoreRuntimeConfig.h"
#include "IStompClient.h"
#include "IStompMessage.h"
#include "StompModule.h"
#include "Core/DTCoreSettings.h"

void UDxWebSocketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bIsShuttingDown = false;

	DTCoreRuntimeConfig::EnsureRuntimeOverrideTemplate();
	LoginInfo.Empty();

	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	if (ensure(Settings))
	{
		FString RuntimeLogin;
		const FString LoginValue = DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketLogin"), RuntimeLogin)
			? RuntimeLogin
			: Settings->WebSocketLogin;

		FString RuntimePasscode;
		const FString PasscodeValue = DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketPasscode"), RuntimePasscode)
			? RuntimePasscode
			: Settings->WebSocketPasscode;

		if (!LoginValue.IsEmpty())
		{
			LoginInfo.Add("login", LoginValue);
		}
		if (!PasscodeValue.IsEmpty())
		{
			LoginInfo.Add("passcode", PasscodeValue);
		}

		for (const FString& Topic : Settings->WebSocketTopics)
		{
			TopicRouteMap.Add(Topic, ETopicRouteType::WebSocket);
		}
		for (const FString& Topic : Settings->ApiTopics)
		{
			TopicRouteMap.Add(Topic, ETopicRouteType::Api);
		}
	}

	this->ConnectWebSocket();
}

void UDxWebSocketSubsystem::Deinitialize()
{
	bIsShuttingDown = true;
	ClearSubscribeTimeout();
	ConnectionState = EDxWebSocketConnectionState::Disconnected;
	bWantsConnection = false;
	++ConnectionGeneration;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		// World 대신 GameInstance의 타이머 초기화
		GameInstance->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	}

	if (StompClient.IsValid())
	{
		StompClient->OnConnected().RemoveAll(this);
		StompClient->OnConnectionError().RemoveAll(this);
		StompClient->OnClosed().RemoveAll(this);
		StompClient->OnError().RemoveAll(this);

		if (StompClient->IsConnected())
		{
			StompClient->Disconnect(LoginInfo);
		}
		StompClient.Reset();
	}

	Super::Deinitialize();
}

void UDxWebSocketSubsystem::ConnectWebSocket()
{
	if (bIsShuttingDown) return;
	bWantsConnection = true;
	RetryCount = 0;
	if (UGameInstance* GI = GetGameInstance()) GI->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	BeginConnectionAttempt();
}

void UDxWebSocketSubsystem::BeginConnectionAttempt()
{
	if (bIsShuttingDown || !bWantsConnection) return;
	ClearSubscribeTimeout();
	const uint64 Generation = ++ConnectionGeneration;
	PendingSubscribeCount = 0;
	bSubscriptionBatchStarted = false;
	bConnectedBroadcast = false;
	// 재연결 시 이전 구독 ID 초기화
	SubscriptionIds.Empty();

	// 이미 연결된 클라이언트가 있다면 정리
	if (StompClient.IsValid())
	{
		// 델리게이트 해제 (중복 호출 방지)
		StompClient->OnConnected().RemoveAll(this);
		StompClient->OnConnectionError().RemoveAll(this);
		StompClient->OnClosed().RemoveAll(this);
		StompClient->OnError().RemoveAll(this);

		if (StompClient->IsConnected())
		{
			StompClient->Disconnect(LoginInfo);
		}
		StompClient.Reset();
	}

	FStompModule* StompModule = FModuleManager::LoadModulePtr<FStompModule>("Stomp");
	if (!StompModule)
	{
		DX_LOG(GetWorld(), TEXT("ConnectWebSocket: Stomp 모듈 로드 실패"));
		TryReconnect();
		return;
	}

	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	FString RuntimeWsUrl;
	const FString WsUrl = DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketUrl"), RuntimeWsUrl)
		? RuntimeWsUrl
		: ensure(Settings) && !Settings->WebSocketUrl.IsEmpty()
			? Settings->WebSocketUrl
			: TEXT("ws://localhost:31000");

	DX_LOG(GetWorld(), TEXT("[DEBUG] ConnectWebSocket URL: %s"), *WsUrl);

	StompClient = StompModule->CreateClient(WsUrl);

	if (!StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("ConnectWebSocket: StompClient 생성 실패"));
		TryReconnect();
		return;
	}

	TWeakObjectPtr<UDxWebSocketSubsystem> WeakThis(this);
	StompClient->OnConnected().AddWeakLambda(this, [WeakThis, Generation](const FString& Protocol, const FString& Session, const FString& Server)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Protocol, Session, Server]()
		{
			if (auto* Self = WeakThis.Get(); Self && !Self->bIsShuttingDown && Self->bWantsConnection && Self->ConnectionGeneration == Generation)
				Self->HandleOnConnected(Protocol, Session, Server);
		});
	});
	StompClient->OnConnectionError().AddWeakLambda(this, [WeakThis, Generation](const FString& Error)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Error]()
		{
			if (auto* Self = WeakThis.Get(); Self && !Self->bIsShuttingDown && Self->bWantsConnection && Self->ConnectionGeneration == Generation)
				Self->HandleOnConnectionError(Error);
		});
	});
	StompClient->OnError().AddWeakLambda(this, [WeakThis, Generation](const FString& Error)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Error]()
		{
			if (auto* Self = WeakThis.Get(); Self && !Self->bIsShuttingDown && Self->ConnectionGeneration == Generation)
				Self->HandleOnError(Error);
		});
	});
	StompClient->OnClosed().AddWeakLambda(this, [WeakThis, Generation](const FString& Reason)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Reason]()
		{
			if (auto* Self = WeakThis.Get(); Self && !Self->bIsShuttingDown && Self->bWantsConnection && Self->ConnectionGeneration == Generation)
				Self->HandleOnClosed(Reason);
		});
	});
	SetConnectionState(EDxWebSocketConnectionState::Connecting, TEXT("Connection attempt started"));
	if (bIsShuttingDown || !bWantsConnection || ConnectionGeneration != Generation)
	{
		return;
	}
	StompClient->Connect(LoginInfo);
}

void UDxWebSocketSubsystem::ConnectStompClient(const TMap<FName, FString>& Header)
{
	if (bIsShuttingDown) return;
	LoginInfo = Header;
	if (!bWantsConnection && StompClient.IsValid())
	{
		bWantsConnection = true;
		RetryCount = 0;
		BeginConnectionAttempt();
		return;
	}
	bWantsConnection = true;
	if (!StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("ConnectStompClient: StompClient가 유효하지 않음"));
		return;
	}

	DX_LOG(GetWorld(), TEXT("Connect STOMP WebSocket"));
	const uint64 Generation = ConnectionGeneration;
	SetConnectionState(EDxWebSocketConnectionState::Connecting, TEXT("Connection requested"));
	if (bIsShuttingDown || !bWantsConnection || ConnectionGeneration != Generation)
	{
		return;
	}
	StompClient->Connect(Header);
}

void UDxWebSocketSubsystem::DisconnectStompClient(const TMap<FName, FString>& Header)
{
	bWantsConnection = false;
	++ConnectionGeneration;
	ClearSubscribeTimeout();
	PendingSubscribeCount = 0;
	if (UGameInstance* GI = GetGameInstance()) GI->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	if (StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("DisConnect STOMP WebSocket"));
		StompClient->Disconnect(Header);
	}
	SetConnectionState(EDxWebSocketConnectionState::Disconnected, TEXT("Manual disconnect"));
}

void UDxWebSocketSubsystem::ReceivedMessage(const FWebSocketMessage& Message)
{
	// FStompBuffer Body = Message->GetRawBody();
	const FString& BodyString = Message.BodyString;
	if (BodyString.IsEmpty())
	{
		return;
	}

	const ETopicRouteType* RouteType = TopicRouteMap.Find(Message.Destination);
	if (!RouteType)
	{
		DX_LOG(GetWorld(), TEXT("ReceivedMessage: 등록되지 않은 토픽 [%s]"), *Message.Destination);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI) return;
	UDxDataSubsystem* DS = GI->GetSubsystem<UDxDataSubsystem>();
	if (!DS) return;

	// 두 RouteType 모두 WebSocket 큐(MESSAGE_ID 핸들러)로 보낸다.
	// Api는 WebSocket으로 받는 Api 방식 데이터를 구분하기 위한 값이며, 어느 경로로 들어왔는지 로그로 남긴다.
	const TCHAR* RouteName = *RouteType == ETopicRouteType::Api ? TEXT("Api") : TEXT("WebSocket");
	DX_LOG(GetWorld(), TEXT("ReceivedMessage: RouteType=%s, Topic=%s, Bytes=%d"), RouteName, *Message.Destination, BodyString.Len());
	DS->EnqueueWebSocketData(BodyString);
}

FString UDxWebSocketSubsystem::Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback,
	const FSTOMPRequestCompleted& CompletionCallback)
{
	if (bIsShuttingDown || !bWantsConnection || !StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("Subscribe: StompClient가 유효하지 않음 - Destination: %s"), *Destination);
		return FString();
	}

	if (!EventCallback.IsBound())
	{
		DX_LOG(GetWorld(), TEXT("Subscribe: EventCallback이 바인딩되지 않음 - Destination: %s"), *Destination);
	}

	TWeakObjectPtr<UDxWebSocketSubsystem> WeakThis(this);
	const uint64 Generation = ConnectionGeneration;
	auto Completed = MakeShared<bool>(false);
	if (CompletionCallback.GetUObject() == this && CompletionCallback.GetFunctionName() == GET_FUNCTION_NAME_CHECKED(UDxWebSocketSubsystem, HandleSubscribeComplete))
	{
		BatchReceipts.Add(Completed);
	}

	return StompClient->Subscribe(Destination,
	FStompSubscriptionEvent::CreateLambda([WeakThis, Generation, EventCallback](const IStompMessage& Message) -> void
	{

		FString CopiedBody = Message.GetBodyAsString();
		// const TArray<uint8> CopiedRawBody = Message.GetRawBody();
		TMap<FName, FString> CopiedHeaders = Message.GetHeader();
		FString CopiedSubId = Message.GetSubscriptionId();
		FString CopiedDest = Message.GetDestination();
		FString CopiedMsgId = Message.GetMessageId();
		FString CopiedAckId = Message.GetAckId();

		AsyncTask(ENamedThreads::GameThread, [
			WeakThis,
			Generation,
			EventCallback,
			Body = MoveTemp(CopiedBody),
			Headers = MoveTemp(CopiedHeaders),
			SubId = MoveTemp(CopiedSubId),
			Dest = MoveTemp(CopiedDest),
			MsgId = MoveTemp(CopiedMsgId),
			AckId = MoveTemp(CopiedAckId)
			]() mutable
		{
			if (!WeakThis.IsValid() || WeakThis->bIsShuttingDown || !WeakThis->bWantsConnection || WeakThis->ConnectionGeneration != Generation) return;

			// UWebSocketMessage* Msg = NewObject<UWebSocketMessage>(StrongThis);
			FWebSocketMessage Msg; // 스택 할당 (매우 빠름, GC 없음)
			Msg.BodyString = MoveTemp(Body);
			Msg.Headers = MoveTemp(Headers);
			Msg.SubscriptionId = MoveTemp(SubId);
			Msg.Destination = MoveTemp(Dest);
			Msg.MessageId = MoveTemp(MsgId);
			Msg.AckId = MoveTemp(AckId);

			EventCallback.ExecuteIfBound(Msg);
		});
	}),
	FStompRequestCompleted::CreateLambda([WeakThis, Generation, Completed, CompletionCallback](bool bSuccess, const FString& Error) -> void
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Completed, CompletionCallback, bSuccess, Error]()
		{
			if (!WeakThis.IsValid() || WeakThis->bIsShuttingDown || WeakThis->ConnectionGeneration != Generation || *Completed) return;
			*Completed = true;
			CompletionCallback.ExecuteIfBound(bSuccess, Error);
		});
	})
);
}

void UDxWebSocketSubsystem::Unsubscribe(const FString& Subscription, const FSTOMPRequestCompleted& CompletionCallback)
{
	if (!StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("Unsubscribe: StompClient가 유효하지 않음"));
		return;
	}

	if (Subscription.IsEmpty())
	{
		DX_LOG(GetWorld(), TEXT("Unsubscribe: StompClient가 ID가 비어있음"));
		return;
	}

	TWeakObjectPtr<UDxWebSocketSubsystem> WeakThis(this);
	const uint64 Generation = ConnectionGeneration;
	auto Completed = MakeShared<bool>(false);
	StompClient->Unsubscribe(Subscription,
		FStompRequestCompleted::CreateLambda([WeakThis, Generation, Completed, CompletionCallback](bool bSuccess, const FString& Error)->void
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, Generation, Completed, CompletionCallback, bSuccess, Error]()
			{
				if (!WeakThis.IsValid() || WeakThis->bIsShuttingDown || WeakThis->ConnectionGeneration != Generation || *Completed) return;
				*Completed = true;
				CompletionCallback.ExecuteIfBound(bSuccess, Error);
			});
		})
		);
}

void UDxWebSocketSubsystem::HandleOnConnected(const FString& ProtocolVersion, const FString& SessionId,
                                              const FString& ServerString)
{
	if (bIsShuttingDown || !bWantsConnection || bSubscriptionBatchStarted) return;
	bSubscriptionBatchStarted = true;
	LastSubscribeFailureCount = 0;
	const uint64 Generation = ConnectionGeneration;
	DX_LOG(GetWorld(), TEXT("STOMP WebSocket 연결 성공 - Protocol: %s, Session: %s"), *ProtocolVersion, *SessionId);

	RetryCount = 0;
	// GameInstance의 타이머를 확실하게 클리어
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	}

	if (!ReceivedMessageEvent.IsBound())
	{
		ReceivedMessageEvent.BindDynamic(this, &UDxWebSocketSubsystem::ReceivedMessage);
	}

	PendingProtocolVersion = ProtocolVersion;
	PendingSessionId = SessionId;
	PendingServerString = ServerString;
	PendingSubscribeCount = TopicRouteMap.Num();
	SetConnectionState(EDxWebSocketConnectionState::Subscribing, TEXT("Transport connected; subscriptions pending"));
	if (bIsShuttingDown || !bWantsConnection || ConnectionGeneration != Generation)
	{
		return;
	}
	if (PendingSubscribeCount == 0)
	{
		CompleteSubscriptionBatch();
		return;
	}

	const float Timeout = GetDefault<UDTCoreSettings>()->SubscribeReceiptTimeoutSeconds;
	if (Timeout > 0.0f)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			GI->GetTimerManager().SetTimer(SubscribeTimeoutHandle,
				FTimerDelegate::CreateWeakLambda(this, [this, Generation]()
				{
					HandleSubscribeTimeout(Generation);
				}), Timeout, false);
		}
	}

	// CompletedMessageEvent는 UPROPERTY이므로 BindDynamic 사용 가능
	CompletedMessageEvent.BindDynamic(this, &UDxWebSocketSubsystem::HandleSubscribeComplete);

	for (const auto& Pair : TopicRouteMap)
	{
		FString SubId = Subscribe(Pair.Key, ReceivedMessageEvent, CompletedMessageEvent);
		SubscriptionIds.Add(Pair.Key, SubId);
	}
}

void UDxWebSocketSubsystem::HandleSubscribeComplete(bool bSuccess, FString Error)
{
	if (bIsShuttingDown || !bWantsConnection || bConnectedBroadcast || PendingSubscribeCount <= 0) return;
	if (!bSuccess)
	{
		++LastSubscribeFailureCount;
		DX_LOG(GetWorld(), TEXT("Subscribe 실패: %s"), *Error);
	}

	PendingSubscribeCount--;
	if (PendingSubscribeCount <= 0)
	{
		CompleteSubscriptionBatch();
	}
}

void UDxWebSocketSubsystem::SetConnectionState(EDxWebSocketConnectionState NewState, const FString& Reason)
{
	if (bIsShuttingDown || ConnectionState == NewState)
	{
		return;
	}
	ConnectionState = NewState;
	OnConnectionStateChanged.Broadcast(NewState, Reason);
}

void UDxWebSocketSubsystem::ClearSubscribeTimeout()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetTimerManager().ClearTimer(SubscribeTimeoutHandle);
	}
	for (const auto& Receipt : BatchReceipts)
	{
		*Receipt = true;
	}
	BatchReceipts.Reset();
}

void UDxWebSocketSubsystem::CompleteSubscriptionBatch()
{
	if (bIsShuttingDown || !bWantsConnection || bConnectedBroadcast)
	{
		return;
	}
	ClearSubscribeTimeout();
	PendingSubscribeCount = 0;
	bConnectedBroadcast = true;
	// 상태 이벤트에서 재진입하더라도 완료된 batch의 기존 알림은 정확히 한 번 전달한다.
	const FString Protocol = PendingProtocolVersion;
	const FString Session = PendingSessionId;
	const FString Server = PendingServerString;
	SetConnectionState(EDxWebSocketConnectionState::Ready, TEXT("Subscription batch completed"));
	OnConnected.Broadcast(Protocol, Session, Server);
}

void UDxWebSocketSubsystem::HandleSubscribeTimeout(uint64 Generation)
{
	if (bIsShuttingDown || !bWantsConnection || ConnectionGeneration != Generation || bConnectedBroadcast || PendingSubscribeCount <= 0)
	{
		return;
	}
	LastSubscribeFailureCount += PendingSubscribeCount;
	CompleteSubscriptionBatch();
}

void UDxWebSocketSubsystem::HandleOnConnectionError(const FString& Error)
{
	DX_LOG(GetWorld(), TEXT("ConnectionError STOMP WebSocket: %s"), *Error);

	++ConnectionGeneration;
	ClearSubscribeTimeout();
	TryReconnect();
}

void UDxWebSocketSubsystem::HandleOnError(const FString& Error)
{
	DX_LOG(GetWorld(), TEXT("Error STOMP WebSocket: %s"), *Error);
}

void UDxWebSocketSubsystem::HandleOnClosed(const FString& Reason)
{
	DX_LOG(GetWorld(), TEXT("Closed STOMP WebSocket: %s"), *Reason);

	if (!StompClient.IsValid())
	{
		return;
	}

	++ConnectionGeneration;
	ClearSubscribeTimeout();
	TryReconnect();
}

void UDxWebSocketSubsystem::TryReconnect()
{
	if (bIsShuttingDown || !bWantsConnection) return;
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		DX_LOG(GetWorld(), TEXT("TryReconnect: Game Instance가 유효하지 않음"));
		return;
	}

	if (GameInstance->GetTimerManager().IsTimerActive(ReconnectTimerHandle))
	{
		return;
	}

	// 최대 재시도 횟수 초과 시 중단 (0 = 무제한)
	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	const int32 MaxAttempts = Settings ? Settings->MaxReconnectAttempts : 10;
	if (MaxAttempts > 0 && RetryCount >= MaxAttempts)
	{
		SetConnectionState(EDxWebSocketConnectionState::GaveUp, TEXT("Maximum reconnect attempts reached"));
		DX_LOG(GetWorld(), TEXT("TryReconnect: 최대 재시도 횟수(%d) 초과 - 재연결 중단. ConnectWebSocket() 호출로 수동 재시도 가능"), MaxAttempts);
		return;
	}

	float CurrentDelay = InitialRetryDelay * FMath::Pow(BackoffMultiplier, static_cast<float>(RetryCount));
	CurrentDelay = FMath::Min(CurrentDelay, MaxRetryDelay);

	DX_LOG(GetWorld(), TEXT("WebSocket connection lost. Retrying in %.1f seconds... (Attempt: %d)"), CurrentDelay, RetryCount + 1);

	RetryCount++;

	// World 대신 GameInstance의 타이머를 사용
	GameInstance->GetTimerManager().SetTimer(
		ReconnectTimerHandle,
		this,
		&UDxWebSocketSubsystem::BeginConnectionAttempt,
		CurrentDelay,
		false
	);
	SetConnectionState(EDxWebSocketConnectionState::Reconnecting, TEXT("Reconnect scheduled"));
}
