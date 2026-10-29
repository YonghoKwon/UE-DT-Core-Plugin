#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "DTCoreSettings.generated.h"

UCLASS(Config=Game, meta=(DisplayName="DTCore Settings"))
class DTCORE_API UDTCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// API 데이터 테이블 경로 설정
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|DataTable")
	TSoftObjectPtr<UDataTable> ApiDataTable;
	// WebSocket 데이터 테이블 경로 설정
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|DataTable")
	TSoftObjectPtr<UDataTable> WebSocketDataTable;
	// Level 데이터 테이블 경로 설정
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|DataTable")
	TSoftObjectPtr<UDataTable> LevelDataTable;
	// Legacy 프로젝트 전용 필드로 기존 Config 호환만 유지하며 신규 사용은 권장하지 않는다.
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|DataTable", meta = (DisplayName = "CCTV URL DataTable (Legacy)", ToolTip = "Legacy 프로젝트 전용 필드. DTCore 내부에서 사용하지 않으며 신규 사용 비권장."))
	TSoftObjectPtr<UDataTable> CctvUrlDataTable;

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Object")
	TMap<FString, TObjectPtr<UClass>> ObjectBPClasses;

	// Legacy 프로젝트 전용 필드로 기존 Config 키를 유지한다.
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|DataTable", meta = (DisplayName = "Ship Object Name DataTable (Legacy)", ToolTip = "Legacy 프로젝트 전용 필드. DTCore 내부에서 사용하지 않으며 신규 사용 비권장."))
	TSoftObjectPtr<UDataTable> ShipObjectNameDataTable;

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket")
	FString WebSocketUrl = TEXT("");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket")
	FString WebSocketLogin = TEXT("");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket")
	FString WebSocketPasscode = TEXT("");

	// WebSocket 재연결 최대 시도 횟수 (0 = 무제한)
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket", meta = (ClampMin = "0"))
	int32 MaxReconnectAttempts = 10;

	// 0이면 기존처럼 모든 구독 receipt callback을 기다린다.
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket", meta = (ClampMin = "0"))
	float SubscribeReceiptTimeoutSeconds = 0.0f;

	// 0이면 SetTimeout을 호출하지 않고 엔진 기본값을 따른다.
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API", meta = (ClampMin = "0"))
	float HttpRequestTimeoutSeconds = 0.0f;

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API", meta = (ClampMin = "1"))
	int32 HttpMaxAttempts = 3;

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API", meta = (ClampMin = "0.001"))
	float HttpInitialRetryDelaySeconds = 1.0f;

	// 빈 값은 LaunchDir/Logs/CustomLogs이며 상대 경로는 LaunchDir 기준이다.
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Log")
	FString LogDirectory;

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Log", meta = (ClampMin = "0"))
	int32 LogRetentionDays = 7;

	// API 기본 URL
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString BaseApiUrl = TEXT("");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString LocalApiUrl = TEXT("");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString TestApiUrl = TEXT("");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString ProdApiUrl = TEXT("");

	// 구독 대상 목록이며 라우팅은 MESSAGE_ID 기준이다.
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|Topics")
	TArray<FString> WebSocketTopics;

	// 구독 대상 목록이며 라우팅은 MESSAGE_ID 기준이다.
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|Topics")
	TArray<FString> ApiTopics;

	// 프로젝트 전체에서 기본으로 사용할 위젯 테마 데이터
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|UI")
	TSoftObjectPtr<class UDxWidgetThemeData> DefaultWidgetTheme;
	
	// 프레임 제한 (0 = 무제한)
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Performance", meta = (ClampMin = "0.0"))
	float FrameRateLimit = 60.0f;
	
	/**
	 * 중복 체크를 건너뜀 WidgetFlag 값 목록.
	 * 프로젝트의 DefaultGame.ini [/Script/DTCore.DTCoreSettings] 섹션에서 설정하세요.
	 * 예) +NoDuplicateCheckFlags=3
	 */
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|UI")
	TArray<uint8> NoDuplicateCheckFlags;
};