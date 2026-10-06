#include "Player/DxPlayerControllerBase.h"

#include "DTCore.h"
#include "Player/DxPlayerBase.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InteractableActor/InteractableActor.h"
#include "Core/DxWidgetSubsystem.h"

ADxPlayerControllerBase::ADxPlayerControllerBase()
{
}

void ADxPlayerControllerBase::BeginPlay()
{
	Super::BeginPlay();

	DxPlayerBase = Cast<ADxPlayerBase>(GetPawn());

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = false;
		Subsystem->AddMappingContext(PlayerContext, 0, Options);
	}

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	SetInputMode(FInputModeGameAndUI());
	CurrentMouseCursor = EMouseCursor::Default;

	SetActorTickEnabled(true);
}

void ADxPlayerControllerBase::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent))
	{
		EnhancedInputComponent->BindAction(MovementAction, ETriggerEvent::Triggered, this, &ADxPlayerControllerBase::Move);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADxPlayerControllerBase::Look);
		EnhancedInputComponent->BindAction(MouseWheelAction, ETriggerEvent::Triggered, this, &ADxPlayerControllerBase::ControlMoveSpeed);
		EnhancedInputComponent->BindAction(UpDownAction, ETriggerEvent::Triggered, this, &ADxPlayerControllerBase::MoveUpDown);

		EnhancedInputComponent->BindAction(RightMouseButtonAction, ETriggerEvent::Triggered, this, &ADxPlayerControllerBase::ClickRightMouseButton);
		EnhancedInputComponent->BindAction(LeftMouseButtonAction, ETriggerEvent::Triggered, this, &ADxPlayerControllerBase::ClickLeftMouseButton);

		// 일반 Number 버튼 추가 (추가 바인딩 필요 시)
		// for (int32 i = 0; i < NumberKeyActions.Num(); ++i)
		// {
		// 	if (NumberKeyActions[i])
		// 	{
		// 		EnhancedInputComponent->BindAction(NumberKeyActions[i], ETriggerEvent::Triggered, this, &ADxPlayerControllerBase::OnNumberKeyPressed, i);
		// 	}
		// }
	}
}

void ADxPlayerControllerBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!DxPlayerBase) return;

	// UI 입력으로 이동 처리
	if (UIMoveInput != FVector2D::ZeroVector)
	{
		DxPlayerBase->Move(UIMoveInput);
	}

	// UI 입력으로 회전 처리
	if (UILookInput != FVector2D::ZeroVector)
	{
		DxPlayerBase->Look(UILookInput);
	}

	// UI 입력으로 수직 이동 처리
	if (UIVerticalInput != 0.0f)
	{
		DxPlayerBase->MoveUpDown(UIVerticalInput);
	}

	LastHoverCheckTime += DeltaTime;
	if (!bIsClickRightMouseButton && bShowMouseCursor && LastHoverCheckTime >= HoverCheckInterval)
	{
		CheckMouseHover();
		LastHoverCheckTime = 0.0f; // 타이머 초기화
	}
}

// 이동 제어
void ADxPlayerControllerBase::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (ADxPlayerBase* DxPlayer = Cast<ADxPlayerBase>(GetPawn()))
	{
		DxPlayer->Move(MovementVector);
	}
}
void ADxPlayerControllerBase::MoveUpDown(const FInputActionValue& Value)
{
	const float value = Value.Get<float>();

	if (ADxPlayerBase* DxPlayer = Cast<ADxPlayerBase>(GetPawn()))
	{
		DxPlayer->MoveUpDown(value);
	}
}
// 시점 변경
void ADxPlayerControllerBase::Look(const FInputActionValue& Value)
{
	const FVector2D LookVector = Value.Get<FVector2D>();
	if (ADxPlayerBase* DxPlayer = Cast<ADxPlayerBase>(GetPawn()))
	{
		DxPlayer->Look(LookVector);
	}
}
// 배속(또는 줌) 제어 - 실제 동작은 Pawn(HandleMouseWheel)에서 결정
void ADxPlayerControllerBase::ControlMoveSpeed(const FInputActionValue& Value)
{
	const float RawValue = Value.Get<float>();
	if (RawValue == 0.f) return;

	// 마우스가 UMG 위젯(예: ScrollBox) 위에 있으면 위젯 스크롤과 겹치지 않도록 카메라 줌/속도 조절을 막는다.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDxWidgetSubsystem* WidgetSubsystem = GI->GetSubsystem<UDxWidgetSubsystem>())
		{
			if (WidgetSubsystem->IsMouseOverAnyWidget())
			{
				return;
			}
		}
	}

	if (ADxPlayerBase* DxPlayer = Cast<ADxPlayerBase>(GetPawn()))
	{
		DxPlayer->HandleMouseWheel(RawValue, ControlSpeedStep);
	}
}

void ADxPlayerControllerBase::ClickLeftMouseButton(const FInputActionValue& Value)
{
	const bool value = Value.Get<bool>();
	double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
#if WITH_DEV_AUTOMATION_TESTS
	if (InputClockForTests) Now=InputClockForTests();
#endif
	if (ClickActivationPolicy==EDxClickActivationPolicy::SingleRelease)
	{
		if (value) { if (!bWasLeftMouseButtonDown) bWasLeftMouseButtonDown=true; return; }
		const bool WasPressed=bWasLeftMouseButtonDown; bWasLeftMouseButtonDown=false;
		if (WasPressed && PossibleClick && !bIsWidgetUnderMouse && IsValid(CurrentHoveredActor)) ActivateHoveredActor();
		return;
	}

	// [진단용 로그] 실제 입력 이벤트 패턴 확인 (PixelStreaming 다중클릭 문제 추적)
	DX_LOG(GetWorld(), TEXT("[ClickLeftMouseButton] value=%s, bWasDown=%s, Time=%.4f"),
		value ? TEXT("true(Press)") : TEXT("false(Release)"),
		bWasLeftMouseButtonDown ? TEXT("true") : TEXT("false"),
		Now);

	if (value)
	{
		// PixelStreaming 환경에서는 동일 물릭 클릭에 대해 Press 가 중복 전달될 수 있으므로,
		// 이미 눌려있는 상태에서 온 중복 Press는 무신한다.
		if (bWasLeftMouseButtonDown)
		{
			DX_LOG(GetWorld(), TEXT("[ClickLeftMouseButton] Ignored - duplicate Press while already down. Time=%.4f"), Now);
			return;
		}
		bWasLeftMouseButtonDown = true;

		// 더블클릭 판정을 Press 타이밍 기준으로 수행한다.
		// (Release 이벤트는 PixelStreaming 전송 과정에서 수십ms~수초까지 불규칙하게 지연되어
		// 타이밍 기준으로 신뢰할 수 없음이 로그로 확인됨. Press는 지연이 짧고 일정함.
		if (!PossibleClick || bIsWidgetUnderMouse)
		{
			DX_LOG(GetWorld(), TEXT("[ClickLeftMouseButton] Press ignored - PossibleClick=%s, WidgetUnderMouse=%s"),
				PossibleClick ? TEXT("true") : TEXT("false"), bIsWidgetUnderMouse ? TEXT("true") : TEXT("false"));
			return;
		}

		if (!CurrentHoveredActor)
		{
			DX_LOG(GetWorld(), TEXT("[ClickLeftMouseButton] Press ignored - CurrentHoverdActor is null. Time=%.4f"), Now);
			LastPressedActor = nullptr;
			LastPressTime = -1.0;
			return;
		}

		const bool bIsSameActorAsLastPress = LastPressedActor.IsValid() && LastPressedActor.Get() == CurrentHoveredActor;
		const bool bWithinDoubleClickWindow = LastPressTime >= 0.0 && (Now - LastPressTime) <= static_cast<double>(DoubleClickPressThreshold);

		if (bIsSameActorAsLastPress && bWithinDoubleClickWindow)
		{
			// 더블클릭 확정 (Press-Press 간격 기준)
			DX_LOG(GetWorld(), TEXT("[ClickLeftMouseButton] CONFIRMED double click by PRESS timing (delta=%.4f) -> Click() CALLED. Actor=%s, Time=%.4f"),
				Now - LastPressTime, *CurrentHoveredActor->GetName(), Now);

			LastPressedActor = nullptr;
			LastPressTime = -1.0;
			ActivateHoveredActor();
		}
		else
		{
			// 첫 번째 클릭 후보로 기록, 다음 Press를 기다림
			DX_LOG(GetWorld(), TEXT("[ClickLeftMouseButton] FIRST press registered - Actor=%s, waiting for second press within %.2fs"),
				*CurrentHoveredActor->GetName(), DoubleClickPressThreshold);
			LastPressedActor = CurrentHoveredActor;
			LastPressTime = Now;
		}
		return;
	}

	// value == false (Release) - 더블클릭 판정에는 사용하지 않고, 눌림 상태 플래그만 해제한다.
	bWasLeftMouseButtonDown = false;
}

void ADxPlayerControllerBase::ActivateHoveredActor()
{
	if (!IsValid(CurrentHoveredActor)) return;
#if WITH_DEV_AUTOMATION_TESTS
	if (ClickSinkForTests) { ClickSinkForTests(CurrentHoveredActor); return; }
#endif
	CurrentHoveredActor->Click();
}

// 우클릭 제어
void ADxPlayerControllerBase::ClickRightMouseButton(const FInputActionValue& Value)
{
	const bool value = Value.Get<bool>();

	if (value)
	{
		bIsClickRightMouseButton = true;

		bShowMouseCursor = false;
		bEnableClickEvents = false;
		bEnableMouseOverEvents = false;
		SetInputMode(FInputModeGameOnly());
	}
	else
	{
		bIsClickRightMouseButton = false;

		bShowMouseCursor = true;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;
		SetInputMode(FInputModeGameAndUI());
	}
}

// 마우스 호버 감지
void ADxPlayerControllerBase::CheckMouseHover()
{
	// 오픈된 DxWidget 위에 마우스가 있으면 3D 호버/클릭 처리 차단
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDxWidgetSubsystem* WidgetSubsystem = GI->GetSubsystem<UDxWidgetSubsystem>())
		{
			if (WidgetSubsystem->IsMouseOverAnyWidget())
			{
				bIsWidgetUnderMouse = true;
				// 기존에 호버된 3D 액터가 있으면 Unhover 처리
				if (CurrentHoveredActor)
				{
					CurrentHoveredActor->OnCursorUnhover();
					CurrentHoveredActor = nullptr;
					CurrentHoveredMesh = nullptr;
				}
				return;
			}
		}
	}
	bIsWidgetUnderMouse = false;

	// 마우스 위치 계산 (Tick에서 호출 조건 확인 후 실행됨)
	FVector WorldLocation, WorldDirection;
	if (!DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
	{
		return;
	}

	// 트레이스 설정 (매번 생성 비용 절약)
	static const FCollisionObjectQueryParams ObjectQueryParams = []()
	{
		FCollisionObjectQueryParams Params;
		Params.AddObjectTypesToQuery(ECC_WorldStatic);
		Params.AddObjectTypesToQuery(ECC_WorldDynamic);
		Params.AddObjectTypesToQuery(ECC_Pawn);
		Params.AddObjectTypesToQuery(ECC_PhysicsBody);
		Params.AddObjectTypesToQuery(ECC_GameTraceChannel1);
		return Params;
	}();

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetPawn());

	TArray<FHitResult> HitResults;
	FVector Start = WorldLocation;
	FVector End = Start + (WorldDirection * 150000.0f); // 100km

	// Single → Multi 로 변경하여 모든 히트를 수집
	GetWorld()->LineTraceMultiByObjectType(
		HitResults, Start, End, ObjectQueryParams, QueryParams
	);

	// GameTraceChannel1(Test-> Alarm 등) 오브젝트를 최우선으로 선택
	const FHitResult* BestHit = nullptr;
	for (const FHitResult& Hit : HitResults)
	{
		if (const UPrimitiveComponent* Comp = Hit.GetComponent())
		{
			if (Comp->GetCollisionObjectType() == ECC_GameTraceChannel1)
			{
				BestHit = &Hit;
				break;
			}
		}
	}

	if (!BestHit && HitResults.Num() > 0)
	{
		BestHit = &HitResults[0];
	}

	AActor* HitActorRaw = BestHit ? BestHit->GetActor() : nullptr;

	// 같은 액터 위에서 마우스가 움직일 때는 로직 건너뜀
	if (CurrentHoveredActor && CurrentHoveredActor == HitActorRaw)
	{
		return;
	}

	// 기존 액터 Unhover
	if (CurrentHoveredActor)
	{
		CurrentHoveredActor->OnCursorUnhover();
		CurrentHoveredActor = nullptr;
		CurrentHoveredMesh = nullptr;
	}

	// 새 액터 Hover
	if (BestHit)
	{
		if (AInteractableActor* NewHitActor = Cast<AInteractableActor>(HitActorRaw))
		{
			UPrimitiveComponent* NewHitComp = BestHit->GetComponent();

			NewHitActor->OnCursorHover(NewHitComp);
			CurrentHoveredActor = NewHitActor;
			CurrentHoveredMesh = NewHitComp;
		}
	}
}

// 버튼으로 이동 제어
void ADxPlayerControllerBase::SetUIMoveInput(const FVector2D& MoveInput)
{
	UIMoveInput = MoveInput;
}

void ADxPlayerControllerBase::SetUILookInput(const FVector2D& LookInput)
{
	UILookInput = LookInput;
}

void ADxPlayerControllerBase::SetUIVerticalInput(float Value)
{
	UIVerticalInput = Value;
}

// 플레이어 속도 순환
void ADxPlayerControllerBase::CyclePlayerControlSpeed()
{
	if (ADxPlayerBase* DxPlayer = Cast<ADxPlayerBase>(GetPawn()))
	{
		DxPlayer->CycleControlSpeed();
	}
}

// 플레이어 속도 가져오기
float ADxPlayerControllerBase::GetPlayerControlSpeed()
{
	if (ADxPlayerBase* DxPlayer = Cast<ADxPlayerBase>(GetPawn()))
	{
		return DxPlayer->GetControlSpeed();
	}
	return 0.0f;
}
