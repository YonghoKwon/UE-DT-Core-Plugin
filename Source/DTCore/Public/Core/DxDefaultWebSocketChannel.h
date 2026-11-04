#pragma once

#include "CoreMinimal.h"
#include "Core/DxDataChannelBase.h"
#include "DxDefaultWebSocketChannel.generated.h"

/**
 * 기존 기본 WebSocket 연결(UDxWebSocketSubsystem)을 채널 목록에서 보이게 하는 얇은 adapter.
 * FindChannel("Default")로 처음 찾을 때 만들어진다. 기존 연결·구독·재연결 동작은 바꾸지 않고 기존 공개 함수로 전달만 한다.
 * - 상태: Subscribing·Ready는 Connected로 보인다. Ready 여부는 UDxWebSocketSubsystem::GetConnectionState로 확인한다.
 * - Subscribe 반환값은 STOMP 구독 ID이며 재연결 후에는 유지되지 않는다(기존 Subscribe와 같다).
 * - 등록 해제·종료 시 기본 연결을 끊지 않는다.
 */
UCLASS(NotBlueprintable)
class DTCORE_API UDxDefaultWebSocketChannel : public UDxDataChannelBase
{
	GENERATED_BODY()

public:
	void SetOwner(UDxWebSocketSubsystem* InOwner);

	virtual bool Connect() override;
	virtual void Disconnect() override;
	virtual FString Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback, const FSTOMPRequestCompleted& CompletionCallback) override;
	virtual void Unsubscribe(const FString& SubscriptionKey, const FSTOMPRequestCompleted& CompletionCallback) override;
	virtual FDxDataChannelStatus GetStatus() const override;

	static EDxDataChannelState MapState(EDxWebSocketConnectionState State);

protected:
	virtual void OnChannelShutdown() override;
	virtual bool PublishBytes(const FString& Destination, const TArray<uint8>& Body, const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback) override;

private:
	UFUNCTION()
	void HandleOwnerStateChanged(EDxWebSocketConnectionState NewState, FString Reason);

	TWeakObjectPtr<UDxWebSocketSubsystem> Owner;
};
