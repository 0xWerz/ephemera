#include "Frame/EphemeraFrameSubsystem.h"

#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "SimulationMath.h"

namespace
{
	// Leaving a sphere of influence takes a little more distance than entering it, so skimming the
	// boundary doesn't flip the frame back and forth.
	constexpr double LeaveMargin = 1.04;
	// Step used to differentiate the frame mapping when re-expressing a rider's velocity.
	constexpr double RebaseStep = 1.0 / 240.0;
}

TStatId UEphemeraFrameSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEphemeraFrameSubsystem, STATGROUP_Tickables);
}

bool UEphemeraFrameSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UEphemeraFrameSubsystem::RegisterBody(AEphemeraBody* Body)
{
	if (Body && !Bodies.Contains(Body))
	{
		Bodies.Add(Body);
		bPosed = false;
	}
}

void UEphemeraFrameSubsystem::UnregisterBody(AEphemeraBody* Body)
{
	Bodies.Remove(Body);
}

void UEphemeraFrameSubsystem::RegisterRider(UObject* Rider)
{
	if (Rider && Rider->Implements<UEphemeraFrameRider>())
	{
		Riders.AddUnique(Rider);
	}
}

void UEphemeraFrameSubsystem::UnregisterRider(UObject* Rider)
{
	Riders.Remove(Rider);
}

AEphemeraBody* UEphemeraFrameSubsystem::FindBody(FName BodyId) const
{
	for (AEphemeraBody* Body : Bodies)
	{
		if (Body && Body->BodyId() == BodyId)
		{
			return Body;
		}
	}
	return nullptr;
}

AEphemeraBody* UEphemeraFrameSubsystem::GetStar() const
{
	for (AEphemeraBody* Body : Bodies)
	{
		if (Body && Body->Definition && Body->Definition->bIsStar)
		{
			return Body;
		}
	}
	return nullptr;
}

FTransform UEphemeraFrameSubsystem::InertialPose(const AEphemeraBody* Body, double AtTime) const
{
	if (!Body || !Body->Definition)
	{
		return FTransform::Identity;
	}
	const UEphemeraBodyDefinition& D = *Body->Definition;
	FVector Position = FVector::ZeroVector;
	if (D.OrbitRadiusM > 0.0)
	{
		EphemeraSim::Orbit Orbit;
		Orbit.radius = D.OrbitRadiusM;
		Orbit.period = D.OrbitPeriodS;
		Orbit.phase = D.OrbitPhaseRad;
		Orbit.eccentricity = D.OrbitEccentricity;
		Orbit.tilt = D.OrbitTiltRad;
		const EphemeraSim::Vec P = Orbit.Position(AtTime);
		// The shared maths is Y-up; Unreal is Z-up.
		Position = FVector(P.x, P.z, P.y) * 100.0;
		if (const AEphemeraBody* Parent = FindBody(D.ParentId))
		{
			if (Parent != Body)
			{
				Position += InertialPose(Parent, AtTime).GetLocation();
			}
		}
	}
	FQuat Rotation = FQuat(FVector::ForwardVector, D.AxialTiltRad);
	if (D.SpinPeriodS > 0.0)
	{
		Rotation = Rotation * FQuat(FVector::UpVector, D.SpinPhaseRad + UE_DOUBLE_TWO_PI * AtTime / D.SpinPeriodS);
	}
	return FTransform(Rotation, Position);
}

FTransform UEphemeraFrameSubsystem::InertialToWorld(const AEphemeraBody* InReference, double AtTime) const
{
	return InReference ? InertialPose(InReference, AtTime).Inverse() : FTransform::Identity;
}

void UEphemeraFrameSubsystem::ApplyPoses()
{
	const AEphemeraBody* Ref = Reference.Get();
	const FTransform ToWorld = InertialToWorld(Ref, Time);
	for (AEphemeraBody* Body : Bodies)
	{
		if (!Body)
		{
			continue;
		}
		if (Body == Ref)
		{
			// The reference rests at the origin. Only touch it if something displaced it.
			if (!Body->GetActorTransform().Equals(FTransform::Identity, 0.0))
			{
				Body->SetActorTransform(FTransform::Identity, false, nullptr, ETeleportType::TeleportPhysics);
			}
			continue;
		}
		Body->SetActorTransform(InertialPose(Body, Time) * ToWorld, false, nullptr, ETeleportType::TeleportPhysics);
	}
	bPosed = true;
	OnFrameUpdated.Broadcast();
}

FVector UEphemeraFrameSubsystem::PointVelocity(const AEphemeraBody* Body, const FVector& LocalPointCm) const
{
	if (!Body || !Body->Definition)
	{
		return FVector::ZeroVector;
	}
	const AEphemeraBody* Ref = Reference.Get();
	auto At = [&](const double AtTime)
	{
		return (InertialPose(Body, AtTime) * InertialToWorld(Ref, AtTime)).TransformPosition(LocalPointCm);
	};
	return (At(Time + RebaseStep) - At(Time)) / RebaseStep;
}

void UEphemeraFrameSubsystem::ChangeReference(AEphemeraBody* NewReference)
{
	AEphemeraBody* OldReference = Reference.Get();
	if (NewReference == OldReference)
	{
		return;
	}
	// old world -> inertial -> new world, now and one small step later (for velocities).
	auto Map = [&](double AtTime)
	{
		const FTransform OldWorldToInertial = OldReference ? InertialPose(OldReference, AtTime) : FTransform::Identity;
		return OldWorldToInertial * InertialToWorld(NewReference, AtTime);
	};
	const FTransform Now = Map(Time);
	const FTransform Soon = Map(Time + RebaseStep);
	for (const TWeakObjectPtr<UObject>& Weak : Riders)
	{
		UObject* Object = Weak.Get();
		IEphemeraFrameRider* Rider = Cast<IEphemeraFrameRider>(Object);
		const AActor* Actor = Cast<AActor>(Object);
		if (!Rider || !Actor)
		{
			continue;
		}
		const FVector P = Actor->GetActorLocation();
		const FVector V = Rider->GetFrameVelocity();
		const FVector NewVelocity = (Soon.TransformPosition(P + V * RebaseStep) - Now.TransformPosition(P)) / RebaseStep;
		Rider->RebaseToFrame(Now, NewVelocity);
	}
	Reference = NewReference;
	ApplyPoses();
	UE_LOG(LogTemp, Log, TEXT("Ephemera frame: reference is now %s"), NewReference ? *NewReference->BodyId().ToString() : TEXT("open space"));
}

AEphemeraBody* UEphemeraFrameSubsystem::DominantBody(const FVector& WorldPosition) const
{
	AEphemeraBody* Best = nullptr;
	double BestRadius = TNumericLimits<double>::Max();
	for (AEphemeraBody* Body : Bodies)
	{
		if (!Body || !Body->Definition || Body->Definition->bIsStar)
		{
			continue;
		}
		const double Influence = Body->InfluenceRadiusCm();
		if (Influence >= BestRadius || FVector::Dist(WorldPosition, Body->GetActorLocation()) >= Influence)
		{
			continue;
		}
		// A moon never claims space inside its parent's shell (ground + air): standing under the
		// moon must not hand you the moon's frame, gravity and sky.
		const AEphemeraBody* Parent = FindBody(Body->Definition->ParentId);
		if (Parent && Parent->Definition && !Parent->Definition->bIsStar
			&& FVector::Dist(WorldPosition, Parent->GetActorLocation()) < Parent->ShellRadiusCm())
		{
			continue;
		}
		Best = Body;
		BestRadius = Influence;
	}
	return Best;
}

AEphemeraBody* UEphemeraFrameSubsystem::NearestBody(const FVector& WorldPosition, const AEphemeraBody* Keep) const
{
	AEphemeraBody* Best = nullptr;
	double BestHeight = TNumericLimits<double>::Max();
	for (AEphemeraBody* Body : Bodies)
	{
		if (!Body || !Body->Definition)
		{
			continue;
		}
		double Height = FMath::Max(FVector::Dist(WorldPosition, Body->GetActorLocation()) - Body->RadiusCm(), 0.0);
		if (Body == Keep)
		{
			Height *= 0.8;
		}
		if (Height < BestHeight)
		{
			Best = Body;
			BestHeight = Height;
		}
	}
	return Best;
}

double UEphemeraFrameSubsystem::HeightOverNearestBodyCm(const FVector& WorldPosition) const
{
	double Best = TNumericLimits<double>::Max();
	for (const AEphemeraBody* Body : Bodies)
	{
		if (Body && Body->Definition)
		{
			Best = FMath::Min(Best, FVector::Dist(WorldPosition, Body->GetActorLocation()) - Body->RadiusCm());
		}
	}
	return Best;
}

AEphemeraBody* UEphemeraFrameSubsystem::NearestWithAtmosphere(const FVector& WorldPosition) const
{
	AEphemeraBody* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (AEphemeraBody* Body : Bodies)
	{
		if (!Body || !Body->Definition || Body->Definition->AtmosphereHeightM <= 0.0)
		{
			continue;
		}
		const double Distance = FVector::Dist(WorldPosition, Body->GetActorLocation()) - Body->RadiusCm();
		if (Distance < BestDistance)
		{
			Best = Body;
			BestDistance = Distance;
		}
	}
	return Best;
}

FVector UEphemeraFrameSubsystem::GravityAt(const FVector& WorldPosition) const
{
	const AEphemeraBody* Body = DominantBody(WorldPosition);
	if (!Body)
	{
		const AEphemeraBody* Star = GetStar();
		return Star ? Star->GravityAt(WorldPosition) : FVector::ZeroVector;
	}
	FVector Gravity = Body->GravityAt(WorldPosition);
	// Fields overlap just past a planet's shell: its moon's space still feels the planet, fading
	// out long before the moon's own ground, so there is no gravity cliff at the boundary.
	const AEphemeraBody* Parent = FindBody(Body->Definition->ParentId);
	if (Parent && Parent->Definition && !Parent->Definition->bIsStar)
	{
		const double Shell = Parent->ShellRadiusCm();
		const double Weight = 1.0 - FMath::SmoothStep(Shell, Shell * 1.35, FVector::Dist(WorldPosition, Parent->GetActorLocation()));
		if (Weight > 0.0)
		{
			Gravity += Parent->GravityAt(WorldPosition) * Weight;
		}
	}
	return Gravity;
}

FVector UEphemeraFrameSubsystem::SpinVector() const
{
	const AEphemeraBody* Ref = Reference.Get();
	if (!Ref || !Ref->Definition || Ref->Definition->SpinPeriodS <= 0.0)
	{
		return FVector::ZeroVector;
	}
	// In its own frame the reference's spin axis is its local +Z, which is world +Z.
	return FVector::UpVector * (UE_DOUBLE_TWO_PI / Ref->Definition->SpinPeriodS);
}

FVector UEphemeraFrameSubsystem::FictitiousAcceleration(const FVector& WorldPosition, const FVector& Velocity) const
{
	const FVector Omega = SpinVector();
	if (Omega.IsZero())
	{
		return FVector::ZeroVector;
	}
	const FVector R = WorldPosition;   // the reference sits at the origin
	return -2.0 * FVector::CrossProduct(Omega, Velocity) - FVector::CrossProduct(Omega, FVector::CrossProduct(Omega, R));
}

FQuat UEphemeraFrameSubsystem::SkyRotation() const
{
	return InertialToWorld(Reference.Get(), Time).GetRotation();
}

FVector UEphemeraFrameSubsystem::DirectionToStar(const FVector& From) const
{
	const AEphemeraBody* Star = GetStar();
	return Star ? (Star->GetActorLocation() - From).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector) : FVector::UpVector;
}

void UEphemeraFrameSubsystem::SetTime(double Seconds)
{
	// Riders stay where they are on the body they are at: remember each one's place in that
	// body's own space, move the rails, then put it back.
	struct FHeld { AActor* Actor; const AEphemeraBody* Body; FTransform Local; };
	TArray<FHeld> Held;
	for (const TWeakObjectPtr<UObject>& Weak : Riders)
	{
		AActor* Actor = Cast<AActor>(Weak.Get());
		const AEphemeraBody* Body = Actor ? DominantBody(Actor->GetActorLocation()) : nullptr;
		if (Body)
		{
			Held.Add({Actor, Body, Actor->GetActorTransform().GetRelativeTransform(Body->GetActorTransform())});
		}
	}
	Time = Seconds;
	ApplyPoses();
	for (const FHeld& Item : Held)
	{
		Item.Actor->SetActorTransform(Item.Local * Item.Body->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UEphemeraFrameSubsystem::Tick(float DeltaTime)
{
	Bodies.RemoveAll([](const TObjectPtr<AEphemeraBody>& Body) { return !IsValid(Body); });
	if (Bodies.IsEmpty())
	{
		return;
	}
	Time += DeltaTime * TimeScale;
	if (!bPosed)
	{
		ApplyPoses();
	}
	if (const AActor* Watcher = Observer.Get())
	{
		const FVector At = Watcher->GetActorLocation();
		AEphemeraBody* Current = Reference.Get();
		AEphemeraBody* Wanted = DominantBody(At);
		// Out beyond every world's reach the frame stays with the world last left, not the star's.
		// In the star's frame every planet moves every tick, and moving a planet means moving its
		// hundreds of terrain pieces and everything standing on them: measured on the PC, that
		// took seconds a frame and the game looked frozen the moment you flew far enough out.
		if (!Wanted && Current)
		{
			Wanted = Current;
		}
		// Stay with the current reference until clearly outside it.
		if (Current && Wanted != Current && FVector::Dist(At, Current->GetActorLocation()) < Current->InfluenceRadiusCm() * LeaveMargin
			&& (!Wanted || Wanted->InfluenceRadiusCm() > Current->InfluenceRadiusCm()))
		{
			Wanted = Current;
		}
		if (Wanted != Current)
		{
			ChangeReference(Wanted);
			return;
		}
	}
	ApplyPoses();
}
