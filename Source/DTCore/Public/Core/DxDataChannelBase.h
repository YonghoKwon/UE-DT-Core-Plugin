#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/DxDataChannelTypes.h"
#include "Core/DxWebSocketSubsystem.h"
#include <atomic>
#include "DxDataChannelBase.generated.h"

class UGameInstance;

/**
 * 데이터 I/F 채널 기반 클래스. 기본 WebSocket 연결과 별개인 연결 하나를 나타낸다.
 * UDxWebSocketSubsystem::RegisterChannel 또는 DTCoreSettings.DataChannels로 등록하며 GameInstance 수명을 따른다.
 *
 * 스레드 규칙
 * - Connect / Disconnect / Subscribe / Unsubscribe / Publish 호출과 OnStateChanged broadcast는 GameThread이다.
 * - GetThreadPolicy() == GameThread인 채널은 수신·완료 콜백을 GameThread에서 호출한다.
 * - GetThreadPolicy() == Worker인 채널은 작업 스레드에서 처리하고, GameThread에는 상태와 집계만 전달한다.
 *   이 경우 Publish를 작업 스레드에서 부를 수 있는지는 파생 클래스가 문서로 밝힌다.
 * - 보호 훅 UpdateState / SetLastError / Record*는 어느 스레드에서 불러도 안전하다.
 */
UCLASS(Abstract, BlueprintType)
class DTCORE_API UDxDataChannelBase : public UObject
{
	GENERATED_BODY()

public:
	// 등록 시 한 번 호출된다. 직접 부르지 않는다.
	void InitializeChannel(FName InChannelName, const FDxDataChannelSettings& InSettings, UGameInstance* InGameInstance);
	// 등록 해제·GameInstance 종료 시 호출된다. 이후 연결·재연결·콜백을 하지 않는다.
	void ShutdownChannel();

	UFUNCTION(BlueprintCallable, Category = "DxDataChannel")
	virtual bool Connect() PURE_VIRTUAL(UDxDataChannelBase::Connect, return false;);
	UFUNCTION(BlueprintCallable, Category = "DxDataChannel")
	virtual void Disconnect() PURE_VIRTUAL(UDxDataChannelBase::Disconnect, );

	// Body를 UTF-8로 보낸다. 보내지 않으면 false이며 CompletionCallback은 호출되지 않는다.
	UFUNCTION(BlueprintCallable, Category = "DxDataChannel", meta = (AutoCreateRefTerm = "Headers"))
	bool PublishText(const FString& Destination, const FString& Body, const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback);
	UFUNCTION(BlueprintCallable, Category = "DxDataChannel", meta = (AutoCreateRefTerm = "Headers"))
	bool PublishBinary(const FString& Destination, const TArray<uint8>& Body, const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback);

	// 반환값은 채널이 관리하는 구독 키이며 재연결 후에도 유지된다. 실패 시 빈 문자열.
	UFUNCTION(BlueprintCallable, Category = "DxDataChannel")
	virtual FString Subscribe(const FString& Destination, const FSTOMPSubscriptionEvent& EventCallback, const FSTOMPRequestCompleted& CompletionCallback) PURE_VIRTUAL(UDxDataChannelBase::Subscribe, return FString(););
	UFUNCTION(BlueprintCallable, Category = "DxDataChannel")
	virtual void Unsubscribe(const FString& SubscriptionKey, const FSTOMPRequestCompleted& CompletionCallback) PURE_VIRTUAL(UDxDataChannelBase::Unsubscribe, );

	UFUNCTION(BlueprintPure, Category = "DxDataChannel")
	virtual FDxDataChannelStatus GetStatus() const;
	UFUNCTION(BlueprintPure, Category = "DxDataChannel")
	EDxDataChannelState GetState() const { return static_cast<EDxDataChannelState>(State.load()); }
	UFUNCTION(BlueprintPure, Category = "DxDataChannel")
	FName GetChannelName() const { return ChannelName; }
	UFUNCTION(BlueprintPure, Category = "DxDataChannel")
	virtual EDxDataChannelThreadPolicy GetThreadPolicy() const { return EDxDataChannelThreadPolicy::GameThread; }

	const FDxDataChannelSettings& GetChannelSettings() const { return ChannelSettings; }
	bool IsShutdown() const { return bShutdown.load(); }

	// 기본 채널 설정을 대신 쓰는 규칙(0·음수 = 기본값)을 적용한 재연결 대기 시간
	static float ComputeReconnectDelay(float InitialSeconds, float MaxSeconds, float Multiplier, int32 AttemptIndex);

	// GameThread에서만 broadcast한다.
	UPROPERTY(BlueprintAssignable, Category = "DxDataChannel")
	FDxDataChannelStateChanged OnStateChanged;

protected:
	// 등록 직후 GameThread에서 호출된다. 설정의 bAutoConnect 처리 전이다.
	virtual void OnChannelInitialized() {}
	// ShutdownChannel에서 GameThread로 호출된다. 연결·타이머를 정리한다.
	virtual void OnChannelShutdown() {}
	// PublishText / PublishBinary 공통 경로. 크기 검사·드롭 집계 후 실제 송신은 파생 클래스가 한다.
	virtual bool PublishBytes(const FString& Destination, const TArray<uint8>& Body, const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback) PURE_VIRTUAL(UDxDataChannelBase::PublishBytes, return false;);

	// 재연결 정책 조회 (채널 설정 우선, 없으면 기본 채널 설정)
	float GetReconnectDelaySeconds(int32 AttemptIndex) const;
	int32 GetMaxReconnectAttempts() const;
	// 대기 시간 후 Connect를 다시 부른다. 횟수를 넘기면 GaveUp으로 바꾸고 false.
	bool ScheduleReconnect(const FString& Reason);
	void CancelReconnect();
	void ResetReconnectAttempts() { ReconnectAttemptCount.store(0); }

	// 상태 갱신. 바뀐 경우에만 GameThread에서 OnStateChanged를 broadcast한다.
	void UpdateState(EDxDataChannelState NewState, const FString& Reason);
	void SetLastError(const FString& Error);
	void RecordPublished(int64 Count = 1) { PublishedCount.fetch_add(Count); }
	void RecordDropped(int64 Count = 1) { DroppedCount.fetch_add(Count); }
	void RecordBackpressure(int64 Count = 1) { BackpressureCount.fetch_add(Count); }
	// MaxMessageBytes를 넘으면 드롭으로 집계하고 false
	bool CheckMessageSize(int64 Bytes);

	// URL·인증 해석 (Runtime Override 키 → 채널 값 → 기본 채널 값)
	FString ResolveUrl() const;
	TMap<FName, FString> ResolveCredentials() const;

	UGameInstance* GetOwningGameInstance() const { return OwningGameInstance.Get(); }

private:
	void BroadcastState(EDxDataChannelState NewState, const FString& Reason);
	void HandleReconnectTimer();

	FName ChannelName;
	FDxDataChannelSettings ChannelSettings;
	TWeakObjectPtr<UGameInstance> OwningGameInstance;
	FTimerHandle ReconnectTimerHandle;

	std::atomic<uint8> State{static_cast<uint8>(EDxDataChannelState::Disconnected)};
	std::atomic<bool> bShutdown{false};
	std::atomic<int64> PublishedCount{0};
	std::atomic<int64> DroppedCount{0};
	std::atomic<int64> BackpressureCount{0};
	std::atomic<int32> ReconnectAttemptCount{0};
	mutable FCriticalSection LastErrorLock;
	FString LastError;
};
