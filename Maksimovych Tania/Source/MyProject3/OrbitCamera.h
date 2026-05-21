// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MovieSceneSequencePlayer.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#endif
#include "OrbitCamera.generated.h"

class UCameraComponent;
class USceneComponent;
class UOrbitCameraDriverComponent;
class UMovieSceneSequencePlayer;
struct FFrameTime;

/** One trailer frame / camera angle. */
USTRUCT(BlueprintType)
struct FOrbitCameraShot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	FName ShotName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	float OrbitAngleDegrees = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot", meta = (ClampMin = "50.0"))
	float OrbitRadius = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	float OrbitHeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	FVector LookAtOffset = FVector(0.f, 0.f, 90.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	float PitchAngle = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	bool bTrailerOrbit = true;

	/** Static frame (no spin) vs moving shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	bool bStaticShot = false;

	/** If true, switching to this shot also sets Auto Advance / Orbit Speed on the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot")
	bool bShotOverridesOrbitMotion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot", meta = (EditCondition = "bShotOverridesOrbitMotion"))
	bool bShotAutoAdvanceAngle = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shot", meta = (ClampMin = "0.0", EditCondition = "bShotOverridesOrbitMotion"))
	float ShotOrbitSpeedDegrees = 5.f;
};

/** Ticks in editor so Sequencer preview can move the orbit without PIE. */
UCLASS()
class MYPROJECT3_API UOrbitCameraDriverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOrbitCameraDriverComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

UCLASS(Blueprintable)
class MYPROJECT3_API AOrbitCamera : public AActor
{
	GENERATED_BODY()

public:
	AOrbitCamera();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	UCameraComponent* Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	UOrbitCameraDriverComponent* OrbitDriver;

	/**
	 * Car to orbit. Set on the placed BP_OrbitCamera in the level (not in Blueprint defaults).
	 * Leave empty to use Target Tag or Auto Find Name Contains.
	 */
	UPROPERTY(
		EditInstanceOnly,
		BlueprintReadWrite,
		Category = "Orbit|Target",
		meta = (DisplayName = "Target Actor (Car Root)", ToolTip = "Select CarRoot from the level Outliner or use the eyedropper."))
	AActor* TargetActor;

	/** Tag on the car root actor for auto-find (easier than Target Actor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Target")
	FName TargetTag = TEXT("OrbitTarget");

	/** Fallback: find actors whose name contains this string (e.g. "bmw"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Target")
	FString AutoFindNameContains = TEXT("bmw");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "50.0"))
	float OrbitRadius = 700.f;

	/** Height offset from look-at point. 0 = same level as target (trailer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float OrbitHeight = 0.f;

	/** Horizontal trailer orbit (car level). If false, uses Pitch Angle for elevated orbit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	bool bTrailerOrbit = true;

	/** Only used when Trailer Orbit is off — tilts the orbit path up/down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "-80.0", ClampMax = "80.0", EditCondition = "!bTrailerOrbit"))
	float PitchAngle = 0.f;

	/** Keyframe 0 = freeze, higher = faster spin while Auto Advance is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Orbit", meta = (ClampMin = "0.0"))
	float OrbitSpeedDegrees = 22.f;

	/** Point on the car to look at (Z ≈ center of body). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	FVector LookAtOffset = FVector(0.f, 0.f, 90.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	bool bLookAtTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float StartAngleDegrees = 0.f;

	/** Keyframe in Sequencer: off = camera frozen for this part of the timeline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Orbit")
	bool bOrbitEnabled = true;

	// --- Sequencer / render ---

	/** Off when using Sequencer Camera Cut track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Sequencer")
	bool bAutoSetViewTarget = false;

	/** Off when using Sequencer Camera Cut track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Sequencer")
	bool bLockViewTarget = false;

	/** Force angle from sequence time (same as Auto Advance during MRQ render). Usually leave OFF. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Sequencer")
	bool bDriveFromSequenceTime = false;

	/** Keyframe: on only for the part of the timeline where you want spin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Orbit|Sequencer")
	bool bAutoAdvanceAngle = true;

	/** Keyframe this in Sequencer (Actor track → Orbit Angle Degrees). 0–360 = full circle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Orbit|Sequencer")
	float OrbitAngleDegrees = 0.f;

	// --- Dolly (zoom in/out) ---

	/** Keyframe: on only when you want dolly on this section of the timeline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Orbit|Dolly")
	bool bDollyEnabled = false;

	/** Closest distance during dolly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Dolly", meta = (ClampMin = "50.0", EditCondition = "bDollyEnabled"))
	float DollyMinRadius = 400.f;

	/** Farthest distance during dolly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Dolly", meta = (ClampMin = "50.0", EditCondition = "bDollyEnabled"))
	float DollyMaxRadius = 1100.f;

	/** Seconds for one full in→out→in cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Dolly", meta = (ClampMin = "0.1", EditCondition = "bDollyEnabled"))
	float DollyCycleSeconds = 6.f;

	/** Ping-pong (in↔out) vs continuous one-way (in→out, loops). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Dolly", meta = (EditCondition = "bDollyEnabled"))
	bool bDollyPingPong = true;

	/** Smooth easing on the dolly motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Dolly", meta = (EditCondition = "bDollyEnabled"))
	bool bDollySmooth = true;

	/** Keyframe this 0..1 in Sequencer (Actor track → Dolly Alpha) instead of auto cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Orbit|Dolly", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DollyAlpha = 0.f;

	/** If true — radius driven by Dolly Alpha keyframes. If false — auto cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Dolly", meta = (EditCondition = "bDollyEnabled"))
	bool bDollyDriveFromAlpha = false;

	/** Seconds for a full 360° at current Orbit Speed (for sequence length). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Orbit|Sequencer")
	float GetFullOrbitDurationSeconds() const;

	UFUNCTION(BlueprintCallable, Category = "Orbit")
	void SetTarget(AActor* NewTarget);

	UFUNCTION(BlueprintCallable, Category = "Orbit")
	void SetOrbitEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Orbit|Sequencer")
	void SetOrbitAngleDegrees(float AngleDegrees);

	// --- Trailer shots (multiple angles) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trailer|Shots")
	TArray<FOrbitCameraShot> TrailerShots;

	/**
	 * Which trailer preset this camera uses (saved per actor on the level).
	 * Set 0,1,2… on each BP_OrbitCamera copy — otherwise all cameras look the same.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trailer|Shots", meta = (ClampMin = "0"))
	int32 InstanceShotIndex = 0;

	/** Keyframe in Sequencer to switch shots on a single camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Trailer|Shots")
	int32 CurrentShotIndex = 0;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Trailer|Shots")
	void ResetTrailerShotsToDefault();

	/** Copy radius/height/angle from Instance Shot Index preset (after you set the index). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Trailer|Shots")
	void ApplyFullPresetFromInstanceShot();

	/** Apply shot. bApplyGeometry=true copies radius/height (use when changing Instance Shot Index). */
	UFUNCTION(BlueprintCallable, Category = "Trailer|Shots")
	void ApplyShot(int32 ShotIndex, bool bApplyGeometry = false);

	UFUNCTION(BlueprintCallable, Category = "Trailer|Shots")
	void SwitchToNextShot();

	UFUNCTION(BlueprintCallable, Category = "Trailer|Shots")
	void SwitchToPreviousShot();

	UFUNCTION(BlueprintCallable, Category = "Trailer|Shots")
	int32 GetShotCount() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Trailer|Shots")
	FName GetCurrentShotName() const;

	void UpdateOrbit(float DeltaTime);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostInterpChange(FProperty* PropertyThatChanged) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	int32 LastAppliedShotIndex = INDEX_NONE;
	float DollyTimeAccum = 0.f;
	/** Set when MRQ/Sequencer steps time — avoids double-advance with editor tick. */
	bool bOrbitDrivenBySequenceThisFrame = false;
	bool bUseManualLookAtPoint = false;
	FVector ManualLookAtPoint = FVector::ZeroVector;
	TSet<TWeakObjectPtr<UMovieSceneSequencePlayer>> BoundSequencePlayers;
	TWeakObjectPtr<UMovieSceneSequencePlayer> CachedLevelSequencePlayer;
	void RefreshCachedSequencePlayer();

#if WITH_EDITOR
	FTSTicker::FDelegateHandle EditorTickerHandle;
	void RegisterEditorTicker();
	void UnregisterEditorTicker();
	bool EditorTickerCallback(float DeltaTime);
#endif

	bool IsSequencePlayerActive(const UMovieSceneSequencePlayer* Player) const;
	bool DoesSequencePlayerMatchWorld(const UMovieSceneSequencePlayer* Player) const;
	bool IsAnySequencePlaying() const;
	void ResolveTarget();
	void EnsureTargetResolved();
	void AdvanceOrbitAngle(float DeltaTime);
	void AdvanceDolly(float DeltaTime);
	float GetEffectiveOrbitRadius() const;
	bool TryGetAngleFromSequenceTime(float& OutAngleDegrees) const;
	bool TrySyncOrbitFromSequenceTime();
	bool TrySyncOrbitFromSequencePlayer(const UMovieSceneSequencePlayer* Player);
	void BindSequencePlayers();
	void UnbindSequencePlayers();
	void OnSequencePlayerUpdated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime);
	void ApplyOrbitFromTimeSeconds(float TimeSeconds);
	void InitDefaultTrailerShots();
	void SyncShotFromIndex();
	const FOrbitCameraShot* GetActiveShot() const;
	FVector GetLookAtLocation() const;
	void SetupDetachedCamera();
	void UpdateOrbitTransform();
	void ActivateAsViewTarget();
};
