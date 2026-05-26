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
	UDxApiSubsystem();
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
	void InternalOnResponseReceived(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request, TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response, bool bWasSuccessful, FDxApiCallback Callback);
	FString GetServerUrl(EApiType ApiType) const;
	FString GetHttpStr(EApiMethod ApiMethod) const;
protected:

	// Variable
public:
private:
	FHttpModule* HttpModule = nullptr;

	// 현재 통신 중인 HTTP 요청들을 추적하는 배열 추가
	TArray<TSharedRef<IHttpRequest, ESPMode::ThreadSafe>> ActiveHttpRequests;

	UPROPERTY()
	TObjectPtr<UDataTable> ApiDataTable;
protected:
};