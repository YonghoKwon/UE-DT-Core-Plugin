#include "Core/DxGameMode.h"

ADxGameMode::ADxGameMode()
{
}

void ADxGameMode::BeginPlay()
{
    Super::BeginPlay();
    
    if (GEngine)
    {
        GEngine->Exec(GetWorld(), TEXT("stat fps"));
    }
}
