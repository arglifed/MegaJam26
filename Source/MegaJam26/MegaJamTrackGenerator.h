// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MegaJamGameMode.h"
#include "MegaJamTypes.h"
#include "MegaJamTrackGenerator.generated.h"

class AMegaJamFlowPawn;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

/** Broadcast when the track generator transitions into a new film scene. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFilmSceneChanged, EMegaJamFilmScene, NewScene);

/** Runtime state for a pooled collectible Film Frame pickup instance. */
USTRUCT()
struct FMegaJamFilmFramePickup
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FVector Scale = FVector(1.2f, 1.2f, 1.2f);

	UPROPERTY()
	EMegaJamTakeState RequiredTake = EMegaJamTakeState::StudioTake;

	UPROPERTY()
	int32 PoolIndex = 0;

	UPROPERTY()
	bool bActive = false;
};

/** Runtime state for a pooled set-piece instance (supports continuous rotation in Scene 3). */
USTRUCT()
struct FMegaJamSetPieceSlot
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform BaseTransform = FTransform::Identity;

	UPROPERTY()
	bool bRotates = false;

	UPROPERTY()
	float RotationSpeedDeg = 45.0f;

	UPROPERTY()
	bool bActive = false;
};

/**
 * Procedural 4-Act corridor for SPLICE // DIRECTOR'S CUT.
 * Manages pooled InstancedStaticMeshComponents for Floor, Walls, Take A / Take B Obstacles (tailored per Act),
 * 4-Scene Set Pieces, Aspect-Locked Collectible Film Frames, and the 148s Golden Final Cut Wrap Arch.
 */
UCLASS()
class MEGAJAM26_API AMegaJamTrackGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMegaJamTrackGenerator();

	virtual void Tick(float DeltaSeconds) override;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USceneComponent> TrackRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> FloorISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> WallISM;

	/** Obstacles that are solid in StudioTake. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> TakeA_ObstaclesISM;

	/** Obstacles that are solid in DirectorsTake. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> TakeB_ObstaclesISM;

	/** Non-colliding 4-scene architectural set dressing pooled per chunk. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> SetPieceISM;

	/** Non-colliding aspect-locked collectible Film Frames (Studio Take A). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> PickupISM;

	/** Non-colliding aspect-locked collectible Film Frames (Director's Take B). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> PickupTakeB_ISM;

	/** Celebratory golden FINAL CUT WRAP ARCH spawned across the corridor at 148.0s. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> WrapArchISM;

	// ---- Modular Art Mesh Slots (fall back to Cube / Cylinder when null) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> ObstacleTakeAMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> ObstacleTakeBMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> FloorTileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> WallSegmentMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> SoundstageTrussMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> SkyscraperMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> FilmReelMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> ApertureRingMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> FilmFramePickupMesh;

	// ---- Track tunables ----
	/** Length of one chunk along +X (cm). Increased +30% (2600 cm) for clear reaction sightlines between rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float ChunkLength = 2600.0f;

	/** Chunks kept active ahead of the pawn's chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 ChunksAhead = 15;

	/** Chunks kept behind the pawn's chunk before they are recycled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 ChunksBehind = 1;

	/** Obstacle rows per chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 RowsPerChunk = 4;

	/** Chunks ahead of the pawn left obstacle-free on start or checkpoint rewind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 SafeChunks = 2;

	/** Deterministic layout seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 Seed = 1337;

	/** Half width of the corridor (cm). Walls are centered on +/- this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float CorridorHalfWidth = 1000.0f;

	/** Probability that a row holds an obstacle at density scale 1 (Act IV, full flow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float MaxObstacleDensity = 0.80f;

	/** Obstacles are placed within +/- this lateral range (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float ObstacleLateralRange = 700.0f;

	/** Obstacle footprint (X depth and Y width, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float ObstacleSize = 200.0f;

	/** Ground obstacle height range (cm), standing on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	FVector2D ObstacleHeightRange = FVector2D(150.0, 300.0);

	/** High-altitude air obstacle Z center range (cm) to challenge Box_4_3 vertical flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	FVector2D AirObstacleZRange = FVector2D(320.0, 820.0);

	/** Collection distance (cm) for Aspect-Locked Film Frame pickups. Generous radius so pickups snag cleanly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float PickupCollectionRadius = 215.0f;

	/** Assumed speed (cm/s) when projecting when a chunk will be reached, to pick its scene. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float MinProjectionSpeed = 2600.0f;

	// ---- Scenes ----
	/** Run time (s) at which each film scene ends: Soundstage, Overpass Night, Cutting Room, Final Cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Scenes")
	TArray<float> SceneEndTimes = { 35.0f, 75.0f, 115.0f, 150.0f };

	UFUNCTION(BlueprintPure, Category="MegaJam|Scenes")
	EMegaJamFilmScene GetCurrentScene() const { return CurrentScene; }

	/** Multicast delegate for C++ / HUD listeners when the run enters a new film scene. */
	UPROPERTY(BlueprintAssignable, Category="MegaJam|Scenes")
	FOnFilmSceneChanged OnFilmSceneChanged;

	/** Blueprint hook (lighting, props, title card) when the run enters a new film scene. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Scenes")
	void OnSceneChanged(EMegaJamFilmScene NewScene);

	// ---- Visuals (soft defaults; override in a Blueprint subclass) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Visuals")
	TObjectPtr<UStaticMesh> BlockMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Visuals")
	TObjectPtr<UStaticMesh> CylinderMesh;

	/** Defaults to /Game/MegaJam26/VFX/M_TakeGhost when that asset exists. Needs scalar TakeIndex and vector TakeColor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Visuals")
	TObjectPtr<UMaterialInterface> ObstacleMaterial;

	/** Soft fallback used when ObstacleMaterial is not assigned. Loaded in BeginPlay only if the package exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Visuals")
	TSoftObjectPtr<UMaterialInterface> DefaultObstacleMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MegaJam26/VFX/M_TakeGhost.M_TakeGhost")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Visuals")
	FLinearColor TakeAColor = FLinearColor(1.0f, 0.5f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Visuals")
	FLinearColor TakeBColor = FLinearColor(0.1f, 0.8f, 1.0f, 1.0f);

	/** Re-lays every chunk around the pawn's current position and CurrentCheckpointTime. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Track")
	void RebuildTrack();

	/** Alias for RebuildTrack to support ResetTrack calls. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Track")
	void ResetTrack() { RebuildTrack(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Toggles collision between the two obstacle pools. */
	UFUNCTION()
	void HandleTakeCut(EMegaJamTakeState NewTake);

	UFUNCTION()
	void HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState);

private:
	static constexpr int32 ObstaclesPerRow = 3;
	static constexpr int32 SetPiecesPerChunk = 4;
	static constexpr int32 PickupsPerChunk = 2;
	static constexpr int32 WrapArchParts = 5;

	int32 PoolSize() const { return FMath::Max(1, ChunksAhead + ChunksBehind); }
	int32 SlotOf(int32 ChunkIndex) const;
	int32 SceneIndexAtTime(float RunTimeSeconds) const;
	float GetRunTimeSeconds() const;

	void ApplyTakeCollision(EMegaJamTakeState Take);
	void BindToPawn();
	void CreatePools();
	void PopulateChunk(int32 ChunkIndex);
	void PopulateSetPiecesForChunk(int32 Slot, int32 ChunkIndex, float ChunkStartX, float ChunkCenterX, EMegaJamFilmScene ChunkScene, FRandomStream& Stream);
	void PopulatePickupsForChunk(int32 Slot, int32 ChunkIndex, float ChunkStartX, EMegaJamFilmScene ChunkScene, FRandomStream& Stream);
	void UpdateDynamicAnimationsAndPickups(float DeltaSeconds);
	void UpdateSceneMaterials(float DeltaSeconds);
	void UpdateFinalCutWrapArch();
	void HideFinalCutWrapArch();
	FLinearColor GetSceneAccentColor(EMegaJamFilmScene Scene) const;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamFlowPawn> BoundPawn;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamGameMode> BoundGameMode;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SetPieceMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WrapArchMID;

	UPROPERTY(Transient)
	TArray<FMegaJamSetPieceSlot> SetPieceSlots;

	UPROPERTY(Transient)
	TArray<FMegaJamFilmFramePickup> PickupSlots;

	bool bPoolsCreated = false;
	bool bBuilt = false;
	bool bWrapArchSpawned = false;
	int32 FirstChunkIndex = 0;
	int32 SafeUntilChunkIndex = 2;
	EMegaJamFilmScene CurrentScene = EMegaJamFilmScene::Soundstage;
	int32 LastBroadcastScene = -1;
	FLinearColor CurrentSceneColor = FLinearColor(1.0f, 0.72f, 0.35f, 1.0f);
};
