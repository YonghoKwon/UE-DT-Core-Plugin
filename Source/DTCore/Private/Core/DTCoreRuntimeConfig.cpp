#include "Core/DTCoreRuntimeConfig.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DTCoreRuntimeConfig
{
	namespace
	{
		constexpr const TCHAR* RuntimeOverrideSection = TEXT("DTCoreRuntimeOverride");
		constexpr const TCHAR* SettingsSection = TEXT("/Script/DTCore.DTCoreSettings");

		static const TCHAR* RuntimeKeys[] =
		{
			TEXT("BaseApiUrl"),
			TEXT("LocalApiUrl"),
			TEXT("TestApiUrl"),
			TEXT("ProdApiUrl"),
			TEXT("WebSocketUrl"),
			TEXT("WebSocketLogin"),
			TEXT("WebSocketPasscode")
		};

		bool TryReadConfigFileString(const FString& IniPath, const TCHAR* Section, const TCHAR* Key, FString& OutValue)
		{
			if (IniPath.IsEmpty() || !FPaths::FileExists(IniPath))
			{
				return false;
			}

			FConfigFile RuntimeConfig;
			if (!RuntimeConfig.Read(IniPath))
			{
				return false;
			}

			FString Value;
			if (!RuntimeConfig.GetString(Section, Key, Value))
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

		bool TryReadGGameIniString(const TCHAR* Section, const TCHAR* Key, FString& OutValue)
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

		void EnsureTemplateInConfigCache()
		{
			if (!GConfig)
			{
				return;
			}

			bool bChanged = false;
			for (const TCHAR* Key : RuntimeKeys)
			{
				FString ExistingValue;
				if (!GConfig->GetString(RuntimeOverrideSection, Key, ExistingValue, GGameIni))
				{
					GConfig->SetString(RuntimeOverrideSection, Key, TEXT(""), GGameIni);
					bChanged = true;
				}
			}

			if (bChanged)
			{
				GConfig->Flush(false, GGameIni);
			}
		}

		void EnsureTemplateInEditableFile(const FString& IniPath)
		{
			if (IniPath.IsEmpty())
			{
				return;
			}

			const FString ConfigDir = FPaths::GetPath(IniPath);
			IFileManager::Get().MakeDirectory(*ConfigDir, true);

			FConfigFile RuntimeConfig;
			if (FPaths::FileExists(IniPath))
			{
				RuntimeConfig.Read(IniPath);
			}

			bool bChanged = false;
			for (const TCHAR* Key : RuntimeKeys)
			{
				FString ExistingValue;
				if (!RuntimeConfig.GetString(RuntimeOverrideSection, Key, ExistingValue))
				{
					RuntimeConfig.SetString(RuntimeOverrideSection, Key, TEXT(""));
					bChanged = true;
				}
			}

			if (bChanged || !FPaths::FileExists(IniPath))
			{
				RuntimeConfig.Write(IniPath);
			}
		}
	}

	FString GetEditableRuntimeConfigPath()
	{
		// 패키징된 Windows 구조 기준:
		// Windows/<ProjectName>/Config/Game.ini
		// LaunchDir()는 보통 Windows/ 를 가리키므로 ProjectName 폴더를 붙여준다.
		return FPaths::LaunchDir() / FApp::GetProjectName() / TEXT("Config") / TEXT("Game.ini");
	}

	bool TryReadRuntimeOverride(const TCHAR* Key, FString& OutValue)
	{
		const FString EditableRuntimeIni = GetEditableRuntimeConfigPath();

		// 1순위: 패키징 폴더에서 직접 수정하기 쉬운 외부 Game.ini
		if (TryReadConfigFileString(EditableRuntimeIni, RuntimeOverrideSection, Key, OutValue))
		{
			return true;
		}

		// 2순위: 외부 Game.ini에 UDeveloperSettings 섹션으로 값을 넣은 경우도 지원
		if (TryReadConfigFileString(EditableRuntimeIni, SettingsSection, Key, OutValue))
		{
			return true;
		}

		// 3순위: Unreal이 관리하는 GGameIni override 섹션
		if (TryReadGGameIniString(RuntimeOverrideSection, Key, OutValue))
		{
			return true;
		}

		// 4순위: 기존 UDeveloperSettings 섹션에 직접 값을 넣은 경우도 지원
		return TryReadGGameIniString(SettingsSection, Key, OutValue);
	}

	void EnsureRuntimeOverrideTemplate()
	{
		// Unreal ConfigCache 경로와 패키징 폴더의 외부 편집용 Game.ini 둘 다 생성/보강한다.
		EnsureTemplateInConfigCache();
		EnsureTemplateInEditableFile(GetEditableRuntimeConfigPath());
	}
}