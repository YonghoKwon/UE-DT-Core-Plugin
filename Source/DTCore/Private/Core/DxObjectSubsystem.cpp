#include "Core/DxObjectSubsystem.h"
#include "DTCore.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UDxObjectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FWorldDelegates::OnWorldCleanup.AddUObject(this,&UDxObjectSubsystem::HandleWorldCleanup);
}

void UDxObjectSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	ClearAllObjects();

	Super::Deinitialize();
}

void UDxObjectSubsystem::RegisterObject(FName Category, const FString& Id, AActor* Actor)
{
	// RegisteredObjects는 동기화 없이 사용하므로 게임 스레드 전용
	// (ParseToStruct 등 백그라운드 컨텍스트에서 호출 금지)
	ensureMsgf(IsInGameThread(), TEXT("[DxObjectSubsystem] RegisterObject must be called on the game thread"));

	if (Category.IsNone())
	{
		DX_LOG(GetWorld(), TEXT("[DxObjectSubsystem] RegisterObject failed. Category is none. Id=%s"), *Id);
		return;
	}

	if (Id.IsEmpty())
	{
		DX_LOG(GetWorld(), TEXT("[DxObjectSubsystem] RegisterObject failed. Id is empty. Category=%s"), *Category.ToString());
		return;
	}

	if (!IsValid(Actor))
	{
		DX_LOG(GetWorld(), TEXT("[DxObjectSubsystem] RegisterObject failed. Actor is invalid. Category=%s, Id=%s"),
			*Category.ToString(), *Id);
		return;
	}

	TMap<FString, TObjectPtr<AActor>>& CategoryMap = RegisteredObjects.FindOrAdd(Category);

	if (CategoryMap.Contains(Id))
	{
		DX_LOG(GetWorld(), TEXT("[DxObjectSubsystem] Object already registered. Overwriting. Category=%s, Id=%s"),
			*Category.ToString(), *Id);
	}

	AActor* Previous=CategoryMap.FindRef(Id).Get();
	CategoryMap.Add(Id, Actor);
	Actor->OnDestroyed.AddUniqueDynamic(this,&UDxObjectSubsystem::HandleRegisteredActorDestroyed);
	if (Previous && Previous!=Actor) ReleaseActorIfUnused(Previous);
}

void UDxObjectSubsystem::UnregisterObject(FName Category, const FString& Id)
{
	ensureMsgf(IsInGameThread(), TEXT("[DxObjectSubsystem] UnregisterObject must be called on the game thread"));

	if (TMap<FString, TObjectPtr<AActor>>* CategoryMap = RegisteredObjects.Find(Category))
	{
		AActor* Removed=CategoryMap->FindRef(Id).Get();
		CategoryMap->Remove(Id);

		if (CategoryMap->Num() == 0)
		{
			RegisteredObjects.Remove(Category);
		}
		ReleaseActorIfUnused(Removed);
	}
}

void UDxObjectSubsystem::ClearCategory(FName Category)
{
	TArray<TObjectPtr<AActor>> Actors;
	if (const auto* Map=RegisteredObjects.Find(Category)) Map->GenerateValueArray(Actors);
	RegisteredObjects.Remove(Category);
	for (auto Actor:Actors) ReleaseActorIfUnused(Actor.Get());
}

void UDxObjectSubsystem::ClearAllObjects()
{
	for (const auto& Category:RegisteredObjects)
		for (const auto& Entry:Category.Value)
			if (IsValid(Entry.Value.Get())) Entry.Value->OnDestroyed.RemoveDynamic(this,&UDxObjectSubsystem::HandleRegisteredActorDestroyed);
	RegisteredObjects.Empty();
}

void UDxObjectSubsystem::AddReferencedObjects(UObject* InThis,FReferenceCollector& Collector)
{
	auto* Self=CastChecked<UDxObjectSubsystem>(InThis);
	for (auto& Category:Self->RegisteredObjects)
		for (auto& Entry:Category.Value) Collector.AddReferencedObject(Entry.Value,Self);
	Super::AddReferencedObjects(InThis,Collector);
}

void UDxObjectSubsystem::ReleaseActorIfUnused(AActor* Actor)
{
	if (!IsValid(Actor)) return;
	for (const auto& Category:RegisteredObjects)
		for (const auto& Entry:Category.Value) if (Entry.Value.Get()==Actor) return;
	Actor->OnDestroyed.RemoveDynamic(this,&UDxObjectSubsystem::HandleRegisteredActorDestroyed);
}

void UDxObjectSubsystem::HandleRegisteredActorDestroyed(AActor* Actor)
{
	for (auto Category=RegisteredObjects.CreateIterator();Category;++Category)
	{
		for (auto Entry=Category.Value().CreateIterator();Entry;++Entry)
			if (Entry.Value().Get()==Actor) Entry.RemoveCurrent();
		if (Category.Value().IsEmpty()) Category.RemoveCurrent();
	}
}

void UDxObjectSubsystem::HandleWorldCleanup(UWorld* World,bool,bool)
{
	TArray<TPair<FName,FString>> Remove;
	for (const auto& Category:RegisteredObjects)
		for (const auto& Entry:Category.Value)
			if (IsValid(Entry.Value.Get()) && Entry.Value->GetWorld()==World) Remove.Emplace(Category.Key,Entry.Key);
	for (const auto& Entry:Remove) UnregisterObject(Entry.Key,Entry.Value);
}

void UDxObjectSubsystem::CompactInvalidObjects(FName Category)
{
	TMap<FString, TObjectPtr<AActor>>* CategoryMap = RegisteredObjects.Find(Category);
	if (!CategoryMap)
	{
		return;
	}

	TArray<FString> InvalidIds;
	for (const auto& Pair : *CategoryMap)
	{
		if (!IsValid(Pair.Value.Get()))
		{
			InvalidIds.Add(Pair.Key);
		}
	}

	for (const FString& InvalidId : InvalidIds)
	{
		CategoryMap->Remove(InvalidId);
	}

	if (CategoryMap->Num() == 0)
	{
		RegisteredObjects.Remove(Category);
	}
}

void UDxObjectSubsystem::CompactAllInvalidObjects()
{
	TArray<FName> Categories;
	RegisteredObjects.GetKeys(Categories);

	for (const FName& Category : Categories)
	{
		CompactInvalidObjects(Category);
	}
}

bool UDxObjectSubsystem::ContainsObject(FName Category, const FString& Id) const
{
	return FindObject(Category, Id) != nullptr;
}

TArray<FString> UDxObjectSubsystem::GetObjectIds(FName Category) const
{
	TArray<FString> Result;

	const TMap<FString, TObjectPtr<AActor>>* CategoryMap = RegisteredObjects.Find(Category);
	if (!CategoryMap)
	{
		return Result;
	}

	for (const auto& Pair : *CategoryMap)
	{
		if (IsValid(Pair.Value.Get()))
		{
			Result.Add(Pair.Key);
		}
	}

	return Result;
}

AActor* UDxObjectSubsystem::FindObject(FName Category, const FString& Id) const
{
	const TMap<FString, TObjectPtr<AActor>>* CategoryMap = RegisteredObjects.Find(Category);
	if (!CategoryMap)
	{
		return nullptr;
	}

	const TObjectPtr<AActor>* Found = CategoryMap->Find(Id);
	if (!Found)
	{
		return nullptr;
	}

	AActor* Actor = Found->Get();
	return IsValid(Actor) ? Actor : nullptr;
}

TArray<AActor*> UDxObjectSubsystem::GetAllObjects(FName Category) const
{
	TArray<AActor*> Result;

	const TMap<FString, TObjectPtr<AActor>>* CategoryMap = RegisteredObjects.Find(Category);
	if (!CategoryMap)
	{
		return Result;
	}

	for (const auto& Pair : *CategoryMap)
	{
		AActor* Actor = Pair.Value.Get();
		if (IsValid(Actor))
		{
			Result.Add(Actor);
		}
	}

	return Result;
}

int32 UDxObjectSubsystem::GetObjectCount(FName Category) const
{
	return GetValidObjectCount(Category);
}

int32 UDxObjectSubsystem::GetRegisteredObjectCount(FName Category) const
{
	const TMap<FString, TObjectPtr<AActor>>* CategoryMap = RegisteredObjects.Find(Category);
	return CategoryMap ? CategoryMap->Num() : 0;
}

int32 UDxObjectSubsystem::GetValidObjectCount(FName Category) const
{
	const TMap<FString, TObjectPtr<AActor>>* CategoryMap = RegisteredObjects.Find(Category);
	if (!CategoryMap)
	{
		return 0;
	}

	int32 Count = 0;
	for (const auto& Pair : *CategoryMap)
	{
		if (IsValid(Pair.Value.Get()))
		{
			++Count;
		}
	}

	return Count;
}

const TMap<FString, TObjectPtr<AActor>>* UDxObjectSubsystem::GetCategoryMap(FName Category) const
{
	return RegisteredObjects.Find(Category);
}
