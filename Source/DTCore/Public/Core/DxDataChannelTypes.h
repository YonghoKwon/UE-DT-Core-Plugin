#pragma once

#include "CoreMinimal.h"
#include "DxDataChannelTypes.generated.h"

class UDxDataChannelBase;

UENUM(BlueprintType)
enum class EDxDataChannelState : uint8
{
	Disconnected,
	Connecting,
	// 전송 연결이 열려 Publish가 가능한 상태
	Connected,
	Reconnecting,
	GaveUp
};

// 파생 채널이 콜백을 어느 스레드에서 처리하는지 나타낸다.
UENUM(BlueprintType)
enum class EDxDataChannelThreadPolicy : uint8
{
	// 수신·완료 콜백을 GameThread에서 호출한다.
	GameThread,
	// 대용량·고빈도: 작업 스레드에서 처리하고 GameThread에는 요약·상태만 전달한다.
	Worker
};

/**
 * DTCoreSettings의 DataChannels 항목 하나.
 * 0 또는 빈 값은 기본 채널(WebSocket*·Reconnect* 설정) 값을 따른다.
 * 비밀값은 설정에 직접 두지 않고 Runtime Override 키 이름만 둔다.
 */
USTRUCT(BlueprintType)
struct DTCORE_API FDxDataChannelSettings
{
	GENERATED_BODY()

	// 등록 이름. None과 "Default"는 사용할 수 없다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	FName ChannelName;

	// 비어 있으면 UDxStompChannel
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	TSoftClassPtr<UDxDataChannelBase> ChannelClass;

	// 비어 있으면 기본 채널 URL(WebSocketUrl)을 쓴다. 같은 서버에 별도 연결을 여는 경우이다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	FString Url;

	// Runtime Override(Game.ini [DTCoreRuntimeOverride])에서 URL을 읽을 키 이름. 값이 있으면 Url보다 우선한다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	FString UrlOverrideKey;

	// 인증값을 읽을 Runtime Override 키 이름. 비어 있고 bUseDefaultCredentials이면 기본 채널 인증값을 쓴다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	FString LoginOverrideKey;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	FString PasscodeOverrideKey;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	bool bUseDefaultCredentials = true;

	// 등록 시 바로 연결한다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel")
	bool bAutoConnect = true;

	// -1이면 MaxReconnectAttempts, 0이면 무제한
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel", meta = (ClampMin = "-1"))
	int32 MaxReconnectAttempts = -1;

	// 0이면 기본 채널의 Reconnect* 값
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel", meta = (ClampMin = "0"))
	float ReconnectInitialDelaySeconds = 0.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel", meta = (ClampMin = "0"))
	float ReconnectMaxDelaySeconds = 0.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel", meta = (ClampMin = "0"))
	float ReconnectBackoffMultiplier = 0.0f;

	// 0이면 제한 없음. 넘는 메시지는 보내지 않고 드롭으로 집계한다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel", meta = (ClampMin = "0"))
	int64 MaxMessageBytes = 0;

	// 0이면 receipt 시간 초과를 두지 않는다.
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DxDataChannel", meta = (ClampMin = "0"))
	float ReceiptTimeoutSeconds = 0.0f;
};

// 채널 상태 snapshot. 항목 간 동시 시점 일관성은 보장하지 않는다.
USTRUCT(BlueprintType)
struct DTCORE_API FDxDataChannelStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DxDataChannel")
	FName ChannelName;

	UPROPERTY(BlueprintReadOnly, Category = "DxDataChannel")
	EDxDataChannelState State = EDxDataChannelState::Disconnected;

	UPROPERTY(BlueprintReadOnly, Category = "DxDataChannel")
	FString LastError;

	// 전송 계층에 넘긴 메시지 수
	UPROPERTY(BlueprintReadOnly, Category = "DxDataChannel")
	int64 PublishedCount = 0;

	// 연결 없음·크기 초과 등으로 보내지 못한 수
	UPROPERTY(BlueprintReadOnly, Category = "DxDataChannel")
	int64 DroppedCount = 0;

	// 대기열·흐름 제어로 밀리거나 버린 수 (파생 채널이 집계)
	UPROPERTY(BlueprintReadOnly, Category = "DxDataChannel")
	int64 BackpressureCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DxDataChannel")
	int32 ReconnectAttemptCount = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDxDataChannelStateChanged, FName, ChannelName, EDxDataChannelState, State, FString, Reason);
