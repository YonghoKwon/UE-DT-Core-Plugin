#pragma once

#include "CoreMinimal.h"
#include "Core/DxWebSocketSubsystem.h"
#include "Api/ApiMessage.h"
#include "WebSocket/TransactionCodeMessage.h"
#include "UI/DxWidget.h"
#include "UI/DxWidgetFreeview.h"
#include "Core/DxStompChannel.h"
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
	int32 PublishSuccessCount = 0;
	int32 PublishFailureCount = 0;
	UFUNCTION()
	void ReceivePublish(bool bSuccess, FString Error);
	TArray<EDxDataChannelState> ChannelStates;
	int32 ChannelStatesOffGameThread = 0;
	UFUNCTION()
	void ReceiveChannelState(FName ChannelName, EDxDataChannelState State, FString Reason);
};

// 보호 훅을 시험에서 부르기 위한 최소 채널
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestChannel : public UDxDataChannelBase
{
	GENERATED_BODY()
public:
	int32 ConnectCount = 0;
	int32 DisconnectCount = 0;
	int32 InitializedCount = 0;
	int32 ShutdownCount = 0;
	TArray<int32> PublishedSizes;
	virtual bool Connect() override;
	virtual void Disconnect() override;
	virtual FString Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback, const FSTOMPRequestCompleted& CompletionCallback) override;
	virtual void Unsubscribe(const FString& SubscriptionKey, const FSTOMPRequestCompleted& CompletionCallback) override;
	void TestUpdateState(EDxDataChannelState NewState) { UpdateState(NewState, TEXT("fixture")); }
	void TestSetLastError(const FString& Error) { SetLastError(Error); }
	void TestRecord(int64 Published, int64 Dropped, int64 Backpressure)
	{
		RecordPublished(Published);
		RecordDropped(Dropped);
		RecordBackpressure(Backpressure);
	}
	float TestReconnectDelay(int32 Attempt) const { return GetReconnectDelaySeconds(Attempt); }
	int32 TestMaxAttempts() const { return GetMaxReconnectAttempts(); }
	bool TestScheduleReconnect() { return ScheduleReconnect(TEXT("fixture")); }
	void TestCancelReconnect() { CancelReconnect(); }
	FString TestResolveUrl() const { return ResolveUrl(); }
	TMap<FName, FString> TestResolveCredentials() const { return ResolveCredentials(); }
protected:
	virtual void OnChannelInitialized() override { ++InitializedCount; }
	virtual void OnChannelShutdown() override { ++ShutdownCount; }
	virtual bool PublishBytes(const FString& Destination, const TArray<uint8>& Body, const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback) override;
};

// 엔진 STOMP 대신 시험용 연결 객체를 쓰는 채널
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestStompChannel : public UDxStompChannel
{
	GENERATED_BODY()
public:
	TSharedPtr<IStompClient> FakeClient;
	int32 CreateCount = 0;
protected:
	virtual TSharedPtr<IStompClient> CreateStompClient(const FString& Url) override;
};

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestNavigationWidget : public UDxWidget
{
	GENERATED_BODY()
public:
	TArray<FName> HandledTargets;
	TSet<FName> RejectedTargets;
	virtual bool HandleNavigateTo_Implementation(FName SubTarget) override;
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

UCLASS(Transient, NotBlueprintable, HideDropdown)
class UDxCoreTestFreeviewWidget : public UDxWidgetFreeview
{
	GENERATED_BODY()
public:
	FVector2D TestWindowSize = FVector2D(200.0, 100.0);
	FVector2D TestBoundsSize = FVector2D(1000.0, 600.0);
	FVector2D LastMovedPosition = FVector2D::ZeroVector;
	int32 MovedCount = 0;

	UFUNCTION()
	void HandleMoved(UDxWidgetFreeview* Widget, FVector2D Position)
	{
		LastMovedPosition = Position;
		++MovedCount;
	}

	FReply TestDown(const FGeometry& Geometry, const FPointerEvent& Event) { return NativeOnMouseButtonDown(Geometry, Event); }
	FReply TestUp(const FGeometry& Geometry, const FPointerEvent& Event) { return NativeOnMouseButtonUp(Geometry, Event); }
	FReply TestMove(const FGeometry& Geometry, const FPointerEvent& Event) { return NativeOnMouseMove(Geometry, Event); }
	void TestCaptureLost() { NativeOnMouseCaptureLost(FCaptureLostEvent(0, 0)); }
	void TestTick(const FGeometry& Geometry) { NativeTick(Geometry, 0.016f); }

protected:
	virtual FVector2D GetWindowSize() const override { return TestWindowSize; }
	virtual FVector2D GetBoundsSize() const override { return TestBoundsSize; }
	virtual FVector2D ScreenToBounds(FVector2D ScreenPosition) const override { return ScreenPosition; }
};
