#include "Core/DTCoreRuntimeConfig.h"

#include "Misc/ConfigCacheIni.h"

namespace DTCoreRuntimeConfig
{
	namespace
	{
		constexpr const TCHAR* RuntimeOverrideSection = TEXT("DTCoreRuntimeOverride");
		constexpr const TCHAR* SettingsSection = TEXT("/Script/DTCore.DTCoreSettings");

		bool TryReadGameIniString(const TCHAR* Section, const TCHAR* Key, FString& OutValue)
		{
			if (!GConfig)
			{
				return false;
			}

			FString Value;
			if (!GConfig->GetString(Section, Key, Value, GGameIni))
			{
				return false;
			}

			Value = Value.TrimStartAndEnd();
			if (Value.IsEmpty())
			{
				return false;
			}

			OutValue = Value;
			return true;
		}
	}

	bool TryReadRuntimeOverride(const TCHAR* Key, FString& OutValue)
	{
		// 1순위: 패키징 후 사용자가 직접 수정하기 쉬운 별도 override 섹션
		if (TryReadGameIniString(RuntimeOverrideSection, Key, OutValue))
		{
			return true;
		}

		// 2순위: 기존 UDeveloperSettings 섹션에 직접 값을 넣은 경우도 지원
		return TryReadGameIniString(SettingsSection, Key, OutValue);
	}

	void EnsureRuntimeOverrideTemplate()
	{
		if (!GConfig)
		{
			return;
		}

		static const TCHAR* Keys[] =
		{
			TEXT("BaseApiUrl"),
			TEXT("LocalApiUrl"),
			TEXT("TestApiUrl"),
			TEXT("ProdApiUrl"),
			TEXT("WebSocketUrl"),
			TEXT("WebSocketLogin"),
			TEXT("WebSocketPasscode")
		};

		bool bChanged = false;
		for (const TCHAR* Key : Keys)
		{
			FString ExistingValue;
			if (!GConfig->GetString(RuntimeOverrideSection, Key, ExistingValue, GGameIni))
			{
				// 값은 일부러 비워둔다. 운영/개발자가 패키징 후 필요한 값만 채우면 된다.
				GConfig->SetString(RuntimeOverrideSection, Key, TEXT(""), GGameIni);
				bChanged = true;
			}
		}

		if (bChanged)
		{
			GConfig->Flush(false, GGameIni);
		}
	}
}
