#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StatusVisualizerCompBase.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class DTCORE_API UStatusVisualizerCompBase : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UStatusVisualizerCompBase();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// Function
public:
private:
protected:

	// Variable
public:
private:
protected:
};
