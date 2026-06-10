#include "Manager/DxLevelManagerBase.h"

#include "Core/DxGameStateBase.h"
#include "Core/DxWidgetSubsystem.h"

ADxLevelManagerBase::ADxLevelManagerBase()
{
	// 베이스는 Tick을 사용하지 않음. Tick이 필요한 파생 클래스는
	// SetActorTickEnabled(true)로 직접 활성화할 것
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void ADxLevelManagerBase::BeginPlay()
{
	Super::BeginPlay();

	// DxGameStateBase에 전달
	if (ADxGameStateBase* GameState = GetWorld()->GetGameState<ADxGameStateBase>())
	{
		GameState->CurrentViewMode = ViewMode;
	}

	// DxWidgetSubsystem에 UI 생성 요청
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDxWidgetSubsystem* WidgetSubsystem = GI->GetSubsystem<UDxWidgetSubsystem>())
		{
			WidgetSubsystem->SwitchUIMode(ViewMode);
		}
	}
}

void ADxLevelManagerBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ADxLevelManagerBase::SetupForLevel()
{
}

void ADxLevelManagerBase::CleanupForLevel()
{
}

