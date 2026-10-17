#include "Frame/EphemeraHeightField.h"

namespace
{
	// The same six faces as EphemeraPlanetBuilder.cpp: normal, then the two edge directions.
	const FVector FaceAxes[6][3] = {
		{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
		{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
		{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
	};
}

UEphemeraHeightField::FCorner UEphemeraHeightField::Locate(const FVector& D) const
{
	const double Ax = FMath::Abs(D.X), Ay = FMath::Abs(D.Y), Az = FMath::Abs(D.Z);
	int32 Face;
	if (Ax >= Ay && Ax >= Az)
	{
		Face = D.X > 0.0 ? 0 : 1;
	}
	else if (Ay >= Az)
	{
		Face = D.Y > 0.0 ? 2 : 3;
	}
	else
	{
		Face = D.Z > 0.0 ? 4 : 5;
	}
	const double Along = FVector::DotProduct(D, FaceAxes[Face][0]);
	// The grid is even in the angle across the face (the tangent warp), not in the flat coordinate.
	const double U = FMath::Atan(FVector::DotProduct(D, FaceAxes[Face][1]) / Along) / (UE_DOUBLE_PI / 4.0);
	const double V = FMath::Atan(FVector::DotProduct(D, FaceAxes[Face][2]) / Along) / (UE_DOUBLE_PI / 4.0);
	const double Gu = FMath::Clamp((U + 1.0) * 0.5 * Resolution, 0.0, Resolution - 1e-9);
	const double Gv = FMath::Clamp((V + 1.0) * 0.5 * Resolution, 0.0, Resolution - 1e-9);
	const int32 I = static_cast<int32>(Gu), J = static_cast<int32>(Gv);
	const double Fu = Gu - I, Fv = Gv - J;
	const int32 Row = Resolution + 1;
	const int32 Base = (Face * Row + J) * Row + I;
	FCorner Corner;
	Corner.Index[0] = Base;
	Corner.Index[1] = Base + 1;
	Corner.Index[2] = Base + Row;
	Corner.Index[3] = Base + Row + 1;
	Corner.Weight[0] = (1.0 - Fu) * (1.0 - Fv);
	Corner.Weight[1] = Fu * (1.0 - Fv);
	Corner.Weight[2] = (1.0 - Fu) * Fv;
	Corner.Weight[3] = Fu * Fv;
	return Corner;
}

double UEphemeraHeightField::HeightM(const FVector& UnitDirection) const
{
	const FCorner C = Locate(UnitDirection);
	double Level = 0.0;
	for (int32 K = 0; K < 4; ++K)
	{
		Level += Heights[C.Index[K]] * C.Weight[K];
	}
	return LowM + (HighM - LowM) * Level / 65535.0;
}

double UEphemeraHeightField::Sample(const TArray<uint8>& Map, const FVector& UnitDirection) const
{
	if (Map.Num() != Heights.Num())
	{
		return 0.0;
	}
	const FCorner C = Locate(UnitDirection);
	double Value = 0.0;
	for (int32 K = 0; K < 4; ++K)
	{
		Value += Map[C.Index[K]] * C.Weight[K];
	}
	return Value / 255.0;
}
