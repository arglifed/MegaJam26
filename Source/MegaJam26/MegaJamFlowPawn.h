// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "MegaJamTypes.h"
#include "MegaJamFlowPawn.generated.h"

class UAudioComponent;
class UCameraComponent;
class UInputAction;
class UMaterialInstanceDynamic;
class USoundBase;
class USpringArmComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Fired when the player hard-cuts between takes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTakeCut, EMegaJamTakeState, NewTake);

/** Fired when the framing mode toggles. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAspectChanged, EMegaJamAspectMode, NewAspect);

/** Fired when the mid-air Clapperboard stunt transition snaps between scenes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnClapperboardSnapped);

/**
 * Raw C++ physics pawn. No ACharacter / CharacterMovementComponent.
 * Velocity is integrated in Tick() and applied with a swept AddActorWorldOffset.
 * Binds Steer / Boost / Brake directly; the mapping context lives on the PlayerController.
 *
 * Director's Cut controls:
 *   IA_Boost (Started) = hard cut between StudioTake / DirectorsTake (or restart the run when crashed / completed).
 *   IA_Brake (Started) = toggle Wide_21_9 / Box_4_3 framing, which clamps the playable Y / Z corridor.
 */
UCLASS()
class MEGAJAM26_API AMegaJamFlowPawn : public APawn
{
	GENERATED_BODY()

public:
	AMegaJamFlowPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---- Components ----
	/** Collision root. Sweeps against obstacles. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	/** Near-miss detector. Larger than the collision sphere, overlap only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USphereComponent> GrazeSphere;

	/** Visual. No collision. Defaults to /Engine/BasicShapes/Sphere. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	/** Optional thematic skeletal mesh component swapped dynamically per scene genre via SceneVehicleMeshes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Art")
	TObjectPtr<USkeletalMeshComponent> VehicleSkeletalMesh;

	/** Per-scene vehicle skeletal meshes (Runner -> Horse -> Vintage Car, etc.) swapped on genre transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Art")
	TMap<EMegaJamSceneGenre, TObjectPtr<USkeletalMesh>> SceneVehicleMeshes;

	/** Chase camera boom. Does not use controller rotation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UCameraComponent> Camera;

	// ---- Audio / MetaSound Bridge ----
	/** Audio component driving the dual-stem MetaSound score and parameter bridge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Audio")
	TObjectPtr<UAudioComponent> ScoreAudioComponent;

	/** Optional MetaSound asset to auto-play in BeginPlay. Safely no-ops if null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Audio")
	TObjectPtr<USoundBase> ScoreMetaSound;

	/** Audio component for the background music track. Separate from the MetaSound score. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Audio")
	TObjectPtr<UAudioComponent> MusicAudioComponent;

	/** Music started in BeginPlay and looped for the whole session (keeps playing through crash resets). Clear it to disable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Audio")
	TObjectPtr<USoundBase> MusicTrack;

	/** 2D one-shot each time a run starts (from the title screen and after every crash reset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Audio")
	TObjectPtr<USoundBase> RunStartSound;

	/** 2D one-shot when a Film Frame pickup is collected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Audio")
	TObjectPtr<USoundBase> FilmFramePickupSound;

	/** 2D one-shot when a Dolly Boost strip is collected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Audio")
	TObjectPtr<USoundBase> DollyBoostStripSound;

	/** 2D one-shot on a real crash (not God Mode's simulated hits). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Audio")
	TObjectPtr<USoundBase> CrashSound;

	/** Per-scene music tracks mapped across all 5 genre scenes (Action, Western, Noir, Chase, Montage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
	TMap<EMegaJamSceneGenre, TObjectPtr<USoundBase>> SceneMusicTracks;

	/** Active genre track currently playing on MusicAudioComponent. */
	UPROPERTY(BlueprintReadOnly, Category="Audio")
	EMegaJamSceneGenre ActiveMusicGenre = EMegaJamSceneGenre::Action;

	/** 2D one-shot played when entering 21:9 Director's Cut (defaults to SW_SFX_PositiveRise). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
	TObjectPtr<USoundBase> DirectorsCutEnterSound;

	/** 2D one-shot played when exiting 21:9 Director's Cut back to 4:3 (defaults to SW_SFX_ClapperboardHit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
	TObjectPtr<USoundBase> DirectorsCutExitSound;

	// ---- Physics tunables ----
	/** Forward acceleration (cm/s^2). Tuned for aggressive high-speed arcade readability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float Acceleration = 3800.0f;

	/** Exponential forward velocity damping (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float Drag = 0.5f;

	/** Extra lateral/vertical damping (1/s) so lane and altitude snaps feel crisp rather than floaty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float LateralDamping = 4.2f;

	/** Lateral steering acceleration (cm/s^2). Boosted +25% for responsive lane changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float StrafeForce = 3750.0f;

	/** Multiplier applied to vertical climb/dive acceleration in Box_4_3 mode (+30%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float VerticalStrafeMultiplier = 1.30f;

	/** Maximum speed (cm/s). Maps to FlowRatio = 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float TerminalVelocity = 5200.0f;

	/** Radius of the near-miss detector (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float GrazeRadius = 220.0f;

	/** DEPRECATED: unused since IA_Boost became the hard cut. Kept so existing Blueprint defaults do not break. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float BoostMultiplier = 1.5f;

	/** DEPRECATED: unused since IA_Brake became the aspect toggle. Kept so existing Blueprint defaults do not break. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float BrakeDeceleration = 6000.0f;

	/** Closing speed into a blocking surface (cm/s) at or above which the pawn crashes. Below it the pawn slides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float CrashSpeedThreshold = 2500.0f;

	/** Minimum seconds between hard cuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float CutCooldown = 0.25f;

	/** Forward speed added on every hard cut (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float CutSpeedImpulse = 420.0f;

	// ---- Match-Cut Chain & Stale-Shot Mechanics ----
	/** Maximum time (s) after grazing an obstacle or collecting a Film Frame in which a Hard Cut or Aspect Switch triggers a Match Cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float MatchCutWindow = 1.5f;

	/** Maximum SpliceChain value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	int32 MaxSpliceChain = 8;

	/** Bonus score multiplied by SpliceChain when a Match Cut or Film Frame pickup triggers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float MatchCutBonusBase = 150.0f;

	/** Seconds spent continuously in the same Aspect Mode before the shot becomes stale (5.5s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float StaleShotThreshold = 5.5f;

	/** Seconds between -1 SpliceChain decay ticks while bIsShotStale is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Chain")
	float StaleDecayInterval = 2.0f;

	// ---- Aspect ratio corridor (world space; +X is forward, Y lateral, Z vertical) ----
	/** Wide_21_9: wide lateral bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D WideYBounds = FVector2D(-900.0, 900.0);

	/** Wide_21_9: horizontal cinema rail bounds (Z = 140). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D WideZBounds = FVector2D(140.0, 140.0);

	/** Box_4_3: full 2D free-flight lateral bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D BoxYBounds = FVector2D(-850.0, 850.0);

	/** Box_4_3: full 2D free-flight vertical bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	FVector2D BoxZBounds = FVector2D(60.0, 860.0);

	/** Lateral strafe force multiplier while in Wide_21_9 Bullet-Time Overdrive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float WideStrafeMultiplier = 1.65f;

	// ---- 4:3 Flight Charge vs. 21:9 Bullet-Time Overdrive Tunables ----
	/** Base forward cruise velocity (cm/s) in 4:3 Flight & Charge Mode (+25% speed bump). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Overdrive")
	float CruiseSpeed = 3800.0f;

	/** Maximum forward speed (cm/s) while in 4:3 Flight & Charge Mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Overdrive")
	float MaxFlightSpeed = 4500.0f;

	/** Forward rail velocity (cm/s) during 21:9 Bullet-Time Overdrive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Overdrive")
	float OverdriveSpeed = 5200.0f;

	/** Minimum Film Meter required to activate 21:9 Director's Cut Overdrive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Overdrive")
	float OverdriveMinActivationMeter = 25.0f;

	/** Film Meter drain rate (units/sec) while 21:9 Bullet-Time Overdrive is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Overdrive")
	float OverdriveDrainPerSec = 15.0f;

	/** Altitude threshold (cm) in Box_4_3 (kept for Blueprint compatibility). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float DropCutMinAltitude = 185.0f;

	/** Forward speed impulse (cm/s) granted on Overdrive activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float DropCutSpeedImpulse = 850.0f;

	/** Additive camera FOV punch (degrees) triggered on Overdrive activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float DropCutFOVPunch = 9.0f;

	/** Passive Flow regeneration rate (% per second) while riding the Wide_21_9 dolly rail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float WideDollyFlowRegenPerSec = 8.0f;

	/** Legacy overexposure tunables kept for Blueprint compatibility (unused in unrestricted 4:3 flight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float OverexposureAltitudeThreshold = 175.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float OverexposureStartSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float OverexposureMaxSeconds = 3.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float OverexposureFlowDrainPerSec = 0.0f;

	/** FInterpTo speed used to squeeze the pawn into new bounds after an aspect switch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float AspectClampInterpSpeed = 30.0f;

	/** Active discrete magnetic lane index in 21:9 mode (0 = Far Left -700, 1 = Mid Left -350, 2 = Center 0, 3 = Mid Right +350, 4 = Far Right +700). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	int32 CurrentLane = 2;

	/** Fixed lateral distance (cm) between adjacent 21:9 magnetic lanes (lanes at Y = -700, -350, 0, 350, 700). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float LaneWidthY = 350.0f;

	/** Camera field of view in Wide_21_9 mode (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float WideFOV = 100.0f;

	/** Camera field of view in UltraPanavision_276_1 mode (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float UltraPanavisionFOV = 106.0f;

	/** Camera field of view in Box_4_3 mode (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float BoxFOV = 82.0f;

	/** Interpolation speed for camera FOV transitions between aspect modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Aspect")
	float AspectFOVInterpSpeed = 12.0f;

	// ---- Camera Juice ----
	/** Maximum camera roll (degrees) induced by lateral strafe velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float MaxStrafeRoll = 4.5f;

	/** Interpolation speed for camera strafe roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float CameraRollInterpSpeed = 10.0f;

	/** Additive FOV punch (degrees) applied on a hard cut before decaying back to base FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float CutFOVPunch = 6.0f;

	/** Decay interpolation speed for the additive hard-cut FOV punch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Camera")
	float CutFOVPunchDecaySpeed = 10.0f;

	/** Steady forward cruise speed (cm/s) during the Attract-Mode Title Screen and Victory Cruise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Attract")
	float AttractCruiseSpeed = 1600.0f;

	// ---- Enhanced Input actions (assign in a Blueprint subclass) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputAction> BoostAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Input")
	TObjectPtr<UInputAction> BrakeAction;

	/**
	 * If Steer / Boost / Brake actions are not assigned, creates transient UInputAction objects
	 * so the pawn is playable without any editor assets. Safe to call repeatedly.
	 */
	void EnsureFallbackInputActions();

	// ---- State ----
	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	float GetFlowRatio() const { return FlowRatio; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	FVector GetFlowVelocity() const { return Velocity; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	bool HasCrashed() const { return bCrashed; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	EMegaJamTakeState GetCurrentTake() const { return CurrentTake; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Flow")
	EMegaJamAspectMode GetAspectMode() const { return CurrentAspect; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	int32 GetSpliceChain() const { return SpliceChain; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	float GetAspectHoldTimer() const { return AspectHoldTimer; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	bool IsShotStale() const { return bIsShotStale; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Chain")
	float GetMatchCutFlashTimer() const { return MatchCutFlashTimer; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Aspect")
	float GetDropCutFlashTimer() const { return DropCutFlashTimer; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Aspect")
	float GetDollyBoostFlashTimer() const { return DollyBoostFlashTimer; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Aspect")
	float GetAirExposureRatio() const
	{
		return OverexposureMaxSeconds > KINDA_SMALL_NUMBER
			? FMath::Clamp(AirExposureTimer / OverexposureMaxSeconds, 0.0f, 1.0f)
			: 0.0f;
	}

	UFUNCTION(BlueprintPure, Category="MegaJam|Aspect")
	bool IsOverexposed() const { return bIsOverexposed; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Overdrive")
	float GetFilmMeter() const { return FilmMeter; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Overdrive")
	bool IsInBulletTimeOverdrive() const
	{
		return CurrentAspect == EMegaJamAspectMode::Wide_21_9 || CurrentAspect == EMegaJamAspectMode::UltraPanavision_276_1;
	}

	/** Called by AMegaJamTrackGenerator when the pawn collects an active-take Film Frame pickup. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Chain")
	void NotifyFilmFrameCollected();

	/** Called by AMegaJamTrackGenerator when the pawn rides over an outer-wing 21:9 Dolly Boost Strip. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Aspect")
	void NotifyDollyBoostStripCollected();

	/** Triggers the physical Dolly Boost speed surge, Film Meter gain, FOV warp, camera trauma, and audio rise. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Aspect")
	void TriggerDollyBoost();

	/** Adds procedural camera trauma (0..1) up to an optional ceiling cap (MaxCap) to drive high-frequency Pitch/Yaw/Roll/Z shake. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Camera")
	void AddCameraTrauma(float Amount, float MaxCap = 1.0f);

	/** Triggers a brief unscaled micro-hitstop (time dilation dip) that automatically restores to 1.0f. */
	void TriggerMicroHitstop(float RealDurationSeconds = 0.035f, float Dilation = 0.15f);

	// ---- Dev Testing Hotkeys (F1 God Mode) ----
	UFUNCTION(BlueprintCallable, Category="MegaJam|Dev")
	void ToggleGodMode();

	UFUNCTION(BlueprintPure, Category="MegaJam|Dev")
	bool IsGodModeEnabled() const { return bGodModeEnabled; }

	UFUNCTION(BlueprintPure, Category="MegaJam|Dev")
	int32 GetGodModeSimulatedHits() const { return GodModeSimulatedHits; }

	/** Clears crash state without teleporting back to origin (used by F2 DebugSkipToNextAct). */
	void ClearCrashStateForDebugSkip();

	// ---- Director's Cut actions ----
	/** Executes the Editor's Splice: toggles Take A / Take B and, if pressed within the 128 BPM beat window (0.18s), shreds obstacles ahead into Film Frames. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void ExecuteEditorSplice();

	/** Executes the Scene 02 Western Quick-Draw Beat-Slash while in UltraPanavision_276_1 aspect mode. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void ExecuteWesternBeatSlash();

	/** Triggers the mid-air Clapperboard stunt transition into NextGenre (vertical launch, 0.25s hitstop, blackout slate, mesh & music swap). */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void TriggerClapperboardTransition(EMegaJamSceneGenre NextGenre);

	/** Minimum cooldown (seconds) between ExecuteEditorSplice calls to prevent macro-spamming. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Flow")
	float EditorSpliceCooldown = 0.25f;

	/** Toggles the take, adds CutSpeedImpulse and fires OnTakeCut. Returns false when on cooldown or not in a live run. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	bool TriggerHardCut();

	/** Toggles between Wide_21_9 and Box_4_3. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void ToggleAspect();

	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void SetAspectMode(EMegaJamAspectMode NewAspect);

	/** Teleports back to the spawn transform and clears crash / velocity / take / aspect. Called by the GameMode on ResetRun. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	virtual void ResetForRun();

	/** Alias for ResetForRun. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Flow")
	void ResetPawnState() { ResetForRun(); }

	/** Synchronizes MusicAudioComponent with the target scene genre's mapped music wave. */
	UFUNCTION(BlueprintCallable, Category="Audio")
	void SyncSceneMusicTrack(EMegaJamSceneGenre TargetGenre);

	/** Plays RunStartSound. Called by the GameMode alongside OnClapperboard. */
	UFUNCTION(BlueprintCallable, Category="MegaJam|Audio")
	void PlayRunStartSound() const;

	// ---- Delegates ----
	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnTakeCut OnTakeCut;

	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnAspectChanged OnAspectChanged;

	UPROPERTY(BlueprintAssignable, Category="MegaJam|Flow")
	FOnClapperboardSnapped OnClapperboardSnapped;

	// ---- Reactive events ----
	/** Fired when the near-miss detector first overlaps an obstacle. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnObstacleGrazed(AActor* GrazedActor);

	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnCrashed(AActor* HitActor);

	/** Blueprint hook (camera punch, audio sting). Also broadcast through OnTakeCut. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnTakeCutEvent(EMegaJamTakeState NewTake);

	/** Blueprint hook (letterbox widget). Also broadcast through OnAspectChanged. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnAspectChangedEvent(EMegaJamAspectMode NewAspect);

	/** Blueprint VFX hook triggered on an On-Beat Western Quick-Draw Beat-Slash. */
	UFUNCTION(BlueprintImplementableEvent, Category="VFX")
	void BP_PlaySlashVFX();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Integrates velocity and sweeps the pawn. Triggers HandleCrash on a fast blocking hit. */
	virtual void StepPhysics(float DeltaSeconds);

	virtual void HandleCrash(AActor* HitActor);

	/** Squeezes the pawn into the active aspect bounds and removes velocity driving out of them. */
	void ApplyAspectClamp(float DeltaSeconds);

	void PushTakeAndAspectToSubsystem() const;
	void PushAudioParameters() const;
	void UpdateDynamicMusicModulation(float UnscaledDelta);
	void TriggerAudioEvent(FName ParameterName) const;
	void PlayOneShot(USoundBase* Sound) const;
	bool TryTriggerMatchCut(bool bForceMatchCut = false);
	bool TryTriggerOnBeatCut();
	void UpdatePawnDynamicMaterial(float DeltaSeconds);

	/** Restarts MusicTrack when it ends so it loops. */
	UFUNCTION()
	void OnMusicFinished();

	// ---- Input hooks ----
	virtual void Input_Steer(const FInputActionValue& Value);
	/** Started: hard cut, or restart the run when crashed / completed. */
	virtual void Input_Boost(const FInputActionValue& Value);
	/** Started: aspect toggle. */
	virtual void Input_Brake(const FInputActionValue& Value);

	virtual void Input_SteerReleased();

	UFUNCTION()
	void OnGrazeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float FlowRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	bool bCrashed = false;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamTakeState CurrentTake = EMegaJamTakeState::StudioTake;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	EMegaJamAspectMode CurrentAspect = EMegaJamAspectMode::Box_4_3;

	/** Director's Cut Film Meter (0.0f to 100.0f). Starts at 50.0f. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Overdrive")
	float FilmMeter = 50.0f;

	/** Active Match-Cut chain multiplier (1 to 8). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Chain")
	int32 SpliceChain = 1;

	/** Seconds spent continuously in the current EMegaJamAspectMode without switching. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Chain")
	float AspectHoldTimer = 0.0f;

	/** Anti-camping timer tracking continuous duration in the active Aspect Mode (resets on cut). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Chain")
	float CurrentShotDuration = 0.0f;

	/** True when CurrentShotDuration exceeds StaleShotThreshold (5.5s). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Chain")
	bool bIsShotStale = false;

	/** Seconds spent above Z = 260 in Box_4_3 mode. Cools down 2.5x faster in Wide_21_9 or below Z = 220. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Aspect")
	float AirExposureTimer = 0.0f;

	/** True when AirExposureTimer > OverexposureStartSeconds (2.0s), draining Flow and applying aerodynamic drag. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Aspect")
	bool bIsOverexposed = false;

	/** True while the pawn is diving down at 3600 u/s from Box_4_3 into the Z = 100.0f Wide_21_9 dolly rail. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Aspect")
	bool bIsDropCutSlamming = false;

	/** Decaying camera pitch-nod (degrees) applied during a Drop-Cut Slam and springing back on touchdown. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Aspect")
	float SlamPitchPunch = 0.0f;

	/** Procedural camera trauma level (0.0f to 1.0f), decaying at 2.4f/sec. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Camera")
	float CameraTrauma = 0.0f;

	/** Unscaled real-time seconds remaining on a Drop-Cut Slam micro-hitstop. */
	float HitstopRealTimeRemaining = 0.0f;

	/** Remaining duration (s) for the white-hot emissive burst on Drop-Cut Slam / Match-Cut. */
	float EmissiveBurstTimer = 0.0f;

	/** Dynamic material instance on VisualMesh driven by FlowRatio and SpliceChain. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PawnVisualMID;

	FVector2D SteerInput = FVector2D::ZeroVector;

	/** Latch to prevent sliding across multiple 21:9 lanes from a single held stick/key press. */
	bool bHasProcessedLaneInput = false;

	/** Current decaying additive FOV punch from TriggerHardCut / Drop-Cut Slam. */
	float CurrentFOVPunch = 0.0f;

	/** Current interpolated camera roll angle (degrees). */
	float CurrentCameraRoll = 0.0f;

	/** World time of the last hard cut. */
	double LastCutTime = -1000.0;

	/** World time of the last ExecuteEditorSplice call (0.25s cooldown). */
	double LastEditorSpliceTime = -1000.0;

	/** World time of the most recent obstacle graze or Film Frame pickup. */
	double LastGrazeOrPickupTime = -1000.0;

	/** Accumulator for decaying SpliceChain every StaleDecayInterval seconds while stale. */
	float StaleDecayAccum = 0.0f;

	/** Accumulator for decaying SpliceChain when AirExposureTimer is held at max (4.5s). */
	float OverexposureDecayAccum = 0.0f;

	/** Remaining HUD flash timer after a Match Cut or Film Frame pickup. */
	float MatchCutFlashTimer = 0.0f;

	/** Remaining HUD flash timer after a Drop-Cut Slam. */
	float DropCutFlashTimer = 0.0f;

	/** Remaining HUD flash timer after riding an outer 21:9 Dolly Boost Strip. */
	float DollyBoostFlashTimer = 0.0f;

	/** Bonus Flow offset from 21:9 dolly regen, Drop-Cut Slams, and Boost Strips, vs 4:3 Overexposure drain. */
	float FlowBonusOffset = 0.0f;

	/** Decaying forward velocity bonus granted by outer-lane Dolly Boost Strips. */
	float ActiveDollyBoostBonus = 0.0f;

	/** Decaying katana camera roll tilt (degrees) applied on Western Beat-Slash. */
	float ActiveSlashRoll = 0.0f;

	/** Ensures the celebratory victory sting triggers once when entering Completed state. */
	bool bTriggeredVictorySting = false;

	/** Smoothly interpolated pitch multiplier for MusicAudioComponent. */
	float CurrentMusicPitch = 1.0f;

	/** Smoothly interpolated volume multiplier for MusicAudioComponent. */
	float CurrentMusicVolume = 0.90f;

	/** Dev God Mode flag (toggled by F1 in non-shipping builds). */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Dev")
	bool bGodModeEnabled = false;

	/** Number of fatal hits absorbed while bGodModeEnabled is active. */
	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Dev")
	int32 GodModeSimulatedHits = 0;

	/** World time of the last simulated hit in God Mode to prevent per-frame multi-counting. */
	double LastGodModeHitTime = -1000.0;

	/** Transform captured in BeginPlay, restored by ResetForRun. */
	FTransform SpawnTransform;

	/**
	 * Graze payout times keyed by (component, body index). Instanced meshes share one actor, so the key has to
	 * be per instance. A retrigger delay stops take-cut spamming from farming the multiplier.
	 */
	TMap<TTuple<TWeakObjectPtr<UPrimitiveComponent>, int32>, double> GrazeTimes;

	/** Seconds before the same obstacle instance can pay out another graze. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float GrazeRetriggerDelay = 1.5f;
};
