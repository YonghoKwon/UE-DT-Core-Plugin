#include "Core/DxStompChannel.h"

#include "DTCore.h"
#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "IStompClient.h"
#include "IStompMessage.h"
#include "StompModule.h"
#include "TimerManager.h"

bool UDxStompChannel::Connect()
{
	check(IsInGameThread());
	if (IsShutdown()) return false;
	CancelReconnect();
	bWantsConnection = true;
	const uint64 ConnectGeneration = ++Generation;
	ReleaseClient();
	for (auto& Pair : Subscriptions)
	{
		Pair.Value.StompId.Reset();
	}

	StompClient = CreateStompClient(ResolveUrl());
	if (!StompClient.IsValid())
	{
		SetLastError(TEXT("StompClient 생성 실패"));
		ScheduleReconnect(TEXT("Client creation failed"));
		return false;
	}

	TWeakObjectPtr<UDxStompChannel> WeakThis(this);
	StompClient->OnConnected().AddWeakLambda(this, [WeakThis, ConnectGeneration](const FString&, const FString&, const FString&)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, ConnectGeneration]()
		{
			if (UDxStompChannel* Self = WeakThis.Get(); Self && Self->IsCurrentGeneration(ConnectGeneration))
				Self->HandleTransportConnected();
		});
	});
	StompClient->OnConnectionError().AddWeakLambda(this, [WeakThis, ConnectGeneration](const FString& Error)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, ConnectGeneration, Error]()
		{
			if (UDxStompChannel* Self = WeakThis.Get(); Self && Self->IsCurrentGeneration(ConnectGeneration))
				Self->HandleTransportClosed(Error);
		});
	});
	StompClient->OnClosed().AddWeakLambda(this, [WeakThis, ConnectGeneration](const FString& Reason)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, ConnectGeneration, Reason]()
		{
			if (UDxStompChannel* Self = WeakThis.Get(); Self && Self->IsCurrentGeneration(ConnectGeneration))
				Self->HandleTransportClosed(Reason);
		});
	});
	StompClient->OnError().AddWeakLambda(this, [WeakThis, ConnectGeneration](const FString& Error)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, ConnectGeneration, Error]()
		{
			if (UDxStompChannel* Self = WeakThis.Get(); Self && Self->IsCurrentGeneration(ConnectGeneration))
				Self->HandleTransportError(Error);
		});
	});

	UpdateState(EDxDataChannelState::Connecting, TEXT("Connection attempt started"));
	// 상태 이벤트에서 Disconnect를 부를 수 있다.
	if (!IsCurrentGeneration(ConnectGeneration)) return false;
	StompClient->Connect(ResolveCredentials());
	return true;
}

void UDxStompChannel::Disconnect()
{
	check(IsInGameThread());
	bWantsConnection = false;
	++Generation;
	CancelReconnect();
	ResetReconnectAttempts();
	ReleaseClient();
	for (auto& Pair : Subscriptions)
	{
		Pair.Value.StompId.Reset();
	}
	UpdateState(EDxDataChannelState::Disconnected, TEXT("Manual disconnect"));
}

void UDxStompChannel::OnChannelShutdown()
{
	bWantsConnection = false;
	++Generation;
	ReleaseClient();
	Subscriptions.Empty();
}

void UDxStompChannel::ReleaseClient()
{
	if (!StompClient.IsValid()) return;
	StompClient->OnConnected().RemoveAll(this);
	StompClient->OnConnectionError().RemoveAll(this);
	StompClient->OnClosed().RemoveAll(this);
	StompClient->OnError().RemoveAll(this);
	if (StompClient->IsConnected())
	{
		StompClient->Disconnect(FStompHeader());
	}
	StompClient.Reset();
}

TSharedPtr<IStompClient> UDxStompChannel::CreateStompClient(const FString& Url)
{
	FStompModule* StompModule = FModuleManager::LoadModulePtr<FStompModule>("Stomp");
	if (!StompModule) return nullptr;
	return StompModule->CreateClient(Url);
}

void UDxStompChannel::HandleTransportConnected()
{
	ResetReconnectAttempts();
	UpdateState(EDxDataChannelState::Connected, TEXT("Connected"));
	TArray<FString> Keys;
	Subscriptions.GetKeys(Keys);
	for (const FString& Key : Keys)
	{
		// 상태 이벤트나 이전 구독 콜백이 연결을 바꿨으면 멈춘다.
		if (GetState() != EDxDataChannelState::Connected) break;
		IssueSubscription(Key);
	}
}

void UDxStompChannel::HandleTransportClosed(const FString& Reason)
{
	// 닫힌 연결의 receipt·수신·시간 초과 콜백을 무효화한다.
	++Generation;
	SetLastError(Reason);
	ReleaseClient();
	for (auto& Pair : Subscriptions)
	{
		Pair.Value.StompId.Reset();
	}
	if (bWantsConnection)
	{
		ScheduleReconnect(Reason);
	}
	else
	{
		UpdateState(EDxDataChannelState::Disconnected, Reason);
	}
}

void UDxStompChannel::HandleTransportError(const FString& Error)
{
	// STOMP ERROR 프레임은 연결 종료 이벤트가 따로 오므로 기록만 한다.
	SetLastError(Error);
	DX_LOG(GetOwningGameInstance() ? GetOwningGameInstance()->GetWorld() : nullptr, TEXT("DataChannel[%s]: STOMP 오류 - %s"), *GetChannelName().ToString(), *Error);
}

bool UDxStompChannel::PublishBytes(const FString& Destination, const TArray<uint8>& Body,
	const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback)
{
	check(IsInGameThread());
	if (GetState() != EDxDataChannelState::Connected || !StompClient.IsValid() || !StompClient->IsConnected())
	{
		RecordDropped();
		return false;
	}

	FReceiptHandler OnReceipt;
	if (CompletionCallback.IsBound())
	{
		struct FReceiptState
		{
			bool bCompleted = false;
			FTimerHandle TimeoutHandle;
		};
		TSharedRef<FReceiptState> ReceiptState = MakeShared<FReceiptState>();
		TWeakObjectPtr<UDxStompChannel> WeakThis(this);
		const uint64 SendGeneration = Generation;

		const float Timeout = GetChannelSettings().ReceiptTimeoutSeconds;
		UGameInstance* GI = GetOwningGameInstance();
		if (Timeout > 0.0f && GI)
		{
			GI->GetTimerManager().SetTimer(ReceiptState->TimeoutHandle, FTimerDelegate::CreateWeakLambda(this, [WeakThis, ReceiptState, SendGeneration, CompletionCallback, Destination]()
			{
				UDxStompChannel* Self = WeakThis.Get();
				if (!Self || ReceiptState->bCompleted || !Self->IsCurrentGeneration(SendGeneration)) return;
				ReceiptState->bCompleted = true;
				Self->SetLastError(FString::Printf(TEXT("Receipt timeout: %s"), *Destination));
				CompletionCallback.ExecuteIfBound(false, TEXT("Receipt timeout"));
			}), Timeout, false);
		}

		OnReceipt = [WeakThis, ReceiptState, SendGeneration, CompletionCallback](bool bSuccess, const FString& Error)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, ReceiptState, SendGeneration, CompletionCallback, bSuccess, Error]()
			{
				UDxStompChannel* Self = WeakThis.Get();
				if (!Self || ReceiptState->bCompleted || !Self->IsCurrentGeneration(SendGeneration)) return;
				ReceiptState->bCompleted = true;
				if (UGameInstance* OwnerGI = Self->GetOwningGameInstance())
				{
					OwnerGI->GetTimerManager().ClearTimer(ReceiptState->TimeoutHandle);
				}
				if (!bSuccess)
				{
					Self->SetLastError(Error);
				}
				CompletionCallback.ExecuteIfBound(bSuccess, Error);
			});
		};
	}

	if (!SendFrame(Destination, Body, Headers, MoveTemp(OnReceipt)))
	{
		RecordDropped();
		return false;
	}
	RecordPublished();
	return true;
}

bool UDxStompChannel::SendFrame(const FString& Destination, const TArray<uint8>& Body,
	const TMap<FName, FString>& Headers, FReceiptHandler OnReceipt)
{
	if (!StompClient.IsValid()) return false;
	FStompRequestCompleted Completion;
	if (OnReceipt)
	{
		Completion = FStompRequestCompleted::CreateLambda([OnReceipt = MoveTemp(OnReceipt)](bool bSuccess, const FString& Error)
		{
			OnReceipt(bSuccess, Error);
		});
	}
	StompClient->Send(Destination, Body, Headers, Completion);
	return true;
}

FString UDxStompChannel::Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback,
	const FSTOMPRequestCompleted& CompletionCallback)
{
	check(IsInGameThread());
	if (IsShutdown() || Destination.IsEmpty()) return FString();

	const FString Key = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FChannelSubscription& Entry = Subscriptions.Add(Key);
	Entry.Destination = Destination;
	Entry.EventCallback = EventCallback;
	Entry.CompletionCallback = CompletionCallback;
	if (GetState() == EDxDataChannelState::Connected)
	{
		IssueSubscription(Key);
	}
	return Key;
}

void UDxStompChannel::IssueSubscription(const FString& Key)
{
	FChannelSubscription* Entry = Subscriptions.Find(Key);
	if (!Entry || !StompClient.IsValid()) return;

	TWeakObjectPtr<UDxStompChannel> WeakThis(this);
	const uint64 SubscribeGeneration = Generation;
	const FSTOMPSubscriptionEvent EventCallback = Entry->EventCallback;
	Entry->StompId = StompClient->Subscribe(Entry->Destination,
		FStompSubscriptionEvent::CreateLambda([WeakThis, SubscribeGeneration, EventCallback](const IStompMessage& Message)
		{
			UDxStompChannel* Self = WeakThis.Get();
			if (!Self) return;
			FWebSocketMessage Copied;
			Copied.BodyString = Message.GetBodyAsString();
			Copied.RawBody.Append(Message.GetRawBody(), static_cast<int32>(Message.GetRawBodyLength()));
			Copied.Headers = Message.GetHeader();
			Copied.SubscriptionId = Message.GetSubscriptionId();
			Copied.Destination = Message.GetDestination();
			Copied.MessageId = Message.GetMessageId();
			Copied.AckId = Message.GetAckId();
			Self->OnFrameReceived(MoveTemp(Copied), EventCallback, SubscribeGeneration);
		}),
		FStompRequestCompleted::CreateLambda([WeakThis, SubscribeGeneration, Key](bool bSuccess, const FString& Error)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, SubscribeGeneration, Key, bSuccess, Error]()
			{
				UDxStompChannel* Self = WeakThis.Get();
				if (!Self || !Self->IsCurrentGeneration(SubscribeGeneration)) return;
				FChannelSubscription* Found = Self->Subscriptions.Find(Key);
				if (!Found) return;
				if (!bSuccess)
				{
					Self->SetLastError(Error);
				}
				// 사용자 완료 콜백은 첫 요청 결과만 전달한다.
				if (Found->bCompletionReported) return;
				Found->bCompletionReported = true;
				const FSTOMPRequestCompleted Callback = Found->CompletionCallback;
				Callback.ExecuteIfBound(bSuccess, Error);
			});
		}));
}

void UDxStompChannel::OnFrameReceived(FWebSocketMessage&& Message, const FSTOMPSubscriptionEvent& EventCallback, uint64 FrameGeneration)
{
	TWeakObjectPtr<UDxStompChannel> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, FrameGeneration, EventCallback, Message = MoveTemp(Message)]()
	{
		UDxStompChannel* Self = WeakThis.Get();
		if (!Self || !Self->IsCurrentGeneration(FrameGeneration)) return;
		EventCallback.ExecuteIfBound(Message);
	});
}

void UDxStompChannel::Unsubscribe(const FString& SubscriptionKey, const FSTOMPRequestCompleted& CompletionCallback)
{
	check(IsInGameThread());
	FChannelSubscription Removed;
	if (!Subscriptions.RemoveAndCopyValue(SubscriptionKey, Removed))
	{
		CompletionCallback.ExecuteIfBound(false, TEXT("Unknown subscription"));
		return;
	}
	if (Removed.StompId.IsEmpty() || !StompClient.IsValid() || !StompClient->IsConnected())
	{
		// 아직 서버에 요청하지 않았거나 연결이 없으면 목록에서 지운 것으로 끝난다.
		CompletionCallback.ExecuteIfBound(true, FString());
		return;
	}

	TWeakObjectPtr<UDxStompChannel> WeakThis(this);
	const uint64 UnsubscribeGeneration = Generation;
	StompClient->Unsubscribe(Removed.StompId, FStompRequestCompleted::CreateLambda([WeakThis, UnsubscribeGeneration, CompletionCallback](bool bSuccess, const FString& Error)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis, UnsubscribeGeneration, CompletionCallback, bSuccess, Error]()
		{
			UDxStompChannel* Self = WeakThis.Get();
			if (!Self || !Self->IsCurrentGeneration(UnsubscribeGeneration)) return;
			CompletionCallback.ExecuteIfBound(bSuccess, Error);
		});
	}));
}
