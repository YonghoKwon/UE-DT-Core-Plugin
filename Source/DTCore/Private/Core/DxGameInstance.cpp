#include "Core/DxGameInstance.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h" // 디렉토리 생성을 위해 필요
#include "GameFramework/GameUserSettings.h"
#include "Core/DTCoreSettings.h"

UDxGameInstance* UDxGameInstance::Instance = nullptr;

void UDxGameInstance::Init()
{
	Super::Init();

	UGameUserSettings* UserSettings = UGameUserSettings::GetGameUserSettings();
	if (UserSettings)
	{
		const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
		const float FrameLimit = Settings ? FMath::Max(0.0f, Settings->FrameRateLimit) : 60.0f;
		// Frame 제한 설정 (.ini의 FrameRateLimit 값 사용)		
		UserSettings->SetFrameRateLimit(FrameLimit);
		UserSettings->ApplySettings(false);
	}
}

void UDxGameInstance::Shutdown()
{
	Super::Shutdown();
}
