// Fill out your copyright notice in the Description page of Project Settings.

#include "OrbitDatasetExporter.h"

#include "OrbitCamera.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

AOrbitDatasetExporter::AOrbitDatasetExporter()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AOrbitDatasetExporter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (IsTemplate())
	{
		return;
	}

#if WITH_EDITOR
	RegisterEditorTicker();
#endif
	BindSequencePlayers();
}

void AOrbitDatasetExporter::BeginPlay()
{
	Super::BeginPlay();

	FrameCounter = 0;
	bWroteCalibration = false;
	BindSequencePlayers();
}

void AOrbitDatasetExporter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindSequencePlayers();
#if WITH_EDITOR
	UnregisterEditorTicker();
#endif
	Super::EndPlay(EndPlayReason);
}

void AOrbitDatasetExporter::BeginDestroy()
{
	UnbindSequencePlayers();
#if WITH_EDITOR
	UnregisterEditorTicker();
#endif
	Super::BeginDestroy();
}

#if WITH_EDITOR
void AOrbitDatasetExporter::RegisterEditorTicker()
{
	if (!GIsEditor || EditorTickerHandle.IsValid())
	{
		return;
	}

	EditorTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &AOrbitDatasetExporter::EditorTickerCallback));
}

void AOrbitDatasetExporter::UnregisterEditorTicker()
{
	if (EditorTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorTickerHandle);
		EditorTickerHandle.Reset();
	}
}

bool AOrbitDatasetExporter::EditorTickerCallback(float /*DeltaTime*/)
{
	if (!IsValid(this))
	{
		return true;
	}

	BindSequencePlayers();
	return true;
}
#endif

void AOrbitDatasetExporter::BindSequencePlayers()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ALevelSequenceActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		ULevelSequencePlayer* Player = ActorIt->GetSequencePlayer();
		if (!IsValid(Player) || BoundSequencePlayers.Contains(Player))
		{
			continue;
		}

		Player->OnSequenceUpdated().AddUObject(this, &AOrbitDatasetExporter::OnSequencePlayerUpdated);
		BoundSequencePlayers.Add(Player);
	}
}

void AOrbitDatasetExporter::UnbindSequencePlayers()
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

void AOrbitDatasetExporter::OnSequencePlayerUpdated(
	const UMovieSceneSequencePlayer& Player,
	FFrameTime CurrentTime,
	FFrameTime /*PreviousTime*/)
{
	if (!bExportEnabled)
	{
		return;
	}

	if (!bExportInEditorPreview)
	{
		// Only write while the player actually runs the timeline (Play, MRQ).
		if (!(Player.IsPlaying() || Player.IsPaused()))
		{
			return;
		}
	}

	AActor* Target = ResolveTarget();
	AActor* CameraActor = FindActiveCamera();
	if (!IsValid(Target) || !IsValid(CameraActor))
	{
		return;
	}

	const int32 FrameIndex = StartFrameNumber + FrameCounter;
	WriteFrameLabel(FrameIndex, CameraActor, Target);
	++FrameCounter;
}

void AOrbitDatasetExporter::ExportCurrentFrameNow()
{
	AActor* Target = ResolveTarget();
	AActor* CameraActor = FindActiveCamera();
	if (!IsValid(Target) || !IsValid(CameraActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("OrbitDataset: target or camera not found."));
		return;
	}

	WriteFrameLabel(StartFrameNumber + FrameCounter, CameraActor, Target);
	++FrameCounter;
}

void AOrbitDatasetExporter::ClearOutputFolder()
{
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (Files.DirectoryExists(*ExportDirectory))
	{
		Files.DeleteDirectoryRecursively(*ExportDirectory);
	}
	FrameCounter = 0;
	bWroteCalibration = false;
}

AActor* AOrbitDatasetExporter::ResolveTarget()
{
	if (IsValid(TargetActor))
	{
		return TargetActor;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<AActor*> Tagged;
	UGameplayStatics::GetAllActorsWithTag(World, TargetTag, Tagged);
	if (Tagged.Num() > 0)
	{
		TargetActor = Tagged[0];
		return TargetActor;
	}

	return nullptr;
}

AActor* AOrbitDatasetExporter::FindActiveCamera() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (AActor* ViewTarget = PC->GetViewTarget())
		{
			if (IsValid(ViewTarget) && !ViewTarget->IsA<APlayerController>())
			{
				return ViewTarget;
			}
		}
	}

	// Editor preview fallback: first AOrbitCamera on the level.
	for (TActorIterator<AOrbitCamera> It(World); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

void AOrbitDatasetExporter::EnsureDirectory()
{
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();

	const FString LabelDir = FPaths::Combine(ExportDirectory, TEXT("label_2"));
	const FString CalibDir = FPaths::Combine(ExportDirectory, TEXT("calib"));

	Files.CreateDirectoryTree(*LabelDir);
	Files.CreateDirectoryTree(*CalibDir);
}

void AOrbitDatasetExporter::WriteCalibrationFile(float FieldOfViewDegrees)
{
	if (bWroteCalibration)
	{
		return;
	}

	const float Width = static_cast<float>(ImageWidth);
	const float Height = static_cast<float>(ImageHeight);
	const float HFovRad = FMath::DegreesToRadians(FieldOfViewDegrees);
	const float Fx = (Width * 0.5f) / FMath::Tan(HFovRad * 0.5f);
	const float Fy = Fx; // square pixels
	const float Cx = Width * 0.5f;
	const float Cy = Height * 0.5f;

	// KITTI calib stores P2 = [fx 0 cx 0; 0 fy cy 0; 0 0 1 0]
	const FString Line = FString::Printf(
		TEXT("P2: %.6f 0.000000 %.6f 0.000000 0.000000 %.6f %.6f 0.000000 0.000000 0.000000 1.000000 0.000000"),
		Fx, Cx, Fy, Cy);

	const FString Path = FPaths::Combine(ExportDirectory, TEXT("calib"), TEXT("calib.txt"));
	FFileHelper::SaveStringToFile(Line, *Path);
	bWroteCalibration = true;
}

void AOrbitDatasetExporter::AppendOverlayCsv(
	int32 FrameIndex,
	const FVector& CameraLocation,
	const FRotator& CameraRotation,
	const FVector& TargetLocation,
	float OrbitAngleDegrees)
{
	const FString CsvPath = FPaths::Combine(ExportDirectory, TEXT("overlay.csv"));

	FString Existing;
	if (!FPaths::FileExists(CsvPath))
	{
		Existing = TEXT("frame,image,cam_x,cam_y,cam_z,cam_pitch,cam_yaw,cam_roll,car_x,car_y,car_z,orbit_angle\r\n");
	}
	else
	{
		FFileHelper::LoadFileToString(Existing, *CsvPath);
	}

	const FString ImageName = FString::Printf(TEXT("%06d.png"), FrameIndex);
	Existing += FString::Printf(
		TEXT("%d,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\r\n"),
		FrameIndex,
		*ImageName,
		CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
		CameraRotation.Pitch, CameraRotation.Yaw, CameraRotation.Roll,
		TargetLocation.X, TargetLocation.Y, TargetLocation.Z,
		OrbitAngleDegrees);

	FFileHelper::SaveStringToFile(Existing, *CsvPath);
}

void AOrbitDatasetExporter::WriteFrameLabel(int32 FrameIndex, AActor* CameraActor, AActor* Target)
{
	EnsureDirectory();

	// Resolve the active camera component (UCameraComponent) for FOV.
	UCameraComponent* CameraComp = nullptr;
	if (AOrbitCamera* Orbit = Cast<AOrbitCamera>(CameraActor))
	{
		CameraComp = Orbit->Camera;
	}
	else
	{
		CameraComp = CameraActor->FindComponentByClass<UCameraComponent>();
	}

	const float FovDeg = CameraComp ? CameraComp->FieldOfView : 90.f;
	WriteCalibrationFile(FovDeg);

	// Camera world transform — use the camera component if available,
	// otherwise fall back to the actor transform.
	const FTransform CameraTransform = CameraComp
		? CameraComp->GetComponentTransform()
		: CameraActor->GetActorTransform();

	const FVector CameraLocation = CameraTransform.GetLocation();
	const FRotator CameraRotation = CameraTransform.Rotator();

	// World AABB of the target.
	FVector TargetOrigin;
	FVector TargetExtent;
	Target->GetActorBounds(false, TargetOrigin, TargetExtent, true);

	// 8 corners of the world AABB.
	TArray<FVector> Corners;
	Corners.Reserve(8);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float SignX = (Index & 1) ? 1.f : -1.f;
		const float SignY = (Index & 2) ? 1.f : -1.f;
		const float SignZ = (Index & 4) ? 1.f : -1.f;
		Corners.Add(TargetOrigin + FVector(
			SignX * TargetExtent.X,
			SignY * TargetExtent.Y,
			SignZ * TargetExtent.Z));
	}

	// World -> UE camera local. UE camera looks down +X.
	const FTransform InvCamera = CameraTransform.Inverse();

	// Convert UE camera-local (X fwd, Y right, Z up) -> KITTI cam (X right, Y down, Z fwd).
	// Then project to pixel coordinates.
	const float Width = static_cast<float>(ImageWidth);
	const float Height = static_cast<float>(ImageHeight);
	const float HFovRad = FMath::DegreesToRadians(FovDeg);
	const float Fx = (Width * 0.5f) / FMath::Tan(HFovRad * 0.5f);
	const float Fy = Fx;
	const float Cx = Width * 0.5f;
	const float Cy = Height * 0.5f;

	float MinU = TNumericLimits<float>::Max();
	float MinV = TNumericLimits<float>::Max();
	float MaxU = -TNumericLimits<float>::Max();
	float MaxV = -TNumericLimits<float>::Max();
	bool bAnyInFront = false;

	for (const FVector& WorldCorner : Corners)
	{
		const FVector UECam = InvCamera.TransformPosition(WorldCorner);

		// Reject corners behind the camera.
		if (UECam.X <= 1.f)
		{
			continue;
		}
		bAnyInFront = true;

		const float Kx = static_cast<float>(UECam.Y);
		const float Ky = -static_cast<float>(UECam.Z);
		const float Kz = static_cast<float>(UECam.X);

		const float U = Fx * Kx / Kz + Cx;
		const float V = Fy * Ky / Kz + Cy;

		MinU = FMath::Min(MinU, U);
		MinV = FMath::Min(MinV, V);
		MaxU = FMath::Max(MaxU, U);
		MaxV = FMath::Max(MaxV, V);
	}

	if (!bAnyInFront)
	{
		// Target is fully behind the camera — write empty label (DontCare).
		const FString EmptyLine = FString::Printf(TEXT("DontCare -1 -1 -10 0.00 0.00 0.00 0.00 -1 -1 -1 -1000 -1000 -1000 -10\r\n"));
		const FString Path = FPaths::Combine(
			ExportDirectory, TEXT("label_2"),
			FString::Printf(TEXT("%06d.txt"), FrameIndex));
		FFileHelper::SaveStringToFile(EmptyLine, *Path);
		return;
	}

	// Clamp bbox to image and compute truncation ratio.
	const float ClampedMinU = FMath::Clamp(MinU, 0.f, Width - 1.f);
	const float ClampedMinV = FMath::Clamp(MinV, 0.f, Height - 1.f);
	const float ClampedMaxU = FMath::Clamp(MaxU, 0.f, Width - 1.f);
	const float ClampedMaxV = FMath::Clamp(MaxV, 0.f, Height - 1.f);

	const float FullArea = FMath::Max(1.f, (MaxU - MinU) * (MaxV - MinV));
	const float VisibleArea = FMath::Max(0.f,
		(ClampedMaxU - ClampedMinU) * (ClampedMaxV - ClampedMinV));
	const float Truncation = FMath::Clamp(1.f - (VisibleArea / FullArea), 0.f, 1.f);

	// 3D location in KITTI camera coords (meters; UE units are cm by default).
	const FVector TargetUE = InvCamera.TransformPosition(TargetOrigin);
	const float LocX = static_cast<float>(TargetUE.Y) * 0.01f;
	const float LocY = -static_cast<float>(TargetUE.Z) * 0.01f;
	const float LocZ = static_cast<float>(TargetUE.X) * 0.01f;

	// Dimensions: KITTI = height (Z), width (Y), length (X) in meters.
	const float DimH = TargetExtent.Z * 2.f * 0.01f;
	const float DimW = TargetExtent.Y * 2.f * 0.01f;
	const float DimL = TargetExtent.X * 2.f * 0.01f;

	// Rotation around Y in camera coords.
	const float TargetYawWorld = Target->GetActorRotation().Yaw;
	const float CameraYawWorld = CameraRotation.Yaw;
	const float RelativeYawDeg = FRotator::NormalizeAxis(TargetYawWorld - CameraYawWorld);
	const float RotationY = FMath::DegreesToRadians(-RelativeYawDeg);

	// Alpha: observation angle.
	const float Beta = FMath::Atan2(LocX, LocZ);
	const float Alpha = FRotator::NormalizeAxis(
		FMath::RadiansToDegrees(RotationY - Beta));

	const FString Line = FString::Printf(
		TEXT("%s %.2f 0 %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\r\n"),
		*ClassLabel,
		Truncation,
		FMath::DegreesToRadians(Alpha),
		ClampedMinU, ClampedMinV, ClampedMaxU, ClampedMaxV,
		DimH, DimW, DimL,
		LocX, LocY, LocZ,
		RotationY);

	const FString Path = FPaths::Combine(
		ExportDirectory, TEXT("label_2"),
		FString::Printf(TEXT("%06d.txt"), FrameIndex));
	FFileHelper::SaveStringToFile(Line, *Path);

	float OrbitAngle = 0.f;
	if (AOrbitCamera* Orbit = Cast<AOrbitCamera>(CameraActor))
	{
		OrbitAngle = Orbit->OrbitAngleDegrees;
	}

	AppendOverlayCsv(
		FrameIndex,
		CameraLocation,
		CameraRotation,
		TargetOrigin,
		OrbitAngle);
}
