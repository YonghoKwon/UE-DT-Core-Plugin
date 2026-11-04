#include "Core/DxDefaultWebSocketChannel.h"

void UDxDefaultWebSocketChannel::SetOwner(UDxWebSocketSubsystem* InOwner)
{
	check(IsInGameThread());
	if (UDxWebSocketSubsystem* Previous = Owner.Get())
	{
		Previous->OnConnectionStateChanged.RemoveDynamic(this, &UDxDefaultWebSocketChannel::HandleOwnerStateChanged);
	}
	Owner = InOwner;
	if (InOwner)
	{
		InOwner->OnConnectionStateChanged.AddDynamic(this, &UDxDefaultWebSocketChannel::HandleOwnerStateChanged);
		UpdateState(MapState(InOwner->GetConnectionState()), TEXT("Default channel attached"));
	}
}

EDxDataChannelState UDxDefaultWebSocketChannel::MapState(EDxWebSocketConnectionState State)
{
	switch (State)
	{
	case EDxWebSocketConnectionState::Connecting: return EDxDataChannelState::Connecting;
	case EDxWebSocketConnectionState::Subscribing:
	case EDxWebSocketConnectionState::Ready: return EDxDataChannelState::Connected;
	case EDxWebSocketConnectionState::Reconnecting: return EDxDataChannelState::Reconnecting;
	case EDxWebSocketConnectionState::GaveUp: return EDxDataChannelState::GaveUp;
	default: return EDxDataChannelState::Disconnected;
	}
}

void UDxDefaultWebSocketChannel::HandleOwnerStateChanged(EDxWebSocketConnectionState NewState, FString Reason)
{
	UpdateState(MapState(NewState), Reason);
}

bool UDxDefaultWebSocketChannel::Connect()
{
	UDxWebSocketSubsystem* Subsystem = Owner.Get();
	if (!Subsystem || IsShutdown()) return false;
	Subsystem->ConnectWebSocket();
	return true;
}

void UDxDefaultWebSocketChannel::Disconnect()
{
	if (UDxWebSocketSubsystem* Subsystem = Owner.Get(); Subsystem && !IsShutdown())
	{
		Subsystem->DisconnectStompClient(Subsystem->LoginInfo);
	}
}

FString UDxDefaultWebSocketChannel::Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback,
	const FSTOMPRequestCompleted& CompletionCallback)
{
	UDxWebSocketSubsystem* Subsystem = Owner.Get();
	return Subsystem && !IsShutdown() ? Subsystem->Subscribe(Destination, EventCallback, CompletionCallback) : FString();
}

void UDxDefaultWebSocketChannel::Unsubscribe(const FString& SubscriptionKey, const FSTOMPRequestCompleted& CompletionCallback)
{
	if (UDxWebSocketSubsystem* Subsystem = Owner.Get(); Subsystem && !IsShutdown())
	{
		Subsystem->Unsubscribe(SubscriptionKey, CompletionCallback);
	}
}

bool UDxDefaultWebSocketChannel::PublishBytes(const FString& Destination, const TArray<uint8>& Body,
	const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback)
{
	UDxWebSocketSubsystem* Subsystem = Owner.Get();
	if (!Subsystem)
	{
		RecordDropped();
		return false;
	}
	// 미연결 드롭은 기존 GetPublishDroppedCount에 집계되고 GetStatus에서 합산된다.
	const bool bSent = Subsystem->PublishBinaryMessage(Destination, Body, Headers, CompletionCallback);
	if (bSent)
	{
		RecordPublished();
	}
	return bSent;
}

FDxDataChannelStatus UDxDefaultWebSocketChannel::GetStatus() const
{
	FDxDataChannelStatus Status = Super::GetStatus();
	if (const UDxWebSocketSubsystem* Subsystem = Owner.Get())
	{
		Status.State = MapState(Subsystem->GetConnectionState());
		Status.DroppedCount += Subsystem->GetPublishDroppedCount();
		Status.ReconnectAttemptCount = Subsystem->GetReconnectAttemptCount();
	}
	return Status;
}

void UDxDefaultWebSocketChannel::OnChannelShutdown()
{
	if (UDxWebSocketSubsystem* Subsystem = Owner.Get())
	{
		Subsystem->OnConnectionStateChanged.RemoveDynamic(this, &UDxDefaultWebSocketChannel::HandleOwnerStateChanged);
	}
	Owner.Reset();
}
