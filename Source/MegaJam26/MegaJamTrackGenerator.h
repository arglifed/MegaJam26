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
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	FVector Scale = FVector(1.2f, 1.2f, 1.2f);

	UPROPERTY()
	EMegaJamTakeState RequiredTake = EMegaJamTakeState::StudioTake;

	UPROPERTY()
	int32 PoolIndex = 0;

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	bool bHasBallisticVelocity = false;

	UPROPERTY()
	float MagnetImmunityTimer = 0.0f;
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

	/** Local axis the slot spins around when bRotates (Z = yaw, the original behaviour). */
	UPROPERTY()
	FVector SpinAxis = FVector::ZAxisVector;

	/** EMegaJamFilmScene index of the chunk that placed it: picks the per-scene art component (SetPieceSceneISMs). */
	UPROPERTY()
	int32 SceneIdx = 0;
};

/** Runtime state for an outer-wing 21:9 Dolly Boost Strip instance. */
USTRUCT()
struct FMegaJamDollyBoostStrip
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	int32 PoolIndex = 0;

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
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PreRegisterAllComponents() override;
	virtual void PostInitializeComponents() override;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USceneComponent> TrackRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> FloorISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> WallISM;

	/** Invisible full-height (10m) corridor collision walls keeping the player inside the Y = +/-700 track while WallISM renders a 2m guardrail. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> CollisionWallISM;

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

	/** Non-colliding outer-wing 21:9 Dolly Boost Strips (Y = +/-700, Z = 18) flanking Center-Median Blockades and Ceiling Gates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> BoostStripISM;

	/** Non-colliding emissive floor rails for the 5 discrete 21:9 magnetic lanes (Y = -700, -350, 0, 350, 700). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UInstancedStaticMeshComponent> LaneRailsISM;

	/** Non-colliding outer-corridor studio soundstage dressing (lighting trusses, cranes) outside Y = +/-1200. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Art")
	TObjectPtr<UInstancedStaticMeshComponent> StudioDressingISM;

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

	/**
	 * Per-scene obstacle art for Take A, indexed by scene order (0 Action, 1 Western, 2 Noir, 3 Chase, 4 Final Cut).
	 * Each mesh must be 100 x 100 x 100 with a centred pivot. When any entry (A or B) is set, obstacles are drawn by
	 * non-colliding per-scene skin components that mirror the TakeA/TakeB collision instances, which are then hidden.
	 * The mesh's own material (slot 0) is used and receives TakeIndex, TakeColor and TakeGhosting = 1.
	 * Empty entries fall back to ObstacleTakeAMesh / the cube with ObstacleMaterial.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TArray<TObjectPtr<UStaticMesh>> SceneObstacleMeshesTakeA;

	/** Per-scene obstacle art for Take B. Same rules as SceneObstacleMeshesTakeA. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TArray<TObjectPtr<UStaticMesh>> SceneObstacleMeshesTakeB;

	/** Optional one-piece finish arch. Placed once, uniformly scaled to span the corridor, instead of the 5 stretched ApertureRingMesh parts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TObjectPtr<UStaticMesh> WrapArchMesh;

	/** Optional outer-corridor dressing meshes (centred pivot). One is picked at random per spawn and rested on Z = 0. Empty = SoundstageTrussMesh as before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	TArray<TObjectPtr<UStaticMesh>> StudioDressingMeshes;

	/** Uniform scale for StudioDressingMeshes (the art is about 100 cm on its longest side). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	float StudioDressingScale = 14.0f;

	/** Per-scene outer-corridor prop scatter table (tombstones, pine trees, sedans, etc. spawned outside Y = +/-1200). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Environment")
	TMap<EMegaJamSceneGenre, FScenePropList> SceneScatterProps;

	/**
	 * When true, set pieces, pickups and the wrap arch keep the material of their assigned mesh (slot 0) instead of
	 * ObstacleMaterial. That material should expose TakeIndex / TakeColor (M_MeshyPBR does). Engine fallback shapes still use ObstacleMaterial.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Art")
	bool bUseMeshMaterials = false;

	// ---- Track tunables ----
	/** Length of one chunk along +X (cm). 3200 cm for long, clean forward sightlines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float ChunkLength = 3200.0f;

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
	float CorridorHalfWidth = MEGAJAM_CORRIDOR_HALF_WIDTH;

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

	/**
	 * Beat-Splice Harvesting: finds active-take obstacles directly ahead of PawnLocation within ForwardRange & SpliceRadius,
	 * shreds them, and spawns a 3D arc of 3 to 5 collectible Film Frames in their place. Returns the number of obstacles shredded.
	 */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Track")
	int32 SpliceObstaclesInFront(const FVector& PawnLocation, float ForwardRange = 2500.0f, float SpliceRadius = 600.0f);

	/**
	 * Scene 02 Quick-Draw Beat-Slash & Scene 03 Cryo-Shatter: cleaves all Take A / Take B barricades and obstacles overlapping
	 * [ForwardMinX, ForwardMaxX] within [CenterY - HalfWidthY, CenterY + HalfWidthY], spawning a lateral split of collectible Film Frames.
	 */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Track")
	int32 CleaveObstaclesInRange(float ForwardMinX, float ForwardMaxX, float HalfWidthY, float CenterY = 0.0f);

	/** Mathematical hurtbox & graze check against the active-take obstacle pool (allows bSweep = false on the Pawn). */
	bool CheckObstacleCollisionAndGraze(const FVector& PawnLocation, float HurtboxRadius, float GrazeRadius, bool& bOutGrazed, int32& OutGrazedInstanceIndex) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Toggles collision between the two obstacle pools. */
	UFUNCTION()
	void HandleTakeCut(EMegaJamTakeState NewTake);

	UFUNCTION()
	void HandleTakeSwapped(bool bNewTakeState);

	UFUNCTION()
	void HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState);

private:
	static constexpr int32 ObstaclesPerRow = 3;
	static constexpr int32 SetPiecesPerChunk = 4;
	static constexpr int32 PickupsPerChunk = 10;
	static constexpr int32 LaneRailsPerChunk = 5;
	static constexpr int32 BurstPickupPoolSize = 30;
	static constexpr int32 BoostStripsPerChunk = 2;
	static constexpr int32 WrapArchParts = 5;

	int32 PoolSize() const { return FMath::Max(1, ChunksAhead + ChunksBehind); }
	int32 SlotOf(int32 ChunkIndex) const;
	int32 SceneIndexAtTime(float RunTimeSeconds) const;
	float GetRunTimeSeconds() const;
	bool IsHazardChunkForScene(int32 ChunkIndex, EMegaJamFilmScene ChunkScene) const;
	int32 GetHazardArchetypeForChunk(int32 ChunkIndex) const;

	void ApplyTakeCollision(EMegaJamTakeState Take);
	void BindToPawn();
	void CreatePools();
	void PopulateChunk(int32 ChunkIndex);
	void GenerateTrackChunk(float StartX, float EndX);
	void PopulateSetPiecesForChunk(int32 Slot, int32 ChunkIndex, float ChunkStartX, float ChunkCenterX, EMegaJamFilmScene ChunkScene, FRandomStream& Stream);
	void PopulatePickupsForChunk(int32 Slot, int32 ChunkIndex, float ChunkStartX, EMegaJamFilmScene ChunkScene, bool bIsHazardChunk, int32 HazardArchetype, FRandomStream& Stream);
	void PopulateBoostStripsForChunk(int32 Slot, int32 ChunkIndex, float ChunkStartX, bool bIsHazardChunk, int32 HazardArchetype, FRandomStream& Stream);
	void UpdateDynamicAnimationsAndPickups(float DeltaSeconds);
	void UpdateSceneMaterials(float DeltaSeconds);
	void UpdateFinalCutWrapArch();
	void HideFinalCutWrapArch();
	FLinearColor GetSceneAccentColor(EMegaJamFilmScene Scene) const;
	void CreateArtPools(int32 ObstacleSlots);
	void SyncObstacleSkins();
	/** Issue #87: places the per-scene set-piece art at its real proportions (uniform scale). Used when SetPieceSceneISMs exist. */
	void PlaceSetPieceArtForChunk(int32 BaseSetIndex, int32 ChunkIndex, float ChunkStartX, float ChunkCenterX, EMegaJamFilmScene ChunkScene);
	UStaticMesh* GetSetPieceArtMesh(EMegaJamFilmScene Scene) const;
	void MarkSetPiecesDirty();
	UInstancedStaticMeshComponent* CreateVisualISM(UStaticMesh* Mesh);
	UMaterialInstanceDynamic* MakeMeshMID(UInstancedStaticMeshComponent* ISM);

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamFlowPawn> BoundPawn;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamGameMode> BoundGameMode;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ObstacleMatA;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ObstacleMatB;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SetPieceMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WrapArchMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoostStripMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LaneRailMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PickupMatA;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PickupMatB;

	UPROPERTY(Transient)
	TArray<FMegaJamSetPieceSlot> SetPieceSlots;

	UPROPERTY(Transient)
	TArray<FMegaJamFilmFramePickup> PickupSlots;

	UPROPERTY(Transient)
	TArray<FMegaJamDollyBoostStrip> BoostStripSlots;

	/**
	 * Issue #87: visual-only set-piece components, one per EMegaJamFilmScene (truss, skyscraper, film reel, aperture ring),
	 * each holding every set-piece slot. Each chunk draws its own scene's art, so nothing swaps or pops at scene changes.
	 * Empty when no set-piece art is assigned (SetPieceISM and the stretched cube layout are used instead).
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> SetPieceSceneISMs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SetPieceSceneMIDs;

	/** Visual-only obstacle skins, one per scene (see SceneObstacleMeshesTakeA). Empty when the feature is unused. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ObstacleSkinISMsA;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ObstacleSkinISMsB;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> StudioDressingExtraISMs;

	/** Dynamically created non-colliding ISMCs keyed by prop mesh for SceneScatterProps. */
	UPROPERTY(Transient)
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> ActiveScatterISMCs;

	/** Which skin component currently shows each obstacle instance (-1 = none yet). */
	TArray<int8> ObstacleSkinOwnerA;
	TArray<int8> ObstacleSkinOwnerB;

	bool bPoolsCreated = false;
	bool bBuilt = false;
	bool bWrapArchSpawned = false;
	float ScaledTrackTime = 0.0f;
	int32 NextBurstPickupCursor = 0;
	int32 FirstChunkIndex = 0;
	int32 SafeUntilChunkIndex = 2;
	EMegaJamFilmScene CurrentScene = EMegaJamFilmScene::Soundstage;
	int32 LastBroadcastScene = -1;
	FLinearColor CurrentSceneColor = FLinearColor(1.0f, 0.72f, 0.35f, 1.0f);
};
