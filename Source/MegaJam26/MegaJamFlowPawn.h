// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "MegaJamFlowPawn.generated.h"

class UCameraComponent;
class UInputAction;
class USpringArmComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Raw C++ physics pawn. No ACharacter / CharacterMovementComponent.
 * Velocity is integrated in Tick() and applied with a swept AddActorWorldOffset.
 * Binds Steer / Boost / Brake directly; the mapping context lives on the PlayerController.
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

	/** Chase camera boom. Does not use controller rotation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MegaJam|Components")
	TObjectPtr<UCameraComponent> Camera;

	// ---- Physics tunables ----
	/** Forward acceleration (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float Acceleration = 4000.0f;

	/** Exponential velocity damping (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float Drag = 0.5f;

	/** Lateral / vertical steering acceleration (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float StrafeForce = 3000.0f;

	/** Maximum speed (cm/s). Maps to FlowRatio = 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float TerminalVelocity = 6000.0f;

	/** Radius of the near-miss detector (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float GrazeRadius = 200.0f;

	/** Acceleration multiplier while boost is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float BoostMultiplier = 1.5f;

	/** Deceleration at full brake input (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float BrakeDeceleration = 6000.0f;

	/** Closing speed into a blocking surface (cm/s) at or above which the pawn crashes. Below it the pawn slides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")
	float CrashSpeedThreshold = 2500.0f;

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

	// ---- Reactive events ----
	/** Fired when the near-miss detector first overlaps an obstacle. */
	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnObstacleGrazed(AActor* GrazedActor);

	UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")
	void OnCrashed(AActor* HitActor);

protected:
	virtual void BeginPlay() override;

	/** Integrates velocity and sweeps the pawn. Triggers HandleCrash on a fast blocking hit. */
	virtual void StepPhysics(float DeltaSeconds);

	virtual void HandleCrash(AActor* HitActor);

	// ---- Input hooks ----
	virtual void Input_Steer(const FInputActionValue& Value);
	virtual void Input_Boost(const FInputActionValue& Value);
	virtual void Input_Brake(const FInputActionValue& Value);

	virtual void Input_SteerReleased();
	virtual void Input_BoostReleased();
	virtual void Input_BrakeReleased();

	UFUNCTION()
	void OnGrazeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnGrazeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	float FlowRatio = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="MegaJam|Flow")
	bool bCrashed = false;

	FVector2D SteerInput = FVector2D::ZeroVector;
	bool bBoostHeld = false;
	float BrakeInput = 0.0f;

	/** Actors currently inside the graze sphere that have already paid out. */
	TSet<TWeakObjectPtr<AActor>> GrazedActors;
};
