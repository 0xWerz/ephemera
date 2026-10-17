#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Interface.h"
#include "EphemeraFrameSubsystem.generated.h"

class AEphemeraBody;

UINTERFACE(MinimalAPI)
class UEphemeraFrameRider : public UInterface
{
	GENERATED_BODY()
};

/** Anything that must keep its place when the reference frame hands over: the player, the ship, the scout. */
class EPHEMERA_API IEphemeraFrameRider
{
	GENERATED_BODY()

public:
	/** Velocity in the current frame, cm/s. */
	virtual FVector GetFrameVelocity() const = 0;
	/** Apply the hand-over: OldToNew maps old-frame world space to new-frame world space. */
	virtual void RebaseToFrame(const FTransform& OldToNew, const FVector& NewVelocity) = 0;
};

/**
 * The solar system on rails, seen from wherever the player is.
 *
 * Every body's pose is a pure function of time (orbit + spin). Each tick the subsystem picks the
 * reference body (the one whose sphere of influence contains the observer) and places every body
 * relative to it, so the reference body rests at the world origin and never moves: its lighting,
 * shadow and physics caches stay valid, and standard character movement works on it. In open space
 * the reference is the star's inertial frame.
 */
UCLASS()
class EPHEMERA_API UEphemeraFrameSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterBody(AEphemeraBody* Body);
	void UnregisterBody(AEphemeraBody* Body);
	void RegisterRider(UObject* Rider);
	void UnregisterRider(UObject* Rider);

	/** The actor whose position chooses the reference frame (the player's pawn). */
	void SetObserver(AActor* Actor) { Observer = Actor; }

	/** Null in open space (the star's inertial frame). */
	AEphemeraBody* GetReference() const { return Reference.Get(); }
	AEphemeraBody* FindBody(FName BodyId) const;
	AEphemeraBody* GetStar() const;
	/** Innermost sphere of influence containing the point; null if only the star's. */
	AEphemeraBody* DominantBody(const FVector& WorldPosition) const;

	/**
	 * The body whose surface is nearest the point, star included. Keep, if given, is the one chosen
	 * last time: it counts as a fifth nearer than it is, so the choice does not flicker between two.
	 */
	AEphemeraBody* NearestBody(const FVector& WorldPosition, const AEphemeraBody* Keep = nullptr) const;

	/** Distance from a point to the nearest body's datum sphere, cm (negative inside one). */
	double HeightOverNearestBodyCm(const FVector& WorldPosition) const;

	/** The closest body that has an atmosphere (for the sky), or null. */
	AEphemeraBody* NearestWithAtmosphere(const FVector& WorldPosition) const;

	/** Gravity at a world position in the current frame, cm/s². */
	FVector GravityAt(const FVector& WorldPosition) const;
	/** Coriolis + centrifugal acceleration for something moving freely in the current (rotating) frame, cm/s². */
	FVector FictitiousAcceleration(const FVector& WorldPosition, const FVector& Velocity) const;
	/** Rotation of the fixed stars in the current frame (they turn overhead as the world you stand on spins). */
	FQuat SkyRotation() const;
	/** Unit vector from a point toward the star, in the current frame. */
	FVector DirectionToStar(const FVector& From) const;
	/**
	 * How fast a point fixed to a body (given in the body's own space, cm) is moving in the current
	 * frame, cm/s. Zero on the reference body; a planet's orbital and spin speed anywhere else.
	 * Whatever is set down on a body must be given this, or the next change of frame finds it at
	 * rest in the old frame and sends it off at hundreds of metres a second.
	 */
	FVector PointVelocity(const AEphemeraBody* Body, const FVector& LocalPointCm) const;

	double GetTime() const { return Time; }
	/** Jump the rails to a time (loop reset, tests). Riders keep their place on the reference body. */
	void SetTime(double Seconds);
	/** World seconds per real second (time dilation). */
	double TimeScale = 1.0;

	/** Fired after every body has been placed for this frame (the sky follows from here). */
	FSimpleMulticastDelegate OnFrameUpdated;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FTransform InertialPose(const AEphemeraBody* Body, double AtTime) const;
	/** Maps inertial space to current world space at a time (the inverse of the reference's pose). */
	FTransform InertialToWorld(const AEphemeraBody* InReference, double AtTime) const;
	void ApplyPoses();
	void ChangeReference(AEphemeraBody* NewReference);
	FVector SpinVector() const;

	UPROPERTY() TArray<TObjectPtr<AEphemeraBody>> Bodies;
	UPROPERTY() TArray<TWeakObjectPtr<UObject>> Riders;
	TWeakObjectPtr<AEphemeraBody> Reference;
	TWeakObjectPtr<AActor> Observer;
	double Time = 0.0;
	bool bPosed = false;
};
