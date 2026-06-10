#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Api/ApiStruct.h"
#include "DxApiSubsystem.generated.h"

class FHttpModule;
class IHttpRequest;
class IHttpResponse;
class UDataTable;

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FDxApiCallback, bool, bSuccess, int32, ResponseCode, const FString&, Content);

/**
 *
 */
UCLASS()
class DTCORE_API UDxApiSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	// Function
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "DxApi")
	void DxHttpCall(const FString& FullUrl, const FString& Verb, const FString& ContentString, const TMap<FString, FString>& Headers, FDxApiCallback Callback);
	UFUNCTION(BlueprintCallable, Category = "DxApi")
	void DxRequestApi(const FName& RowName, FDxApiCallback Callback);
	UFUNCTION(BlueprintCallable, Category = "DxApi")
	void DxRequestApiWithParameter(const FName& RowName, FDxApiCallback Callback, const TArray<FString>& Parameters);

	UFUNCTION(BlueprintPure, Category = "DxApi")
	bool IsApiDataTableLoaded() const { return ApiDataTable != nullptr; }
private:
	// 재시도를 위해 요청 정보를 함께 들고 다니는 컨텍스트
	struct FDxHttpRequestContext
	{
		FString FullUrl;
		FString Verb;
		FString ContentString;
		TMap<FString, FString> Headers;
		FDxApiCallback Callback;
		int32 AttemptIndex = 0;
	};

	void InternalHttpCall(FDxHttpRequestContext Context);
	void InternalOnResponseReceived(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request, TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response, bool bWasSuccessful, FDxHttpRequestContext Context);
	FString GetServerUrl(EApiType ApiType) const;
	FString GetHttpStr(EApiMethod ApiMethod) const;
protected:

	// Variable
public:
private:
	FHttpModule* HttpModule = nullptr;

	// 현재 통신 중인 HTTP 요청들을 추적하는 배열 추가
	TArray<TSharedRef<IHttpRequest, ESPMode::ThreadSafe>> ActiveHttpRequests;

	// 재시도 대기 중인 타이머들 (Deinitialize 시 일괄 해제)
	TArray<FTimerHandle> PendingRetryTimers;

	// 연결 실패/5xx 응답에 한해 지수 백오프로 재시도
	static constexpr int32 MaxHttpAttempts = 3;
	static constexpr float InitialHttpRetryDelay = 1.0f;

	UPROPERTY()
	TObjectPtr<UDataTable> ApiDataTable;
protected:
};