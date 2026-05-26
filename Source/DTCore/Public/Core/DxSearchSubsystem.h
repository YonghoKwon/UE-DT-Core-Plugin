#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DxSearchSubsystem.generated.h"

USTRUCT(BlueprintType)
struct DTCORE_API FSearchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Search")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "Search")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Search")
	uint8 CategoryValue = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Search")
	FName Category = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Search")
	FString DisplayText;

	UPROPERTY(BlueprintReadOnly, Category = "Search")
	FString AdditionalInfo;

	UPROPERTY(BlueprintReadOnly, Category = "Search")
	TObjectPtr<AActor> Actor = nullptr;
};

USTRUCT(BlueprintType)
struct DTCORE_API FDxSearchOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	int32 MaxResults = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	bool bCaseSensitive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	bool bPartialMatch = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnSearchCompleted,
	bool, bSuccess,
	const TArray<FSearchResult>&, Results,
	const FString&, ErrorMessage
);

UCLASS(Abstract)
class DTCORE_API UDxSearchSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Search", meta=(DeprecatedFunction, DeprecationMessage="Use PerformSearchByCategory instead."))
	void PerformSearch(uint8 InCategoryValue, const FString& SearchText);

	UFUNCTION(BlueprintCallable, Category = "Search")
	void PerformSearchByCategory(FName Category, const FString& SearchText, const FDxSearchOptions& Options);

	UPROPERTY(BlueprintAssignable, Category = "Search")
	FOnSearchCompleted OnSearchCompleted;

protected:
	virtual void SearchByCategory(
		uint8 InCategoryValue,
		const FString& SearchText,
		TArray<FSearchResult>& OutResults
	) PURE_VIRTUAL(UDxSearchSubsystem::SearchByCategory, );

	virtual void SearchByCategoryName(
		FName Category,
		const FString& SearchText,
		const FDxSearchOptions& Options,
		TArray<FSearchResult>& OutResults
	);
};