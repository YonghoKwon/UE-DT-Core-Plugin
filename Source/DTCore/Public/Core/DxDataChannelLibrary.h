#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DxDataChannelLibrary.generated.h"

/**
 * 데이터 채널 공통 규약 도우미. 새 채널 코드에서만 사용하며 기본 WebSocket 경로는 쓰지 않는다.
 * - destination: 소문자 영숫자·'-'·'_' 조각을 '.'으로 잇는다. 예) topic.virtual.sensor.export.0
 * - 표준 헤더: dx-schema-version, dx-message-id, dx-sent-at(UTC ISO-8601)
 */
UCLASS()
class DTCORE_API UDxDataChannelLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static const FName SchemaVersionHeader;
	static const FName MessageIdHeader;
	static const FName SentAtHeader;

	// 조각 하나라도 규칙에 맞지 않으면 빈 문자열
	UFUNCTION(BlueprintPure, Category = "DxDataChannel")
	static FString MakeDestination(const TArray<FString>& Segments);

	UFUNCTION(BlueprintPure, Category = "DxDataChannel")
	static bool IsValidDestination(const FString& Destination);

	// MessageId가 비어 있으면 GUID를 만든다. 기존 Headers 값은 덮어쓰지 않는다.
	UFUNCTION(BlueprintCallable, Category = "DxDataChannel", meta = (AutoCreateRefTerm = "Headers"))
	static TMap<FName, FString> MakeStandardHeaders(const FString& SchemaVersion, const FString& MessageId, const TMap<FName, FString>& Headers);
};
