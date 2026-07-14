#include "ActorComponent/StatusVisualizerCompBase.h"


// Sets default values for this component's properties
UStatusVisualizerCompBase::UStatusVisualizerCompBase()
{
	// 베이스는 Tick을 사용하지 않음. Tick이 필요한 파생 클래스는
	// SetComponentTickEnabled(true)로 직접 활성화할 것
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}


// Called when the game starts
void UStatusVisualizerCompBase::BeginPlay()
{
	Super::BeginPlay();
}

