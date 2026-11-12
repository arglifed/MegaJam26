// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MegaJamGameMode.h"
#include "MegaJamTypes.h"
#include "MegaJamTrackGenerator.generated.h"

class AMegaJamFlowPawn;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

/**
 * Procedural corridor. Four instanced mesh pools (floor, walls, Take A obstacles, Take B obstacles) are laid out
 * as a ring buffer of chunks along +X. Chunks behind the pawn are recycled ahead of it.
 *
 * Take handling: the pawn's OnTakeCut toggles collision between the two obstacle pools. The ghost material reads
 * MPC_FlowState.ActiveTake so the inactive pool renders faint.
 *
 * Passability: every chunk is split into RowsPerChunk rows and each row holds at most one obstacle (of either take),
 * so the two takes never block the same longitudinal slice.
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

	// ---- Track tunables ----
	/** Length of one chunk along +X (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float ChunkLength = 2000.0f;

	/** Chunks kept active ahead of the pawn's chunk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 ChunksAhead = 15;

	/** Chunks kept behind the pawn's chunk before they are recycled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 ChunksBehind = 1;

	/** Obstacle rows per chunk. Each row holds at most one obstacle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 RowsPerChunk = 4;

	/** Chunks with an index below this are left empty (start runway). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 SafeChunks = 2;

	/** Deterministic layout seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	int32 Seed = 1337;

	/** Half width of the corridor (cm). Walls are centered on +/- this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float CorridorHalfWidth = 1000.0f;

	/** Probability that a row holds an obstacle at density scale 1 (scene 4, full flow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float MaxObstacleDensity = 0.75f;

	/** Obstacles are placed within +/- this lateral range (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float ObstacleLateralRange = 700.0f;

	/** Obstacle footprint (X depth and Y width, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float ObstacleSize = 200.0f;

	/** Obstacle height range (cm), standing on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	FVector2D ObstacleHeightRange = FVector2D(150.0, 300.0);

	/** Assumed speed (cm/s) when projecting when a chunk will be reached, to pick its scene. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Track")
	float MinProjectionSpeed = 3000.0f;

	// ---- Scenes ----
	/** Run time (s) at which each film scene ends: Soundstage, Overpass Night, Cutting Room, Final Cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Scenes")
	TArray<float> SceneEndTimes = { 35.0f, 75.0f, 115.0f, 150.0f };

	UFUNCTION(BlueprintPure, Category="MegaJam|Scenes")
	EMegaJamFilmScene GetCurrentScene() const { return CurrentScene; }

	/** Blueprint hook (lighting, props, title card) when the run enters a new film scene. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Scenes")
	void OnSceneChanged(EMegaJamFilmScene NewScene);

	// ---- Visuals (soft defaults; override in a Blueprint subclass) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Visuals")
	TObjectPtr<UStaticMesh> BlockMesh;

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

	/** Re-lays every chunk around the pawn's current position. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Track")
	void RebuildTrack();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Toggles collision between the two obstacle pools. */
	UFUNCTION()
	void HandleTakeCut(EMegaJamTakeState NewTake);

	UFUNCTION()
	void HandleRunStateChanged(EMegaJamRunState NewState, EMegaJamRunState OldState);

private:
	int32 PoolSize() const { return FMath::Max(1, ChunksAhead + ChunksBehind); }
	int32 SlotOf(int32 ChunkIndex) const;
	int32 SceneIndexAtTime(float RunTimeSeconds) const;
	float GetRunTimeSeconds() const;

	void ApplyTakeCollision(EMegaJamTakeState Take);
	void BindToPawn();
	void CreatePools();
	void PopulateChunk(int32 ChunkIndex);

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamFlowPawn> BoundPawn;

	UPROPERTY(Transient)
	TObjectPtr<AMegaJamGameMode> BoundGameMode;

	bool bPoolsCreated = false;
	bool bBuilt = false;
	int32 FirstChunkIndex = 0;
	EMegaJamFilmScene CurrentScene = EMegaJamFilmScene::Soundstage;
	int32 LastBroadcastScene = -1;
};

