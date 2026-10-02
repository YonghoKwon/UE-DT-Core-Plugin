#pragma once

#include "CoreMinimal.h"
#include "UI/DxWidgetInfo.h"
#include "Engine/DataAsset.h"
#include "DxWidgetConfigData.generated.h"

enum class EPlayerViewType : uint8;
/**
 * 
 */
UCLASS(BlueprintType)
class DTCORE_API UDxWidgetConfigData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Config")
	TMap<uint8, FWidgetInfoList> WidgetMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Config")
	TMap<EPlayerViewType, uint8> DefaultViewModeWidgetFlags;
};
