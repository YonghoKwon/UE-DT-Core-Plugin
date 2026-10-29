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
