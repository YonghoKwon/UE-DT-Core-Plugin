#include "Core/DxDataChannelBase.h"

#include "DTCore.h"
#include "Async/Async.h"
#include "Core/DTCoreRuntimeConfig.h"
#include "Core/DTCoreSettings.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

void UDxDataChannelBase::InitializeChannel(FName InChannelName, const FDxDataChannelSettings& InSettings, UGameInstance* InGameInstance)
{
	check(IsInGameThread());
	ChannelName = InChannelName;
	ChannelSettings = InSettings;
	ChannelSettings.ChannelName = InChannelName;
	OwningGameInstance = InGameInstance;
	bShutdown.store(false);
	OnChannelInitialized();
}

void UDxDataChannelBase::ShutdownChannel()
{
	check(IsInGameThread());
	if (bShutdown.exchange(true)) return;
	CancelReconnect();
	OnChannelShutdown();
	State.store(static_cast<uint8>(EDxDataChannelState::Disconnected));
}

bool UDxDataChannelBase::PublishText(const FString& Destination, const FString& Body,
	const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback)
{
	FTCHARToUTF8 Convert(*Body);
	TArray<uint8> Encoded;
	Encoded.Append(reinterpret_cast<const uint8*>(Convert.Get()), Convert.Length());
	return PublishBinary(Destination, Encoded, Headers, CompletionCallback);
}

bool UDxDataChannelBase::PublishBinary(const FString& Destination, const TArray<uint8>& Body,
	const TMap<FName, FString>& Headers, const FSTOMPRequestCompleted& CompletionCallback)
{
	if (Destination.IsEmpty())
	{
		return false;
	}
	if (bShutdown.load())
	{
		RecordDropped();
		return false;
	}
	if (!CheckMessageSize(Body.Num()))
	{
		return false;
	}
	return PublishBytes(Destination, Body, Headers, CompletionCallback);
}

FDxDataChannelStatus UDxDataChannelBase::GetStatus() const
{
	FDxDataChannelStatus Status;
	Status.ChannelName = ChannelName;
	Status.State = GetState();
	{
		FScopeLock Lock(&LastErrorLock);
		Status.LastError = LastError;
	}
	Status.PublishedCount = PublishedCount.load();
	Status.DroppedCount = DroppedCount.load();
	Status.BackpressureCount = BackpressureCount.load();
	Status.ReconnectAttemptCount = ReconnectAttemptCount.load();
	return Status;
}

float UDxDataChannelBase::ComputeReconnectDelay(float InitialSeconds, float MaxSeconds, float Multiplier, int32 AttemptIndex)
{
	// 기본 채널 TryReconnect와 같은 하한과 계산식이다.
	const float Initial = FMath::Max(0.1f, InitialSeconds);
	const float Max = FMath::Max(Initial, MaxSeconds);
	const float Backoff = FMath::Max(1.0f, Multiplier);
	const float Delay = Initial * FMath::Pow(Backoff, static_cast<float>(FMath::Max(0, AttemptIndex)));
	return FMath::Min(Delay, Max);
}

float UDxDataChannelBase::GetReconnectDelaySeconds(int32 AttemptIndex) const
{
	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	const float Initial = ChannelSettings.ReconnectInitialDelaySeconds > 0.0f ? ChannelSettings.ReconnectInitialDelaySeconds : (Settings ? Settings->ReconnectInitialDelaySeconds : 1.0f);
	const float Max = ChannelSettings.ReconnectMaxDelaySeconds > 0.0f ? ChannelSettings.ReconnectMaxDelaySeconds : (Settings ? Settings->ReconnectMaxDelaySeconds : 60.0f);
	const float Multiplier = ChannelSettings.ReconnectBackoffMultiplier > 0.0f ? ChannelSettings.ReconnectBackoffMultiplier : (Settings ? Settings->ReconnectBackoffMultiplier : 2.0f);
	return ComputeReconnectDelay(Initial, Max, Multiplier, AttemptIndex);
}

int32 UDxDataChannelBase::GetMaxReconnectAttempts() const
{
	if (ChannelSettings.MaxReconnectAttempts >= 0)
	{
		return ChannelSettings.MaxReconnectAttempts;
	}
	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	return Settings ? Settings->MaxReconnectAttempts : 10;
}

bool UDxDataChannelBase::ScheduleReconnect(const FString& Reason)
{
	check(IsInGameThread());
	if (bShutdown.load()) return false;
	UGameInstance* GI = OwningGameInstance.Get();
	if (!GI) return false;
	if (GI->GetTimerManager().IsTimerActive(ReconnectTimerHandle)) return true;

	const int32 Attempt = ReconnectAttemptCount.load();
	const int32 MaxAttempts = GetMaxReconnectAttempts();
	if (MaxAttempts > 0 && Attempt >= MaxAttempts)
	{
		UpdateState(EDxDataChannelState::GaveUp, TEXT("Maximum reconnect attempts reached"));
		return false;
	}

	const float Delay = GetReconnectDelaySeconds(Attempt);
	ReconnectAttemptCount.store(Attempt + 1);
	DX_LOG(GI->GetWorld(), TEXT("DataChannel[%s]: %.1f초 후 재연결 (Attempt: %d, Reason: %s)"), *ChannelName.ToString(), Delay, Attempt + 1, *Reason);
	GI->GetTimerManager().SetTimer(ReconnectTimerHandle, this, &UDxDataChannelBase::HandleReconnectTimer, Delay, false);
	UpdateState(EDxDataChannelState::Reconnecting, Reason);
	return true;
}

void UDxDataChannelBase::CancelReconnect()
{
	if (UGameInstance* GI = OwningGameInstance.Get())
	{
		GI->GetTimerManager().ClearTimer(ReconnectTimerHandle);
	}
}

void UDxDataChannelBase::HandleReconnectTimer()
{
	if (bShutdown.load()) return;
	Connect();
}

void UDxDataChannelBase::UpdateState(EDxDataChannelState NewState, const FString& Reason)
{
	if (bShutdown.load()) return;
	const uint8 Previous = State.exchange(static_cast<uint8>(NewState));
	if (Previous == static_cast<uint8>(NewState)) return;

	if (IsInGameThread())
	{
		BroadcastState(NewState, Reason);
		return;
	}
	TWeakObjectPtr<UDxDataChannelBase> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, NewState, Reason]()
	{
		if (UDxDataChannelBase* Self = WeakThis.Get())
		{
			Self->BroadcastState(NewState, Reason);
		}
	});
}

void UDxDataChannelBase::BroadcastState(EDxDataChannelState NewState, const FString& Reason)
{
	if (bShutdown.load()) return;
	OnStateChanged.Broadcast(ChannelName, NewState, Reason);
}

void UDxDataChannelBase::SetLastError(const FString& Error)
{
	FScopeLock Lock(&LastErrorLock);
	LastError = Error;
}

bool UDxDataChannelBase::CheckMessageSize(int64 Bytes)
{
	if (ChannelSettings.MaxMessageBytes > 0 && Bytes > ChannelSettings.MaxMessageBytes)
	{
		RecordDropped();
		SetLastError(FString::Printf(TEXT("Message too large: %lld > %lld bytes"), Bytes, ChannelSettings.MaxMessageBytes));
		return false;
	}
	return true;
}

FString UDxDataChannelBase::ResolveUrl() const
{
	FString Value;
	if (!ChannelSettings.UrlOverrideKey.IsEmpty() && DTCoreRuntimeConfig::TryReadRuntimeOverride(*ChannelSettings.UrlOverrideKey, Value))
	{
		return Value;
	}
	if (!ChannelSettings.Url.IsEmpty())
	{
		return ChannelSettings.Url;
	}
	// 기본 채널과 같은 해석 순서이다.
	if (DTCoreRuntimeConfig::TryReadRuntimeOverride(TEXT("WebSocketUrl"), Value))
	{
		return Value;
	}
	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	return Settings && !Settings->WebSocketUrl.IsEmpty() ? Settings->WebSocketUrl : TEXT("ws://localhost:31000");
}

TMap<FName, FString> UDxDataChannelBase::ResolveCredentials() const
{
	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	auto Resolve = [Settings, this](const FString& ChannelKey, const TCHAR* DefaultKey, const FString& DefaultValue)
	{
		FString Value;
		if (!ChannelKey.IsEmpty())
		{
			DTCoreRuntimeConfig::TryReadRuntimeOverride(*ChannelKey, Value);
			return Value;
		}
		if (!ChannelSettings.bUseDefaultCredentials)
		{
			return FString();
		}
		return DTCoreRuntimeConfig::TryReadRuntimeOverride(DefaultKey, Value) ? Value : DefaultValue;
	};

	TMap<FName, FString> Credentials;
	const FString Login = Resolve(ChannelSettings.LoginOverrideKey, TEXT("WebSocketLogin"), Settings ? Settings->WebSocketLogin : FString());
	const FString Passcode = Resolve(ChannelSettings.PasscodeOverrideKey, TEXT("WebSocketPasscode"), Settings ? Settings->WebSocketPasscode : FString());
	if (!Login.IsEmpty())
	{
		Credentials.Add(TEXT("login"), Login);
	}
	if (!Passcode.IsEmpty())
	{
		Credentials.Add(TEXT("passcode"), Passcode);
	}
	return Credentials;
}
