#pragma once

#include "CoreMinimal.h"
#include "Core/DxWebSocketSubsystem.h"
#include "Api/ApiMessage.h"
#include "WebSocket/TransactionCodeMessage.h"
#include "UI/DxWidget.h"
#include "Engine/GameInstance.h"
#include "DTCoreTestTypes.generated.h"

// UHT가 Shipping에서도 헤더를 처리하므로 비공개 fixture 타입 선언은 유지한다.
// 실제 시험 구현과 실행은 WITH_DEV_AUTOMATION_TESTS로 제한한다.
struct FDxCoreTestParseProbe
{
	TAtomic<bool> Started{false};
	TAtomic<int32> Applied{0};
};

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	void AttachWorldContext(FWorldContext* Context)
	{
		WorldContext = Context;
	}
};

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestSlowTransaction : public UTransactionCodeMessage
{
	GENERATED_BODY()
public:
	TSharedPtr<FDxCoreTestParseProbe, ESPMode::ThreadSafe> Probe;
	virtual TSharedPtr<FTransactionCodeDataBase> ParseToStruct(const FString&) const override;
	virtual void ProcessStructData(const TSharedPtr<FTransactionCodeDataBase>&) override;
};

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestApiMessage : public UApiMessage
{
	GENERATED_BODY()
public:
	virtual TSharedPtr<FApiDataBase> ParseToStruct(const FString&) const override;
};

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestListener : public UObject
{
	GENERATED_BODY()
public:
	int32 ReceivedCount = 0;
	int32 ReadyCount = 0;
	TArray<EDxWebSocketConnectionState> States;
	TWeakObjectPtr<UDxWebSocketSubsystem> DisconnectTarget;
	EDxWebSocketConnectionState DisconnectAtState = EDxWebSocketConnectionState::Disconnected;
	UFUNCTION()
	void ReceiveMessage(const FWebSocketMessage& Message);
	UFUNCTION()
	void ReceiveReady(FString Protocol, FString Session, FString Server);
	UFUNCTION()
	void ReceiveHttp(bool bSuccess, int32 Code, const FString& Content);
	UFUNCTION()
	void ReceiveState(EDxWebSocketConnectionState State, FString Reason);
};

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestCloseOrderWidget : public UDxWidget
{
	GENERATED_BODY()
public:
	TSharedPtr<TArray<FString>> CloseTrace;
	bool bRemovedBeforeHook = false;
	virtual void CloseWidgetAddLogic_Implementation() override;
};
