#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DxObjectSubsystem.generated.h"

UCLASS()
class DTCORE_API UDxObjectSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	void RegisterObject(FName Category, const FString& Id, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	void UnregisterObject(FName Category, const FString& Id);

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	void ClearCategory(FName Category);

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	void ClearAllObjects();

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	void CompactInvalidObjects(FName Category);

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	void CompactAllInvalidObjects();

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	bool ContainsObject(FName Category, const FString& Id) const;

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	TArray<FString> GetObjectIds(FName Category) const;

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	AActor* FindObject(FName Category, const FString& Id) const;

	UFUNCTION(BlueprintCallable, Category = "DxObject")
	TArray<AActor*> GetAllObjects(FName Category) const;

	// 실제 살아있는 Actor 개수
	UFUNCTION(BlueprintCallable, Category = "DxObject")
	int32 GetObjectCount(FName Category) const;

	// Map에 등록되어 있는 전체 개수. Destroy된 Actor도 포함될 수 있음.
	UFUNCTION(BlueprintCallable, Category = "DxObject")
	int32 GetRegisteredObjectCount(FName Category) const;

	// 실제 유효한 Actor 개수. GetObjectCount와 같은 의미로 둬도 됨.
	UFUNCTION(BlueprintCallable, Category = "DxObject")
	int32 GetValidObjectCount(FName Category) const;

	template<typename T>
	T* FindObjectAs(FName Category, const FString& Id) const
	{
		return Cast<T>(FindObject(Category, Id));
	}

	// C++ 전용 스냅샷 조회가 필요할 때 사용합니다. Blueprint에서는 GetAllObjects/GetObjectIds를 우선 사용하세요.
	const TMap<FString, TObjectPtr<AActor>>* GetCategoryMap(FName Category) const;

private:
	TMap<FName, TMap<FString, TObjectPtr<AActor>>> RegisteredObjects;
};