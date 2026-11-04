#pragma once

#include "CoreMinimal.h"
#include "Core/DxDataChannelBase.h"
#include "DxStompChannel.generated.h"

class IStompClient;

/**
 * 기본 연결과 별도 인스턴스로 STOMP 서버에 붙는 채널. 같은 서버(URL 비움)나 다른 서버 모두 쓸 수 있다.
 * 수신·완료 콜백은 GameThread에서 호출한다. 구독은 재연결마다 다시 요청하며 구독 키는 유지된다.
 *
 * 대용량 파생 클래스는 아래 단계만 바꿔 끼운다.
 * - CreateStompClient: 연결 객체 생성
 * - SendFrame: 실제 송신 (예: 전용 작업 스레드·소켓)
 * - OnFrameReceived: 수신 처리 스레드 (기본은 GameThread로 전달)
 */
UCLASS(BlueprintType)
class DTCORE_API UDxStompChannel : public UDxDataChannelBase
{
	GENERATED_BODY()

public:
	virtual bool Connect() override;
	virtual void Disconnect() override;
	virtual FString Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback, const FSTOMPRequestCompleted& CompletionCallback) override;
	virtual void Unsubscribe(const FString& SubscriptionKey, const FSTOMPRequestCompleted& CompletionCallback) override;

protected:
	using FReceiptHandler = TFunction<void(bool /*bSuccess*/, const FString& /*Error*/)>;

	virtual void OnChannelShutdown() override;
	virtual bool PublishBytes(const FString& Destination, const TArray<uint8>& Body, const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback) override;

	// 연결 객체 생성. 기본은 엔진 Stomp 모듈
	virtual TSharedPtr<IStompClient> CreateStompClient(const FString& Url);
	// 실제 송신. OnReceipt가 있으면 receipt를 요청하며 어느 스레드에서 불러도 된다. 보내지 못하면 false.
	virtual bool SendFrame(const FString& Destination, const TArray<uint8>& Body, const TMap<FName, FString>& Headers, FReceiptHandler OnReceipt);
	// STOMP 수신 스레드에서 호출된다. 기본은 GameThread로 넘겨 IsCurrentGeneration 확인 후 EventCallback을 부른다.
	virtual void OnFrameReceived(FWebSocketMessage&& Message, const FSTOMPSubscriptionEvent& EventCallback, uint64 FrameGeneration);

	// GameThread에서 호출된다.
	virtual void HandleTransportConnected();
	virtual void HandleTransportClosed(const FString& Reason);
	virtual void HandleTransportError(const FString& Error);

	// 재연결·Disconnect 뒤에 도착한 이전 연결의 결과인지 확인한다. GameThread 전용
	bool IsCurrentGeneration(uint64 FrameGeneration) const { return !IsShutdown() && bWantsConnection && Generation == FrameGeneration; }
	uint64 GetGeneration() const { return Generation; }
	TSharedPtr<IStompClient> GetStompClient() const { return StompClient; }

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FDTCoreStompChannelTest;
#endif
	struct FChannelSubscription
	{
		FString Destination;
		FSTOMPSubscriptionEvent EventCallback;
		FSTOMPRequestCompleted CompletionCallback;
		FString StompId;
		bool bCompletionReported = false;
	};

	void ReleaseClient();
	void IssueSubscription(const FString& Key);

	TSharedPtr<IStompClient> StompClient;
	uint64 Generation = 0;
	bool bWantsConnection = false;
	TMap<FString, FChannelSubscription> Subscriptions;
};
