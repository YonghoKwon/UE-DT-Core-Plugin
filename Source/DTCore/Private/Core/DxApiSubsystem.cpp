#include "Core/DxApiSubsystem.h"

#include "DTCore.h"
#include "Core/DxDataSubsystem.h"
#include "Core/DTCoreRuntimeConfig.h"
#include "HttpModule.h"
#include "Core/DTCoreSettings.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "TimerManager.h"

void UDxApiSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// CDO 생성자에서의 에셋 로드는 쿠킹/시작 히치 위험이 있어 Initialize에서 수행
	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();
	if (Settings && Settings->ApiDataTable.ToSoftObjectPath().IsValid())
	{
		ApiDataTable = Settings->ApiDataTable.LoadSynchronous();
	}

	DTCoreRuntimeConfig::EnsureRuntimeOverrideTemplate();
	HttpModule = &FHttpModule::Get();
}

void UDxApiSubsystem::Deinitialize()
{
	// 대기 중인 재시도 타이머 일괄 해제
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		for (FTimerHandle& Handle : PendingRetryTimers)
		{
			GameInstance->GetTimerManager().ClearTimer(Handle);
		}
	}
	PendingRetryTimers.Empty();

	// 역순으로 순회하여 중간에 원소가 삭제(Remove)되더라도 크래시가 발생하지 않도록 방어
	for (int32 i = ActiveHttpRequests.Num() - 1; i >= 0; --i)
	{
		if (ActiveHttpRequests.IsValidIndex(i))
		{
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = ActiveHttpRequests[i];
			if (Request->GetStatus() == EHttpRequestStatus::Processing)
			{
				Request->CancelRequest();
			}
		}
	}
	ActiveHttpRequests.Empty();
	HttpModule = nullptr;

	Super::Deinitialize();
}

void UDxApiSubsystem::DxHttpCall(const FString& FullUrl, const FString& Verb, const FString& ContentString, const TMap<FString, FString>& Headers, FDxApiCallback Callback)
{
	FDxHttpRequestContext Context;
	Context.FullUrl = FullUrl;
	Context.Verb = Verb;
	Context.ContentString = ContentString;
	Context.Headers = Headers;
	Context.Callback = Callback;
	Context.AttemptIndex = 0;

	InternalHttpCall(MoveTemp(Context));
}

void UDxApiSubsystem::InternalHttpCall(FDxHttpRequestContext Context)
{
	if (!HttpModule)
	{
		Context.Callback.ExecuteIfBound(false, 0, TEXT("HttpModule is not initialized."));
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = HttpModule->CreateRequest();
	Request->SetURL(Context.FullUrl);
	// Method 설정 (GET, POST, PUT, DELETE 등)
	Request->SetVerb(Context.Verb);
	// 헤더 설정
	for (const auto& Header : Context.Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	// Body 설정 (POST/PUT 등의 경우)
	if (!Context.ContentString.IsEmpty())
	{
		Request->SetContentAsString(Context.ContentString);
	}

	Request->OnProcessRequestComplete().BindUObject(this, &UDxApiSubsystem::InternalOnResponseReceived, Context);

	// 요청을 보내기 전에 추적 배열에 등록
	ActiveHttpRequests.Add(Request);
	if (!Request->ProcessRequest())
	{
		ActiveHttpRequests.Remove(Request);
		Context.Callback.ExecuteIfBound(false, 0, TEXT("HTTP request failed to start."));
	}
}

void UDxApiSubsystem::DxRequestApi(const FName& RowName, FDxApiCallback Callback)
{
	// DT_Api 데이터테이블이 유효한지 체크
	if (!ApiDataTable)
	{
		DX_LOG(GetWorld(), TEXT("DxRequestApi: ApiDataTable is not set."));
		Callback.ExecuteIfBound(false, 0, TEXT("ApiDataTable is not set"));
		return;
	}

	// 데이터테이블에서 Row 검색
	FApiStruct* ApiData = ApiDataTable->FindRow<FApiStruct>(RowName, TEXT("DxRequestApi"));
	if (!ApiData)
	{
		DX_LOG(GetWorld(), TEXT("DxRequestApi: Row '%s' not found in ApiDataTable."), *RowName.ToString());
		Callback.ExecuteIfBound(false, 0, FString::Printf(TEXT("Row '%s' not found"), *RowName.ToString()));
		return;
	}

	// 예시: 기본 헤더 설정
	TMap<FString, FString> DefaultHeaders;
	DefaultHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));

	// ApiType에 따라 Server URL 결정
	FString ServerUrl = GetServerUrl(ApiData->ApiType);
	// HTTP Method
	FString MethodType = GetHttpStr(ApiData->ApiMethod);

	// URL 조합 (예: Server + / + RequestApiType) - 실제 로직에 맞게 수정 필요
	FString FullUrl = ServerUrl + ApiData->ApiUrl;

	// 콜백이 없는 단순 호출 (빈 델리게이트 전달)
	DxHttpCall(FullUrl, MethodType, TEXT(""), DefaultHeaders, Callback);
}

void UDxApiSubsystem::DxRequestApiWithParameter(const FName& RowName, FDxApiCallback Callback, const TArray<FString>& Parameters)
{
	// DT_Api 데이터테이블이 유효한지 체크
	if (!ApiDataTable)
	{
		DX_LOG(GetWorld(), TEXT("DxRequestApiWithParameter: ApiDataTable is not set."));
		Callback.ExecuteIfBound(false, 0, TEXT("ApiDataTable is not set"));
		return;
	}

	// 데이터테이블에서 Row 검색
	FApiStruct* ApiData = ApiDataTable->FindRow<FApiStruct>(RowName, TEXT("DxRequestApiWithParameter"));
	if (!ApiData)
	{
		DX_LOG(GetWorld(), TEXT("DxRequestApiWithParameter: Row '%s' not found in ApiDataTable."), *RowName.ToString());
		Callback.ExecuteIfBound(false, 0, FString::Printf(TEXT("Row '%s' not found"), *RowName.ToString()));
		return;
	}

	// ApiType에 따라 Server URL 결정
	FString ServerUrl = GetServerUrl(ApiData->ApiType);
	// HTTP Method
	FString MethodType = GetHttpStr(ApiData->ApiMethod);

	// URL 조합 ServerUrl + ApiUrl
	FString FullUrl = ServerUrl + ApiData->ApiUrl;

	// Path Parameter가 있으면 URL에 추가
	for (const FString& PathParam : Parameters)
	{
		if (!PathParam.IsEmpty())
		{
			FullUrl += TEXT("/") + PathParam;
		}
	}

	// 헤더 설정
	TMap<FString, FString> DefaultHeaders;
	DefaultHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));

	// Body 설정
	// FString ContentString = ApiData->Body;

	// UE_LOG(LogBase, Log, TEXT("DxRequestApiWithParameter: Calling API - %s %s"), *MethodType, *FullUrl);

	// HTTP 요청 실행
	DxHttpCall(FullUrl, MethodType, TEXT(""), DefaultHeaders, Callback);
}

void UDxApiSubsystem::InternalOnResponseReceived(TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request, TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response, bool bWasSuccessful, FDxHttpRequestContext Context)
{
	// 응답이 왔으므로 (성공이든 실패든) 추적 배열에서 제거
	if (Request.IsValid())
	{
		ActiveHttpRequests.Remove(Request.ToSharedRef());
	}

	int32 ResponseCode = 0;
	FString Content = TEXT("");
	bool bIsOk = false;
	const bool bConnectionFailed = !bWasSuccessful || !Response.IsValid();

	if (!bConnectionFailed)
	{
		ResponseCode = Response->GetResponseCode();
		Content = Response->GetContentAsString();

		// HTTP 코드가 200번대(성공)인지 확인
		if (EHttpResponseCodes::IsOk(ResponseCode))
		{
			bIsOk = true;

			// DxDataSubsystem에 데이터 저장
			TObjectPtr<UGameInstance> GameInstance = GetGameInstance();
			if (GameInstance)
			{
				TObjectPtr<UDxDataSubsystem> DataSubsystem = GameInstance->GetSubsystem<UDxDataSubsystem>();
				if (DataSubsystem)
				{
					DataSubsystem->EnqueueApiData(Content);
				}
			}
		}
		else
		{
			DX_LOG(GetWorld(), TEXT("Response Error [%d]: %s"), ResponseCode, *Content);
		}
	}
	else
	{
		DX_LOG(GetWorld(), TEXT("Connection Failed or No Response: %s"), *Context.FullUrl);
	}

	// 일시적 오류(연결 실패/5xx)는 지수 백오프로 재시도. 4xx는 재시도하지 않음
	const bool bRetryable = bConnectionFailed || ResponseCode >= 500;
	if (!bIsOk && bRetryable && Context.AttemptIndex + 1 < MaxHttpAttempts && HttpModule)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			const float RetryDelay = InitialHttpRetryDelay * FMath::Pow(2.0f, static_cast<float>(Context.AttemptIndex));
			Context.AttemptIndex++;

			DX_LOG(GetWorld(), TEXT("HTTP retry in %.1fs (Attempt %d/%d): %s"), RetryDelay, Context.AttemptIndex + 1, MaxHttpAttempts, *Context.FullUrl);

			// 이미 만료된 타이머 핸들은 정리해 누적 방지
			FTimerManager& TimerManager = GameInstance->GetTimerManager();
			PendingRetryTimers.RemoveAll([&TimerManager](const FTimerHandle& Handle)
			{
				return !TimerManager.IsTimerActive(Handle);
			});

			FTimerHandle& RetryHandle = PendingRetryTimers.AddDefaulted_GetRef();
			GameInstance->GetTimerManager().SetTimer(
				RetryHandle,
				FTimerDelegate::CreateWeakLambda(this, [this, Context]()
				{
					InternalHttpCall(Context);
				}),
				RetryDelay,
				false
			);

			// 최종 결과가 확정될 때까지 콜백 호출을 미룸
			return;
		}
	}

	Context.Callback.ExecuteIfBound(bIsOk, ResponseCode, Content);
}

FString UDxApiSubsystem::GetServerUrl(EApiType ApiType) const
{
	// 1. Game.ini override 값을 먼저 읽는다. 값이 비어 있으면 무시한다.
	// 패키징 후 생성되는 [DTCoreRuntimeOverride] 섹션에 값을 채우면 재패키징 없이 URL을 바꿀 수 있다.
	FString ConfigKey;
	FString SettingsValue;

	const UDTCoreSettings* Settings = GetDefault<UDTCoreSettings>();

	switch (ApiType)
	{
	case EApiType::Local:
		ConfigKey = TEXT("LocalApiUrl");
		SettingsValue = Settings ? Settings->LocalApiUrl : TEXT("");
		break;
	case EApiType::Test:
		ConfigKey = TEXT("TestApiUrl");
		SettingsValue = Settings ? Settings->TestApiUrl : TEXT("");
		break;
	case EApiType::Prod:
		ConfigKey = TEXT("ProdApiUrl");
		SettingsValue = Settings ? Settings->ProdApiUrl : TEXT("");
		break;
	default:
		ConfigKey = TEXT("BaseApiUrl");
		SettingsValue = Settings ? Settings->BaseApiUrl : TEXT("");
		break;
	}

	FString RuntimeOverride;
	if (DTCoreRuntimeConfig::TryReadRuntimeOverride(*ConfigKey, RuntimeOverride))
	{
		return RuntimeOverride;
	}

	if (!SettingsValue.IsEmpty())
	{
		return SettingsValue;
	}

	return Settings && !Settings->BaseApiUrl.IsEmpty() ? Settings->BaseApiUrl : TEXT("http://localhost:8090");
}

FString UDxApiSubsystem::GetHttpStr(EApiMethod ApiMethod) const
{
	// HTTP Method 변환
	const UEnum* ApiMethodEnum = StaticEnum<EApiMethod>();
	return ApiMethodEnum->GetDisplayNameTextByValue(static_cast<int64>(ApiMethod)).ToString();
}
