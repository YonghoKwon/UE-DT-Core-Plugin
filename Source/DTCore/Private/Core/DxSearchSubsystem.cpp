#include "Core/DxSearchSubsystem.h"
#include "DTCore.h"

void UDxSearchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	DX_LOG(GetWorld(), TEXT("DxSearchSubsystem initialized."));
}

void UDxSearchSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UDxSearchSubsystem::PerformSearch(uint8 InCategoryValue, const FString& SearchText)
{
	const FString TrimmedSearchText = SearchText.TrimStartAndEnd();

	if (TrimmedSearchText.IsEmpty())
	{
		DX_LOG(GetWorld(), TEXT("[DxSearchSubsystem] Search text is empty."));
		OnSearchCompleted.Broadcast(false, TArray<FSearchResult>(), TEXT("Search text is empty."));
		return;
	}

	TArray<FSearchResult> Results;
	SearchByCategory(InCategoryValue, TrimmedSearchText, Results);

	OnSearchCompleted.Broadcast(true, Results, TEXT(""));
}

void UDxSearchSubsystem::PerformSearchByCategory(FName Category, const FString& SearchText, const FDxSearchOptions& Options)
{
	if (Category.IsNone())
	{
		DX_LOG(GetWorld(), TEXT("[DxSearchSubsystem] Search category is none."));
		OnSearchCompleted.Broadcast(false, TArray<FSearchResult>(), TEXT("Search category is none."));
		return;
	}

	const FString TrimmedSearchText = SearchText.TrimStartAndEnd();

	if (TrimmedSearchText.IsEmpty())
	{
		DX_LOG(GetWorld(), TEXT("[DxSearchSubsystem] Search text is empty."));
		OnSearchCompleted.Broadcast(false, TArray<FSearchResult>(), TEXT("Search text is empty."));
		return;
	}

	TArray<FSearchResult> Results;
	SearchByCategoryName(Category, TrimmedSearchText, Options, Results);

	if (Options.MaxResults > 0 && Results.Num() > Options.MaxResults)
	{
		Results.SetNum(Options.MaxResults);
	}

	OnSearchCompleted.Broadcast(true, Results, TEXT(""));
}

void UDxSearchSubsystem::SearchByCategoryName(FName Category, const FString& SearchText, const FDxSearchOptions& Options, TArray<FSearchResult>& OutResults)
{
	// 프로젝트별 Subsystem에서 FName 기반 검색 규칙을 override해서 구현합니다.
	DX_LOG(GetWorld(), TEXT("[DxSearchSubsystem] FName category search override required. Category=%s"), *Category.ToString());
}