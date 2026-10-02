#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DxWebSocketSubsystem.generated.h"

class IStompClient;

/** 토픽 수신 데이터를 어느 큐로 라우팅할지 결정하는 타입 */
UENUM()
enum class ETopicRouteType : uint8
{
	WebSocket,  // → DxDataSubsystem::EnqueueWebSocketData (TC 전문)
	Api,        // → DxDataSubsystem::EnqueueApiData (API 응답)
};

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
private:
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
	
	// 구독 완료 카운팅 (모든 구독 완료 후 OnConnected Broadcast)
	int32 PendingSubscribeCount = 0;
	FString PendingProtocolVersion;
	FString PendingSessionId;
	FString PendingServerString;
protected:
	TSharedPtr<IStompClient> StompClient;
	TMap<FName, FString> LoginInfo;
};
