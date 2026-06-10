#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DxDataSubsystem.generated.h"

class UTransactionCodeMessage;
class UApiMessage;
/**
 * 
 */
UCLASS()
class DTCORE_API UDxDataSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

	// Function
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate() && !bIsShuttingDown; }

	UFUNCTION(Category = "DxData")
	void EnqueueApiData(const FString& DataFromApi);
	UFUNCTION(Category = "DxData")
	void EnqueueWebSocketData(const FString& Data);

private:
	void ProcessApiQueue();
	void ProcessWebSocketQueue();

	// API 핸들러 조회 함수
	UApiMessage* GetOrLoadApiHandler(UClass* HandlerClass);
	UTransactionCodeMessage* GetOrLoadTransactionHandler(UClass* HandlerClass);
protected:

	// Variable
public:
private:
	UPROPERTY()
	TObjectPtr<UDataTable> ApiDataTable;
	UPROPERTY()
	TObjectPtr<UDataTable> WebSocketDataTable;

	TQueue<FString> ApiDataQueue;
	TQueue<FString> WebSocketDataQueue;

	UPROPERTY()
	TMap<FString, TObjectPtr<UApiMessage>> ApiMessageMap;
	UPROPERTY()
	TMap<FString, TObjectPtr<UTransactionCodeMessage>> TransactionCodeMessageMap;
	UPROPERTY()
	TMap<UClass*, UApiMessage*> ApiHandlerInstanceCache;
	UPROPERTY()
	TMap<UClass*, UTransactionCodeMessage*> WebSocketHandlerInstanceCache;


	// 디버그용
	// int32 TotalProcessedCount = 0; // 처리된 총 개수
	// 디버그용 전체 처리된 데이터 개수를 세는 카운터
	FThreadSafeCounter TotalProcessedCount;

	// 백그라운드 작업이 Tick마다 중복 생성되는 것을 막기 위한 플래그
	FThreadSafeBool bApiProcessing = false;
	FThreadSafeBool bWebSocketProcessing = false;

	// Deinitialize 이후 백그라운드 작업이 UObject/핸들러를 건드리지 않도록 보호
	FThreadSafeBool bIsShuttingDown = false;
protected:
	// TSharedPtr<TMap<FString, TSubclassOf<UApiMessage>>> CachedHandlerApiMessageMap;
	// TSharedPtr<TMap<FString, TSubclassOf<UTransactionCodeMessage>>> CachedHandlerTransactionCodeMessageMap;

	TSharedPtr<TMap<FString, UApiMessage*>> CachedHandlerApiMessageMap;

	TSharedPtr<TMap<FString, UTransactionCodeMessage*>> CachedHandlerTransactionCodeMessageMap;
};