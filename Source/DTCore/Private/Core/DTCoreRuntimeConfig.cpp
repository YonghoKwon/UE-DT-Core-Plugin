#include "Core/DTCoreRuntimeConfig.h"

#include "Misc/App.h"
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

		FString NormalizeRuntimePath(const FString& Path)
		{
			FString NormalizedPath = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(NormalizedPath);
			return NormalizedPath;
		}

		void AddUniqueRuntimePath(TArray<FString>& Paths, const FString& Path)
		{
			if (Path.IsEmpty())
			{
				return;
			}

			const FString NormalizedPath = NormalizeRuntimePath(Path);
			Paths.AddUnique(NormalizedPath);
		}

		TArray<FString> GetRuntimeConfigCandidatePaths()
		{
			TArray<FString> Paths;

			// 1순위: Unreal이 계산한 ProjectDir 기준. 패키징에서는 보통 <PackageRoot>/<ProjectName>/ 를 가리킨다.
			AddUniqueRuntimePath(Paths, FPaths::ProjectDir() / TEXT("Config") / TEXT("Game.ini"));

			// 2순위: 실행 파일 폴더가 프로젝트 루트인 경우.
			AddUniqueRuntimePath(Paths, FPaths::LaunchDir() / TEXT("Config") / TEXT("Game.ini"));

			// 3순위: 실행 파일 폴더가 <ProjectName>/Binaries/Win64 또는 <ProjectName>/Binaries/Linux 인 경우.
			AddUniqueRuntimePath(Paths, FPaths::LaunchDir() / TEXT("../../Config/Game.ini"));

			// 4순위: 실행 파일 폴더가 패키지 루트(예: Windows/)인 경우.
			AddUniqueRuntimePath(Paths, FPaths::LaunchDir() / FApp::GetProjectName() / TEXT("Config") / TEXT("Game.ini"));

			return Paths;
		}

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
		const TArray<FString> CandidatePaths = GetRuntimeConfigCandidatePaths();
		return CandidatePaths.Num() > 0 ? CandidatePaths[0] : FString();
	}

	bool TryReadRuntimeOverride(const TCHAR* Key, FString& OutValue)
	{
		const TArray<FString> CandidatePaths = GetRuntimeConfigCandidatePaths();

		// 1순위: 패키징 폴더에서 직접 수정하기 쉬운 외부 Game.ini 후보들.
		for (const FString& RuntimeIniPath : CandidatePaths)
		{
			if (TryReadConfigFileString(RuntimeIniPath, RuntimeOverrideSection, Key, OutValue))
			{
				return true;
			}

			if (TryReadConfigFileString(RuntimeIniPath, SettingsSection, Key, OutValue))
			{
				return true;
			}
		}

		// 2순위: Unreal이 관리하는 GGameIni override 섹션.
		if (TryReadGGameIniString(RuntimeOverrideSection, Key, OutValue))
		{
			return true;
		}

		// 3순위: 기존 UDeveloperSettings 섹션에 직접 값을 넣은 경우도 지원.
		return TryReadGGameIniString(SettingsSection, Key, OutValue);
	}

	void EnsureRuntimeOverrideTemplate()
	{
		// Unreal ConfigCache 경로와 패키징 폴더의 외부 편집용 Game.ini 둘 다 생성/보강한다.
		EnsureTemplateInConfigCache();

		// 생성은 대표 경로(ProjectDir/Config/Game.ini)에 수행한다.
		// 읽기는 TryReadRuntimeOverride에서 여러 후보 경로를 모두 확인한다.
		EnsureTemplateInEditableFile(GetEditableRuntimeConfigPath());
	}
}