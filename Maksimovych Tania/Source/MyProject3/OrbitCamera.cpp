// Fill out your copyright notice in the Description page of Project Settings.

#include "OrbitCamera.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlayer.h"

#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "UObject/UObjectGlobals.h"
#endif

UOrbitCameraDriverComponent::UOrbitCameraDriverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	bTickInEditor = true;
}

void UOrbitCameraDriverComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (AOrbitCamera* OrbitCamera = Cast<AOrbitCamera>(GetOwner()))
	{
		OrbitCamera->UpdateOrbit(DeltaTime);
	}
}

AOrbitCamera::AOrbitCamera()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bUsePawnControlRotation = false;

	OrbitDriver = CreateDefaultSubobject<UOrbitCameraDriverComponent>(TEXT("OrbitDriver"));

	InitDefaultTrailerShots();
}

void AOrbitCamera::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (IsTemplate())
	{
		return;
	}

	SetupDetachedCamera();
	ResolveTarget();
	if (TrailerShots.Num() > 0)
	{
		ApplyShot(InstanceShotIndex, false);
	}
	BindSequencePlayers();
	UpdateOrbitTransform();

#if WITH_EDITOR
	RegisterEditorTicker();
#endif
}

void AOrbitCamera::SetupDetachedCamera()
{
	if (!Camera)
	{
		return;
	}

	// Sequencer often locks the actor transform when BP_OrbitCamera is added as a track.
	// Move only the camera component in world space.
	if (Camera->GetAttachParent())
	{
		Camera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void AOrbitCamera::BeginPlay()
{
	Super::BeginPlay();

	SetupDetachedCamera();
	OrbitAngleDegrees = StartAngleDegrees;
	ResolveTarget();
	if (TrailerShots.Num() > 0)
	{
		ApplyShot(InstanceShotIndex, false);
	}
	UpdateOrbitTransform();
	BindSequencePlayers();

	// MRQ / Sequencer may create the sequence player slightly later.
	FTimerHandle BindTimer;
	GetWorldTimerManager().SetTimer(BindTimer, this, &AOrbitCamera::BindSequencePlayers, 0.25f, false);

	if (bAutoSetViewTarget)
	{
		ActivateAsViewTarget();

		FTimerHandle ViewTargetTimer;
		GetWorldTimerManager().SetTimer(ViewTargetTimer, this, &AOrbitCamera::ActivateAsViewTarget, 0.15f, false);
	}
}

void AOrbitCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindSequencePlayers();
#if WITH_EDITOR
	UnregisterEditorTicker();
#endif
	Super::EndPlay(EndPlayReason);
}

void AOrbitCamera::BeginDestroy()
{
	UnbindSequencePlayers();
#if WITH_EDITOR
	UnregisterEditorTicker();
#endif
	Super::BeginDestroy();
}

bool AOrbitCamera::IsSequencePlayerActive(const UMovieSceneSequencePlayer* Player) const
{
	return Player && (Player->IsPlaying() || Player->IsPaused());
}

bool AOrbitCamera::DoesSequencePlayerMatchWorld(const UMovieSceneSequencePlayer* Player) const
{
	if (!Player)
	{
		return false;
	}

	const UWorld* MyWorld = GetWorld();
	if (!MyWorld)
	{
		return true;
	}

	const UObject* Context = Player->GetPlaybackContext();
	if (!Context)
	{
		return true;
	}

	if (const UWorld* ContextWorld = Context->GetWorld())
	{
		return ContextWorld == MyWorld;
	}

	return true;
}

bool AOrbitCamera::IsAnySequencePlaying() const
{
	for (TObjectIterator<UMovieSceneSequencePlayer> It; It; ++It)
	{
		const UMovieSceneSequencePlayer* Player = *It;
		if (DoesSequencePlayerMatchWorld(Player) && IsSequencePlayerActive(Player))
		{
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR
void AOrbitCamera::RegisterEditorTicker()
{
	if (!GIsEditor || EditorTickerHandle.IsValid())
	{
		return;
	}

	EditorTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &AOrbitCamera::EditorTickerCallback));
}

void AOrbitCamera::UnregisterEditorTicker()
{
	if (EditorTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorTickerHandle);
		EditorTickerHandle.Reset();
	}
}

bool AOrbitCamera::EditorTickerCallback(float DeltaTime)
{
	if (!IsValid(this) || IsActorBeingDestroyed() || !bOrbitEnabled)
	{
		return true;
	}

#if WITH_EDITOR
	if (IsGarbageCollecting() || GIsSavingPackage)
	{
		return true;
	}
#endif

	UpdateOrbit(DeltaTime);
	return true;
}
#endif

void AOrbitCamera::RefreshCachedSequencePlayer()
{
	CachedLevelSequencePlayer.Reset();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ALevelSequenceActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		ULevelSequencePlayer* Player = ActorIt->GetSequencePlayer();
		if (IsValid(Player))
		{
			CachedLevelSequencePlayer = Player;
			return;
		}
	}
}

void AOrbitCamera::BindSequencePlayers()
{
	RefreshCachedSequencePlayer();

	UMovieSceneSequencePlayer* Player = CachedLevelSequencePlayer.Get();
	if (!IsValid(Player) || BoundSequencePlayers.Contains(Player))
	{
		return;
	}

	Player->OnSequenceUpdated().AddUObject(this, &AOrbitCamera::OnSequencePlayerUpdated);
	BoundSequencePlayers.Add(Player);
}

void AOrbitCamera::UnbindSequencePlayers()
{
	for (const TWeakObjectPtr<UMovieSceneSequencePlayer>& WeakPlayer : BoundSequencePlayers)
	{
		if (UMovieSceneSequencePlayer* Player = WeakPlayer.Get())
		{
			Player->OnSequenceUpdated().RemoveAll(this);
		}
	}
	BoundSequencePlayers.Empty();
}

void AOrbitCamera::OnSequencePlayerUpdated(
	const UMovieSceneSequencePlayer& Player,
	FFrameTime CurrentTime,
	FFrameTime PreviousTime)
{
	(void)CurrentTime;
	(void)PreviousTime;

	if (!bOrbitEnabled)
	{
		return;
	}

	bOrbitDrivenBySequenceThisFrame = true;
	TrySyncOrbitFromSequencePlayer(&Player);
	EnsureTargetResolved();
	SyncShotFromIndex();
	UpdateOrbitTransform();
}

void AOrbitCamera::ApplyOrbitFromTimeSeconds(float TimeSeconds)
{
	OrbitAngleDegrees = FMath::Fmod(StartAngleDegrees + TimeSeconds * OrbitSpeedDegrees, 360.f);
}

#if WITH_EDITOR
void AOrbitCamera::PostInterpChange(FProperty* PropertyThatChanged)
{
	Super::PostInterpChange(PropertyThatChanged);

	if (!bOrbitEnabled || !PropertyThatChanged)
	{
		return;
	}

	const FName PropertyName = PropertyThatChanged->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, OrbitAngleDegrees)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, CurrentShotIndex)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, bAutoAdvanceAngle)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, bDollyEnabled)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, DollyAlpha)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, bOrbitEnabled)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, OrbitSpeedDegrees))
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, CurrentShotIndex))
		{
			ApplyShot(CurrentShotIndex, false);
		}

		EnsureTargetResolved();
		UpdateOrbitTransform();
	}
}

void AOrbitCamera::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, InstanceShotIndex))
	{
		ApplyShot(InstanceShotIndex, false);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AOrbitCamera, CurrentShotIndex))
	{
		ApplyShot(CurrentShotIndex, false);
	}
}
#endif

void AOrbitCamera::InitDefaultTrailerShots()
{
	if (TrailerShots.Num() > 0)
	{
		return;
	}

	TrailerShots.Reserve(6);

	FOrbitCameraShot Shot;

	Shot = FOrbitCameraShot();
	Shot.ShotName = TEXT("FrontThreeQuarter");
	Shot.OrbitAngleDegrees = 40.f;
	Shot.OrbitRadius = 620.f;
	Shot.OrbitHeight = 25.f;
	Shot.LookAtOffset = FVector(0.f, 0.f, 95.f);
	Shot.bShotOverridesOrbitMotion = true;
	Shot.bShotAutoAdvanceAngle = true;
	Shot.ShotOrbitSpeedDegrees = 5.f;
	TrailerShots.Add(Shot);

	Shot = FOrbitCameraShot();
	Shot.ShotName = TEXT("LowHeroFront");
	Shot.bStaticShot = true;
	Shot.OrbitAngleDegrees = 28.f;
	Shot.OrbitRadius = 480.f;
	Shot.OrbitHeight = -55.f;
	Shot.LookAtOffset = FVector(0.f, 0.f, 75.f);
	TrailerShots.Add(Shot);

	Shot = FOrbitCameraShot();
	Shot.ShotName = TEXT("SideProfile");
	Shot.OrbitAngleDegrees = 90.f;
	Shot.OrbitRadius = 880.f;
	Shot.OrbitHeight = 0.f;
	Shot.LookAtOffset = FVector(0.f, 0.f, 90.f);
	Shot.bShotOverridesOrbitMotion = true;
	Shot.bShotAutoAdvanceAngle = true;
	Shot.ShotOrbitSpeedDegrees = 4.f;
	TrailerShots.Add(Shot);

	Shot = FOrbitCameraShot();
	Shot.ShotName = TEXT("RearThreeQuarter");
	Shot.bStaticShot = true;
	Shot.OrbitAngleDegrees = 145.f;
	Shot.OrbitRadius = 640.f;
	Shot.OrbitHeight = 20.f;
	Shot.LookAtOffset = FVector(0.f, 0.f, 100.f);
	TrailerShots.Add(Shot);

	Shot = FOrbitCameraShot();
	Shot.ShotName = TEXT("Rear");
	Shot.bStaticShot = true;
	Shot.OrbitAngleDegrees = 180.f;
	Shot.OrbitRadius = 760.f;
	Shot.OrbitHeight = 15.f;
	Shot.LookAtOffset = FVector(0.f, 0.f, 105.f);
	TrailerShots.Add(Shot);

	Shot = FOrbitCameraShot();
	Shot.ShotName = TEXT("Front");
	Shot.bStaticShot = true;
	Shot.OrbitAngleDegrees = 0.f;
	Shot.OrbitRadius = 720.f;
	Shot.OrbitHeight = 30.f;
	Shot.LookAtOffset = FVector(0.f, 0.f, 90.f);
	TrailerShots.Add(Shot);
}

void AOrbitCamera::ResetTrailerShotsToDefault()
{
	TrailerShots.Empty();
	InitDefaultTrailerShots();
	ApplyShot(InstanceShotIndex, false);
	UpdateOrbitTransform();
}

void AOrbitCamera::ApplyFullPresetFromInstanceShot()
{
	ApplyShot(InstanceShotIndex, true);
}

void AOrbitCamera::ApplyShot(int32 ShotIndex, bool bApplyGeometry)
{
	if (!TrailerShots.IsValidIndex(ShotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("OrbitCamera: invalid shot index %d (have %d shots)"), ShotIndex, TrailerShots.Num());
		return;
	}

	const FOrbitCameraShot& Shot = TrailerShots[ShotIndex];
	CurrentShotIndex = ShotIndex;
	LastAppliedShotIndex = ShotIndex;

	// Only the orbit angle comes from the shot index (unless you use Apply Full Preset).
	OrbitAngleDegrees = Shot.OrbitAngleDegrees;
	StartAngleDegrees = Shot.OrbitAngleDegrees;

	if (bApplyGeometry)
	{
		OrbitRadius = Shot.OrbitRadius;
		OrbitHeight = Shot.OrbitHeight;
		LookAtOffset = Shot.LookAtOffset;
		PitchAngle = Shot.PitchAngle;
		bTrailerOrbit = Shot.bTrailerOrbit;

		// Motion flags only when applying the full preset — not when syncing angle from shot index.
		if (Shot.bStaticShot)
		{
			bAutoAdvanceAngle = false;
			bDriveFromSequenceTime = false;
		}
		else if (Shot.bShotOverridesOrbitMotion)
		{
			bAutoAdvanceAngle = Shot.bShotAutoAdvanceAngle;
			OrbitSpeedDegrees = Shot.ShotOrbitSpeedDegrees;
			bDriveFromSequenceTime = false;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("OrbitCamera: shot %d '%s' angle=%.0f radius=%.0f"),
		ShotIndex, *Shot.ShotName.ToString(), Shot.OrbitAngleDegrees, Shot.OrbitRadius);

	EnsureTargetResolved();
	UpdateOrbitTransform();
}

void AOrbitCamera::SwitchToNextShot()
{
	if (TrailerShots.Num() == 0)
	{
		return;
	}

	const int32 NextIndex = (CurrentShotIndex + 1) % TrailerShots.Num();
	ApplyShot(NextIndex, true);
}

void AOrbitCamera::SwitchToPreviousShot()
{
	if (TrailerShots.Num() == 0)
	{
		return;
	}

	const int32 PrevIndex = (CurrentShotIndex - 1 + TrailerShots.Num()) % TrailerShots.Num();
	ApplyShot(PrevIndex, true);
}

int32 AOrbitCamera::GetShotCount() const
{
	return TrailerShots.Num();
}

FName AOrbitCamera::GetCurrentShotName() const
{
	if (TrailerShots.IsValidIndex(CurrentShotIndex))
	{
		return TrailerShots[CurrentShotIndex].ShotName;
	}
	return NAME_None;
}

void AOrbitCamera::SyncShotFromIndex()
{
	if (TrailerShots.Num() == 0)
	{
		return;
	}

	if (CurrentShotIndex != LastAppliedShotIndex)
	{
		// Only change angle — keep per-camera radius/height on the level.
		ApplyShot(CurrentShotIndex, false);
	}
}

const FOrbitCameraShot* AOrbitCamera::GetActiveShot() const
{
	if (TrailerShots.IsValidIndex(CurrentShotIndex))
	{
		return &TrailerShots[CurrentShotIndex];
	}
	if (TrailerShots.IsValidIndex(InstanceShotIndex))
	{
		return &TrailerShots[InstanceShotIndex];
	}
	return nullptr;
}

void AOrbitCamera::UpdateOrbit(float DeltaTime)
{
	BindSequencePlayers();

	if (bAutoSetViewTarget && bLockViewTarget)
	{
		ActivateAsViewTarget();
	}

	if (!bOrbitEnabled)
	{
		return;
	}

	EnsureTargetResolved();
	SyncShotFromIndex();

	// If we sync while the Level Sequence Actor is stopped (Play only in Sequencer tab),
	// timeline time stays at 0 and the camera freezes. Only use timeline when it is playing.
	bool bSyncedFromSequence = false;
	if (bOrbitDrivenBySequenceThisFrame)
	{
		bSyncedFromSequence = true;
	}
	else
	{
		if (!CachedLevelSequencePlayer.IsValid())
		{
			RefreshCachedSequencePlayer();
		}

		if (UMovieSceneSequencePlayer* Player = CachedLevelSequencePlayer.Get())
		{
			if (IsSequencePlayerActive(Player))
			{
				bSyncedFromSequence = TrySyncOrbitFromSequencePlayer(Player);
			}
		}
	}

	bOrbitDrivenBySequenceThisFrame = false;

	if (!bSyncedFromSequence)
	{
		AdvanceOrbitAngle(DeltaTime);
		AdvanceDolly(DeltaTime);
	}

	UpdateOrbitTransform();
}

void AOrbitCamera::AdvanceDolly(float DeltaTime)
{
	if (!bDollyEnabled || bDollyDriveFromAlpha)
	{
		return;
	}

	if (DollyCycleSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	DollyTimeAccum += DeltaTime;

	const float Phase = FMath::Fmod(DollyTimeAccum, DollyCycleSeconds) / DollyCycleSeconds;

	if (bDollyPingPong)
	{
		// 0 -> 1 -> 0
		DollyAlpha = Phase < 0.5f ? Phase * 2.f : 2.f - Phase * 2.f;
	}
	else
	{
		// 0 -> 1, loop
		DollyAlpha = Phase;
	}
}

float AOrbitCamera::GetEffectiveOrbitRadius() const
{
	if (!bDollyEnabled)
	{
		return OrbitRadius;
	}

	const float Alpha = FMath::Clamp(DollyAlpha, 0.f, 1.f);
	const float Smoothed = bDollySmooth ? FMath::SmoothStep(0.f, 1.f, Alpha) : Alpha;
	return FMath::Lerp(DollyMinRadius, DollyMaxRadius, Smoothed);
}

void AOrbitCamera::EnsureTargetResolved()
{
	if (IsValid(TargetActor) || bUseManualLookAtPoint)
	{
		return;
	}

	ResolveTarget();
}

void AOrbitCamera::AdvanceOrbitAngle(float DeltaTime)
{
	float SequenceAngle = 0.f;
	if (TrailerShots.Num() == 0 && bDriveFromSequenceTime && TryGetAngleFromSequenceTime(SequenceAngle))
	{
		OrbitAngleDegrees = SequenceAngle;
		return;
	}

	if (!bAutoAdvanceAngle)
	{
		return;
	}

	OrbitAngleDegrees += OrbitSpeedDegrees * DeltaTime;
	if (OrbitAngleDegrees >= 360.f)
	{
		OrbitAngleDegrees = FMath::Fmod(OrbitAngleDegrees, 360.f);
	}
}

bool AOrbitCamera::TrySyncOrbitFromSequencePlayer(const UMovieSceneSequencePlayer* Player)
{
	if (!bOrbitEnabled || !IsValid(Player))
	{
		return false;
	}

	if (!(bDriveFromSequenceTime || bAutoAdvanceAngle) || OrbitSpeedDegrees <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float TimeSeconds = static_cast<float>(Player->GetCurrentTime().AsSeconds());
	ApplyOrbitFromTimeSeconds(TimeSeconds);

	if (bDollyEnabled && !bDollyDriveFromAlpha && DollyCycleSeconds > KINDA_SMALL_NUMBER)
	{
		const float Phase = FMath::Fmod(TimeSeconds, DollyCycleSeconds) / DollyCycleSeconds;
		if (bDollyPingPong)
		{
			DollyAlpha = Phase < 0.5f ? Phase * 2.f : 2.f - Phase * 2.f;
		}
		else
		{
			DollyAlpha = Phase;
		}
	}

	return true;
}

bool AOrbitCamera::TrySyncOrbitFromSequenceTime()
{
	if (!CachedLevelSequencePlayer.IsValid())
	{
		RefreshCachedSequencePlayer();
	}

	return TrySyncOrbitFromSequencePlayer(CachedLevelSequencePlayer.Get());
}

bool AOrbitCamera::TryGetAngleFromSequenceTime(float& OutAngleDegrees) const
{
	UMovieSceneSequencePlayer* Player = const_cast<AOrbitCamera*>(this)->CachedLevelSequencePlayer.Get();
	if (!IsValid(Player))
	{
		const_cast<AOrbitCamera*>(this)->RefreshCachedSequencePlayer();
		Player = const_cast<AOrbitCamera*>(this)->CachedLevelSequencePlayer.Get();
	}

	if (!IsValid(Player) || !IsSequencePlayerActive(Player))
	{
		return false;
	}

	OutAngleDegrees = FMath::Fmod(
		StartAngleDegrees + static_cast<float>(Player->GetCurrentTime().AsSeconds()) * OrbitSpeedDegrees,
		360.f);
	return true;
}

float AOrbitCamera::GetFullOrbitDurationSeconds() const
{
	if (OrbitSpeedDegrees <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	return 360.f / OrbitSpeedDegrees;
}

void AOrbitCamera::SetOrbitAngleDegrees(float AngleDegrees)
{
	OrbitAngleDegrees = AngleDegrees;
	if (bOrbitEnabled)
	{
		UpdateOrbitTransform();
	}
}

void AOrbitCamera::SetTarget(AActor* NewTarget)
{
	TargetActor = NewTarget;
	bUseManualLookAtPoint = false;
}

void AOrbitCamera::SetOrbitEnabled(bool bEnabled)
{
	bOrbitEnabled = bEnabled;
}

void AOrbitCamera::ResolveTarget()
{
	if (IsValid(TargetActor))
	{
		bUseManualLookAtPoint = false;
		return;
	}

	TArray<AActor*> TaggedActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), TargetTag, TaggedActors);
	if (TaggedActors.Num() > 0)
	{
		TargetActor = TaggedActors[0];
		bUseManualLookAtPoint = false;
		UE_LOG(LogTemp, Log, TEXT("OrbitCamera: found target by tag '%s' -> %s"), *TargetTag.ToString(), *TargetActor->GetName());
		return;
	}

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == this)
		{
			continue;
		}

		if (Actor->GetName().StartsWith(TEXT("CarRoot")))
		{
			TargetActor = Actor;
			bUseManualLookAtPoint = false;
			UE_LOG(LogTemp, Log, TEXT("OrbitCamera: found target by name -> %s"), *TargetActor->GetName());
			return;
		}
	}

	if (AutoFindNameContains.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("OrbitCamera: no target found. Add tag '%s' to CarRoot or place CarRoot actor."), *TargetTag.ToString());
		return;
	}

	FBox CombinedBounds(ForceInit);
	bool bFoundAny = false;

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == this)
		{
			continue;
		}

		if (!Actor->GetName().Contains(AutoFindNameContains, ESearchCase::IgnoreCase))
		{
			continue;
		}

		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(true, Origin, Extent, false);
		CombinedBounds += FBox::BuildAABB(Origin, Extent);
		bFoundAny = true;
	}

	if (bFoundAny)
	{
		ManualLookAtPoint = CombinedBounds.GetCenter();
		bUseManualLookAtPoint = true;
		UE_LOG(LogTemp, Log, TEXT("OrbitCamera: using BMW bounds center at %s"), *ManualLookAtPoint.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("OrbitCamera: no target found."));
	}
}

FVector AOrbitCamera::GetLookAtLocation() const
{
	if (IsValid(TargetActor))
	{
		return TargetActor->GetActorLocation() + LookAtOffset;
	}

	if (bUseManualLookAtPoint)
	{
		return ManualLookAtPoint + LookAtOffset;
	}

	return GetActorLocation() + LookAtOffset;
}

void AOrbitCamera::UpdateOrbitTransform()
{
	if (!Camera)
	{
		return;
	}

	const FVector LookAt = GetLookAtLocation();
	const float AngleRad = FMath::DegreesToRadians(OrbitAngleDegrees);
	const float EffectiveRadius = GetEffectiveOrbitRadius();

	FVector OrbitOffset;

	if (bTrailerOrbit)
	{
		// Horizontal circle around the car (trailer / showcase shot).
		OrbitOffset = FVector(
			EffectiveRadius * FMath::Cos(AngleRad),
			EffectiveRadius * FMath::Sin(AngleRad),
			OrbitHeight);
	}
	else
	{
		const float PitchRad = FMath::DegreesToRadians(PitchAngle);
		const float HorizontalDistance = EffectiveRadius * FMath::Cos(PitchRad);
		const float VerticalOffset = EffectiveRadius * FMath::Sin(PitchRad) + OrbitHeight;

		OrbitOffset = FVector(
			HorizontalDistance * FMath::Cos(AngleRad),
			HorizontalDistance * FMath::Sin(AngleRad),
			VerticalOffset);
	}

	const FVector CameraLocation = LookAt + OrbitOffset;
	const FRotator CameraRotation = bLookAtTarget
		? (LookAt - CameraLocation).Rotation()
		: Camera->GetComponentRotation();

	Camera->SetWorldLocationAndRotation(CameraLocation, CameraRotation, false, nullptr, ETeleportType::TeleportPhysics);
}

void AOrbitCamera::ActivateAsViewTarget()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	if (APawn* Pawn = PC->GetPawn())
	{
		PC->UnPossess();
		Pawn->SetActorHiddenInGame(true);
		Pawn->SetActorTickEnabled(false);
	}

	PC->SetViewTarget(this);
}
