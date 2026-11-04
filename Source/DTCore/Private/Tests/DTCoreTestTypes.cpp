#include "DTCoreTestTypes.h"
#include "Api/FApiDataBase.h"
#include "WebSocket/FTransactionCodeDataBase.h"

TSharedPtr<FTransactionCodeDataBase> UDxCoreTestSlowTransaction::ParseToStruct(const FString&) const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Probe)
	{
		Probe->Started.Store(true);
		FPlatformProcess::Sleep(0.05f);
	}
	return MakeShared<FTransactionCodeDataBase>();
#else
	return nullptr;
#endif
}

void UDxCoreTestSlowTransaction::ProcessStructData(const TSharedPtr<FTransactionCodeDataBase>&)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Probe)
	{
		++Probe->Applied;
	}
#endif
}

TSharedPtr<FApiDataBase> UDxCoreTestApiMessage::ParseToStruct(const FString&) const
{
#if WITH_DEV_AUTOMATION_TESTS
	return MakeShared<FApiDataBase>();
#else
	return nullptr;
#endif
}

void UDxCoreTestListener::ReceiveMessage(const FWebSocketMessage& Message)
{
#if WITH_DEV_AUTOMATION_TESTS
	++ReceivedCount;
#endif
}

void UDxCoreTestListener::ReceiveReady(FString Protocol, FString Session, FString Server)
{
#if WITH_DEV_AUTOMATION_TESTS
	++ReadyCount;
#endif
}

void UDxCoreTestListener::ReceiveHttp(bool bSuccess, int32 Code, const FString& Content)
{
#if WITH_DEV_AUTOMATION_TESTS
	++ReceivedCount;
#endif
}

void UDxCoreTestListener::ReceiveState(EDxWebSocketConnectionState State, FString Reason)
{
#if WITH_DEV_AUTOMATION_TESTS
	States.Add(State);
	UE_LOG(LogTemp, Display, TEXT("DTCORE_STATE state=%d reason=%s"), static_cast<int32>(State), *Reason);
	if (State == DisconnectAtState && DisconnectTarget.IsValid())
	{
		DisconnectTarget->DisconnectStompClient({});
	}
#endif
}

void UDxCoreTestListener::ReceivePublish(bool bSuccess, FString Error)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bSuccess)
	{
		++PublishSuccessCount;
	}
	else
	{
		++PublishFailureCount;
	}
#endif
}

void UDxCoreTestListener::ReceiveChannelState(FName ChannelName, EDxDataChannelState State, FString Reason)
{
#if WITH_DEV_AUTOMATION_TESTS
	ChannelStates.Add(State);
	if (!IsInGameThread())
	{
		++ChannelStatesOffGameThread;
	}
#endif
}

bool UDxCoreTestChannel::Connect()
{
#if WITH_DEV_AUTOMATION_TESTS
	++ConnectCount;
	UpdateState(EDxDataChannelState::Connected, TEXT("fixture connect"));
	return true;
#else
	return false;
#endif
}

void UDxCoreTestChannel::Disconnect()
{
#if WITH_DEV_AUTOMATION_TESTS
	++DisconnectCount;
	UpdateState(EDxDataChannelState::Disconnected, TEXT("fixture disconnect"));
#endif
}

FString UDxCoreTestChannel::Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent&, const FSTOMPRequestCompleted&)
{
	return Destination;
}

void UDxCoreTestChannel::Unsubscribe(const FString&, const FSTOMPRequestCompleted&)
{
}

bool UDxCoreTestChannel::PublishBytes(const FString&, const TArray<uint8>& Body, const TMap<FName, FString>&, const FSTOMPRequestCompleted&)
{
#if WITH_DEV_AUTOMATION_TESTS
	PublishedSizes.Add(Body.Num());
	RecordPublished();
	return true;
#else
	return false;
#endif
}

TSharedPtr<IStompClient> UDxCoreTestStompChannel::CreateStompClient(const FString&)
{
#if WITH_DEV_AUTOMATION_TESTS
	++CreateCount;
	return FakeClient;
#else
	return nullptr;
#endif
}

bool UDxCoreTestNavigationWidget::HandleNavigateTo_Implementation(FName SubTarget)
{
#if WITH_DEV_AUTOMATION_TESTS
	HandledTargets.Add(SubTarget);
	return !RejectedTargets.Contains(SubTarget);
#else
	return false;
#endif
}

void UDxCoreTestCloseOrderWidget::CloseWidgetAddLogic_Implementation()
{
#if WITH_DEV_AUTOMATION_TESTS
	bRemovedBeforeHook = GetParent() == nullptr;
	if (CloseTrace)
	{
		CloseTrace->Add(GetName());
	}
	CloseWidget();
#endif
}
