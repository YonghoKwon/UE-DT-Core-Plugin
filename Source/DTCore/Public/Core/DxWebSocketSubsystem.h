#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DxWebSocketSubsystem.generated.h"

class IStompClient;

/** 수신 Topic의 구분. 두 값 모두 WebSocket 큐(MESSAGE_ID 핸들러)로 전달하며, 수신 로그에 구분을 남긴다. */
UENUM()
enum class ETopicRouteType : uint8
{
	WebSocket,  // WebSocketTopics: 일반 WebSocket(TC) 데이터
	Api,        // ApiTopics: WebSocket으로 받는 Api 방식 데이터
};

UENUM(BlueprintType)
enum class EDxWebSocketConnectionState : uint8
{
	Disconnected,
	Connecting,
	Subscribing,
	Ready,
	Reconnecting,
	GaveUp
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDxWebSocketStateChanged, EDxWebSocketConnectionState, State, FString, Reason);

USTRUCT(BlueprintType)
struct FWebSocketMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "STOMP")
	FString BodyString;
	UPROPERTY(BlueprintReadOnly, Category = "STOMP")
	TArray<uint8> RawBody;
	UPROPERTY(BlueprintReadOnly, Category = "STOMP")
	TMap<FName, FString> Headers;
	UPROPERTY(BlueprintReadOnly, Category = "STOMP")
	FString SubscriptionId;
	UPROPERTY(BlueprintReadOnly, Category = "STOMP")
	FString Destination;
	UPROPERTY(BlueprintReadOnly, Category = "STOMP")
	FString MessageId;
	UPROPERTY(BlueprintReadOnly, Category = "STOMP")
	FString AckId;
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FSTOMPRequestCompleted, bool, bSuccess, FString, Error);
DECLARE_DYNAMIC_DELEGATE_OneParam(FSTOMPSubscriptionEvent, const FWebSocketMessage&, Message);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSTOMPConnectedEvent, FString, ProtocolVersion, FString, SessionId, FString, ServerString);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSTOMPConnectionErrorEvent, FString, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSTOMPErrorEvent, FString, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSTOMPCloseEvent, FString, Reason);
/**
 *
 */
UCLASS()
class DTCORE_API UDxWebSocketSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	// Function
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(Category = "DxWebSocket")
	void ConnectWebSocket();
	UFUNCTION(Category = "DxWebSocket")
	void ConnectStompClient(const TMap<FName, FString>& Header);
	UFUNCTION(Category = "DxWebSocket")
	void DisconnectStompClient(const TMap<FName, FString>& Header);
	UFUNCTION(Category = "DxWebSocket")
	void ReceivedMessage(const FWebSocketMessage& Message);
	UFUNCTION(Category = "DxWebSocket")
	FString Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback, const FSTOMPRequestCompleted& CompletionCallback);
	UFUNCTION(Category = "DxWebSocket")
	void Unsubscribe(const FString& Subscription, const FSTOMPRequestCompleted& CompletionCallback);
	// Ready는 성공 여부와 무관하게 모든 구독 완료 callback을 회수한 상태이다.
	UFUNCTION(BlueprintPure, Category = "DxWebSocket")
	EDxWebSocketConnectionState GetConnectionState() const { return ConnectionState; }
	UFUNCTION(BlueprintPure, Category = "DxWebSocket")
	int32 GetReconnectAttemptCount() const { return RetryCount; }
	UFUNCTION(BlueprintPure, Category = "DxWebSocket")
	int32 GetLastSubscribeFailureCount() const { return LastSubscribeFailureCount; }

	UPROPERTY(BlueprintAssignable, Category = "DxWebSocket")
	FDxWebSocketStateChanged OnConnectionStateChanged;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FDTCoreSubscriptionLifecycleTest;
	friend class FDTCoreConnectionStateTest;
	friend class FDTCoreReceiptTimeoutTest;
#endif
	void BeginConnectionAttempt();
	void SetConnectionState(EDxWebSocketConnectionState NewState, const FString& Reason);
	void CompleteSubscriptionBatch();
	void HandleSubscribeTimeout(uint64 Generation);
	void ClearSubscribeTimeout();
	EDxWebSocketConnectionState ConnectionState = EDxWebSocketConnectionState::Disconnected;
	int32 LastSubscribeFailureCount = 0;
	FTimerHandle SubscribeTimeoutHandle;
	// 각 receipt의 완료 여부는 GameThread에서만 읽고 쓴다.
	TArray<TSharedPtr<bool>> BatchReceipts;
	bool bWantsConnection = false;
	bool bIsShuttingDown = false;
	uint64 ConnectionGeneration = 0;
	bool bSubscriptionBatchStarted = false;
	bool bConnectedBroadcast = false;
	UFUNCTION(Category = "DxWebSocket")
	void HandleOnConnected(const FString& ProtocolVersion, const FString& SessionId, const FString& ServerString);
	UFUNCTION(Category = "DxWebSocket")
	void HandleOnConnectionError(const FString& Error);
	UFUNCTION(Category = "DxWebSocket")
	void HandleOnError(const FString& Error);
	UFUNCTION(Category = "DxWebSocket")
	void HandleOnClosed(const FString&  Reason);

	UFUNCTION(Category = "DxWebSocket")
	void TryReconnect();

	/** 개별 토픽 구독 완료 시 호출 - 모든 구독이 완료되면 OnConnected를 Broadcast */
	UFUNCTION(Category = "DxWebSocket")
	void HandleSubscribeComplete(bool bSuccess, FString Error);
protected:

	// Variable
public:
	UPROPERTY(BlueprintReadOnly, Category = "DxWebSocket")
	FSTOMPSubscriptionEvent ReceivedMessageEvent;
	UPROPERTY(BlueprintReadOnly, Category = "DxWebSocket")
	FSTOMPRequestCompleted CompletedMessageEvent;
	UPROPERTY(BlueprintReadOnly, Category = "DxWebSocket")
	FSTOMPConnectedEvent OnConnected;
private:
	FTimerHandle ReconnectTimerHandle; // 타이머 핸들
	int32 RetryCount = 0;              // 현재 재시도 횟수

	const float InitialRetryDelay = 1.0f; // 초기 대기 시간 (초)
	const float MaxRetryDelay = 60.0f;    // 최대 대기 시간 (초)
	const float BackoffMultiplier = 2.0f; // 시간 증가 배수

	// 토픽별 구독 ID 관리 (Topic -> SubscriptionId)
	TMap<FString, FString> SubscriptionIds;

	// 토픽 → 라우팅 타입 매핑 (DTCoreSettings에서 읽어 Initialize()에서 구성)
	TMap<FString, ETopicRouteType> TopicRouteMap;

	// RouteType 수신 로그를 이미 남긴 토픽 (토픽별 첫 수신만 기록, GameThread 전용)
	TSet<FString> RouteLoggedTopics;

	// 구독 완료 카운팅 (모든 구독 완료 후 OnConnected Broadcast)
	int32 PendingSubscribeCount = 0;
	FString PendingProtocolVersion;
	FString PendingSessionId;
	FString PendingServerString;
protected:
	TSharedPtr<IStompClient> StompClient;
	TMap<FName, FString> LoginInfo;
};
