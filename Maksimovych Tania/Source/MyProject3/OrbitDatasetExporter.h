// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MovieSceneSequencePlayer.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#endif
#include "OrbitDatasetExporter.generated.h"

class UMovieSceneSequencePlayer;
class AActor;
struct FFrameTime;

/**
 * Drop this actor on the level next to BP_OrbitCamera + BMW car.
 * It subscribes to Level Sequence Actor and writes one KITTI label file
 * per rendered frame, plus a CSV used by ffmpeg drawtext to burn the
 * coordinates onto the final video.
 */
UCLASS(Blueprintable)
class MYPROJECT3_API AOrbitDatasetExporter : public AActor
{
	GENERATED_BODY()

public:
	AOrbitDatasetExporter();

	/** Master enable / disable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	bool bExportEnabled = true;

	/**
	 * Absolute folder for KITTI labels and overlay CSV.
	 * Recommended: the same parent folder as the MRQ output directory.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	FString ExportDirectory = TEXT("C:/unreal/MyProject3/Saved/Dataset");

	/** Class name written into the first column of KITTI labels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	FString ClassLabel = TEXT("Car");

	/**
	 * Object that the labels describe (the BMW). If empty, the actor
	 * looks for an actor tagged with TargetTag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	AActor* TargetActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	FName TargetTag = TEXT("OrbitTarget");

	/**
	 * Output image resolution used to compute the camera intrinsics.
	 * Must match the resolution configured in Movie Render Queue.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	int32 ImageWidth = 1920;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	int32 ImageHeight = 1080;

	/** First frame number written into KITTI filenames (000000.txt by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	int32 StartFrameNumber = 0;

	/**
	 * When true — also export coordinates while the editor / Sequencer is
	 * scrubbing (not only during MRQ). Off by default to keep CSV clean.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dataset")
	bool bExportInEditorPreview = false;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dataset")
	void ClearOutputFolder();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dataset")
	void ExportCurrentFrameNow();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	int32 FrameCounter = 0;
	bool bWroteCalibration = false;
	TSet<TWeakObjectPtr<UMovieSceneSequencePlayer>> BoundSequencePlayers;

#if WITH_EDITOR
	FTSTicker::FDelegateHandle EditorTickerHandle;
	void RegisterEditorTicker();
	void UnregisterEditorTicker();
	bool EditorTickerCallback(float DeltaTime);
#endif

	void BindSequencePlayers();
	void UnbindSequencePlayers();
	void OnSequencePlayerUpdated(
		const UMovieSceneSequencePlayer& Player,
		FFrameTime CurrentTime,
		FFrameTime PreviousTime);

	AActor* ResolveTarget();
	AActor* FindActiveCamera() const;

	void EnsureDirectory();
	void WriteCalibrationFile(float FieldOfViewDegrees);
	void WriteFrameLabel(int32 FrameIndex, AActor* Camera, AActor* Target);
	void AppendOverlayCsv(
		int32 FrameIndex,
		const FVector& CameraLocation,
		const FRotator& CameraRotation,
		const FVector& TargetLocation,
		float OrbitAngleDegrees);
};
