#include "Core/DxWebSocketSubsystem.h"

#include "DTCore.h"
#include "Core/DxDataSubsystem.h"
#include "IStompClient.h"
#include "IStompMessage.h"
#include "StompModule.h"
#include "Core/DTCoreSettings.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	constexpr const TCHAR* DTCoreRuntimeOverrideSection = TEXT("DTCoreRuntimeOverride");
	constexpr const TCHAR* DTCoreSettingsSection = TEXT("/Script/DTCore.DTCoreSettings");

	bool TryReadGameIniString(const TCHAR* Section, const TCHAR* Key, FString& OutValue)
	{
		if (!GConfig)
		{
			return false;
		}

		FString Value;
		if (!GConfig->GetString(Section, Key, Value, GGameIni))
		{
			return false;
		}

		Value = Value.TrimStartAndEnd();
		if (Value.IsEmpty())
		{
			return false;
		}

		OutValue = Value;
		return true;
	}

	bool TryReadDTCoreRuntimeOverride(const TCHAR* Key, FString& OutValue)
	{
		// 1순위: 패키징 후 사용자가 직접 수정하기 쉬운 별도 override 섹션
		if (TryReadGameIniString(DTCoreRuntimeOverrideSection, Key, OutValue))
		{
			return true;
		}

		// 2순위: 기존 UDeveloperSettings 섹션에 직접 값을 넣은 경우도 지원
		return TryReadGameIniString(DTCoreSettingsSection, Key, OutValue);
	}

	void EnsureDTCoreRuntimeOverrideTemplate()
	{
		if (!GConfig)
		{
			return;
		}

		static const TCHAR* Keys[] =
		{
			TEXT("BaseApiUrl"),
			TEXT("LocalApiUrl"),
			TEXT("TestApiUrl"),
			TEXT("ProdApiUrl"),
			TEXT("WebSocketUrl"),
			TEXT("WebSocketLogin"),
			TEXT("WebSocketPasscode")
		};

		bool bChanged = false;
		for (const TCHAR* Key : Keys)
		{
			FString ExistingValue;
			if (!GConfig->GetString(DTCoreRuntimeOverrideSection, Key, ExistingValue, GGameIni))
			{
				// 값은 일부러 비워둔다. 운영/개발자가 패키징 후 필요한 값만 채우면 된다.
				GConfig->SetString(DTCoreRuntimeOverrideSection, Key, TEXT(""), GGameIni);
				bChanged = true;
			}
		}

		if (bChanged)
		{
			GConfig->Flush(false, GGameIni);
		}
	}
}

void UDxWebSocketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EnsureDTCoreRuntimeOverrideTemplate();
	LoginInfo.Empty();

	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	if (ensure(Settings))
	{
		FString RuntimeLogin;
		const FString LoginValue = TryReadDTCoreRuntimeOverride(TEXT("WebSocketLogin"), RuntimeLogin)
			? RuntimeLogin
			: Settings->WebSocketLogin;

		FString RuntimePasscode;
		const FString PasscodeValue = TryReadDTCoreRuntimeOverride(TEXT("WebSocketPasscode"), RuntimePasscode)
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

	const UDTCoreSettings* CoreSettings = GetDefault<UDTCoreSettings>();
	FString RuntimeWsUrl;
	const FString WsUrl = TryReadDTCoreRuntimeOverride(TEXT("WebSocketUrl"), RuntimeWsUrl)
		? RuntimeWsUrl
		: ensure(CoreSettings) && !CoreSettings->WebSocketUrl.IsEmpty()
			? CoreSettings->WebSocketUrl
			: TEXT("ws://localhost:61616");

	DX_LOG(GetWorld(), TEXT("[DEBUG] ConnectWebSocket URL: %s"), *WsUrl);

	StompClient = StompModule->CreateClient(WsUrl);

	if (!StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("ConnectWebSocket: StompClient 생성 실패"));
		TryReconnect();
		return;
	}

	StompClient->OnConnected().AddUObject(this, &UDxWebSocketSubsystem::HandleOnConnected);
	StompClient->OnConnectionError().AddUObject(this, &UDxWebSocketSubsystem::HandleOnConnectionError);
	StompClient->OnError().AddUObject(this, &UDxWebSocketSubsystem::HandleOnError);
	StompClient->OnClosed().AddUObject(this, &UDxWebSocketSubsystem::HandleOnClosed);

	this->ConnectStompClient(LoginInfo);
}

void UDxWebSocketSubsystem::ConnectStompClient(const TMap<FName, FString>& Header)
{
	if (!StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("ConnectStompClient: StompClient가 유효하지 않음"));
		return;
	}

	DX_LOG(GetWorld(), TEXT("Connect STOMP WebSocket"));
	StompClient->Connect(Header);
}

void UDxWebSocketSubsystem::DisconnectStompClient(const TMap<FName, FString>& Header)
{
	if (!StompClient.IsValid())
	{
		return;
	}

	DX_LOG(GetWorld(), TEXT("DisConnect STOMP WebSocket"));
	StompClient->Disconnect(Header);
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

	switch (*RouteType)
	{
	case ETopicRouteType::WebSocket:
		DS->EnqueueWebSocketData(BodyString);
		break;
	case ETopicRouteType::Api:
		DS->EnqueueApiData(BodyString);
		break;
	}
}

FString UDxWebSocketSubsystem::Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback,
	const FSTOMPRequestCompleted& CompletionCallback)
{
	if (!StompClient.IsValid())
	{
		DX_LOG(GetWorld(), TEXT("Subscribe: StompClient가 유효하지 않음 - Destination: %s"), *Destination);
		return FString();
	}

	if (!EventCallback.IsBound())
	{
		DX_LOG(GetWorld(), TEXT("Subscribe: EventCallback이 바인딩되지 않음 - Destination: %s"), *Destination);
	}

	TWeakObjectPtr<UDxWebSocketSubsystem> WeakThis(this);

	return StompClient->Subscribe(Destination,
	FStompSubscriptionEvent::CreateLambda([WeakThis, EventCallback](const IStompMessage& Message) -> void
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
			EventCallback,
			Body = MoveTemp(CopiedBody),
			Headers = MoveTemp(CopiedHeaders),
			SubId = MoveTemp(CopiedSubId),
			Dest = MoveTemp(CopiedDest),
			MsgId = MoveTemp(CopiedMsgId),
			AckId = MoveTemp(CopiedAckId)
			]() mutable
		{
			if (!WeakThis.IsValid()) return;

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
	FStompRequestCompleted::CreateLambda([CompletionCallback](bool bSuccess, const FString& Error) -> void
	{
		AsyncTask(ENamedThreads::GameThread, [CompletionCallback, bSuccess, Error]()
		{
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

	StompClient->Unsubscribe(Subscription,
		FStompRequestCompleted::CreateLambda([CompletionCallback](bool bSuccess, const FString& Error)->void
		{
			AsyncTask(ENamedThreads::GameThread, [CompletionCallback, bSuccess, Error]()
			{
				CompletionCallback.ExecuteIfBound(bSuccess, Error);
			});
		})
		);
}

void UDxWebSocketSubsystem::HandleOnConnected(const FString& ProtocolVersion, const FString& SessionId,
                                              const FString& ServerString)
{
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

	for (const auto& Pair : TopicRouteMap)
	{
		FString SubId = Subscribe(Pair.Key, ReceivedMessageEvent, CompletedMessageEvent);
		SubscriptionIds.Add(Pair.Key, SubId);
	}
}

void UDxWebSocketSubsystem::HandleOnConnectionError(const FString& Error)
{
	DX_LOG(GetWorld(), TEXT("ConnectionError STOMP WebSocket: %s"), *Error);

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

	TryReconnect();
}

void UDxWebSocketSubsystem::TryReconnect()
{
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

	float CurrentDelay = InitialRetryDelay * FMath::Pow(BackoffMultiplier, static_cast<float>(RetryCount));
	CurrentDelay = FMath::Min(CurrentDelay, MaxRetryDelay);

	DX_LOG(GetWorld(), TEXT("WebSocket connection lost. Retrying in %.1f seconds... (Attempt: %d)"), CurrentDelay, RetryCount + 1);

	RetryCount++;

	// World 대신 GameInstance의 타이머를 사용
	GameInstance->GetTimerManager().SetTimer(
		ReconnectTimerHandle,
		this,
		&UDxWebSocketSubsystem::ConnectWebSocket,
		CurrentDelay,
		false
	);
}