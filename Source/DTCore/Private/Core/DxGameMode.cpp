#include "Core/DxGameMode.h"

#include "Core/DTCoreSettings.h"

ADxGameMode::ADxGameMode()
{
}

void ADxGameMode::BeginPlay()
{
    Super::BeginPlay();
    
    if (GEngine && GetDefault<UDTCoreSettings>()->bShowFpsOnBeginPlay)
    {
        GEngine->Exec(GetWorld(), TEXT("stat fps"));
    }
}
