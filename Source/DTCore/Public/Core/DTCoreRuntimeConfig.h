#pragma once

#include "CoreMinimal.h"

namespace DTCoreRuntimeConfig
{
	bool TryReadRuntimeOverride(const TCHAR* Key, FString& OutValue);
	void EnsureRuntimeOverrideTemplate();
	FString GetEditableRuntimeConfigPath();
}
