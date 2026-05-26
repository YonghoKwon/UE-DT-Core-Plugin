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

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Object")
	TMap<FString, TObjectPtr<UClass>> ObjectBPClasses;

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|DataTable")
	TSoftObjectPtr<UDataTable> ShipObjectNameDataTable;

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket")
	FString WebSocketUrl = TEXT("ws://localhost:61616");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket")
	FString WebSocketLogin = TEXT("artemis");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|WebSocket")
	FString WebSocketPasscode = TEXT("artemis");

	// API 기본 URL
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString BaseApiUrl = TEXT("http://localhost:8090");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString LocalApiUrl = TEXT("http://localhost:8090");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString TestApiUrl = TEXT("");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|API")
	FString ProdApiUrl = TEXT("");

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|Topics")
	TArray<FString> WebSocketTopics = { TEXT("topic.cep.output.0") };

	UPROPERTY(Config, EditAnywhere, Category = "DTCore|Network|Topics")
	TArray<FString> ApiTopics = { TEXT("topic.api.output.0") };

	// 프로젝트 전체에서 기본으로 사용할 위젯 테마 데이터
	UPROPERTY(Config, EditAnywhere, Category = "DTCore|UI")
	TSoftObjectPtr<class UDxWidgetThemeData> DefaultWidgetTheme;
};