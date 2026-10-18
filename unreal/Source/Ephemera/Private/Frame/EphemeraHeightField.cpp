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

void UEphemeraHeightField::Spot(const FVector& D, int32& Face, int32& I, int32& J, double& Fu, double& Fv) const
{
	const double Ax = FMath::Abs(D.X), Ay = FMath::Abs(D.Y), Az = FMath::Abs(D.Z);
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
	I = static_cast<int32>(Gu);
	J = static_cast<int32>(Gv);
	Fu = Gu - I;
	Fv = Gv - J;
}

UEphemeraHeightField::FCorner UEphemeraHeightField::Locate(const FVector& D) const
{
	int32 Face, I, J;
	double Fu, Fv;
	Spot(D, Face, I, J, Fu, Fv);
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
	int32 Face, I, J;
	double Fu, Fv;
	Spot(UnitDirection, Face, I, J, Fu, Fv);
	auto Weights = [](const double F, double W[4])
	{
		W[0] = F * (-0.5 + F * (1.0 - 0.5 * F));
		W[1] = 1.0 + F * F * (-2.5 + 1.5 * F);
		W[2] = F * (0.5 + F * (2.0 - 1.5 * F));
		W[3] = F * F * (-0.5 + 0.5 * F);
	};
	double Wu[4], Wv[4];
	Weights(Fu, Wu);
	Weights(Fv, Wv);
	const int32 Row = Resolution + 1;
	const int32 First = Face * Row * Row;
	double Level = 0.0;
	// (At a face's edge the points past it are taken as the edge's own: the GPU's grid reads its
	// neighbouring face there, and the two part by a few centimetres within one cell of the edge.)
	for (int32 B = 0; B < 4; ++B)
	{
		const int32 Y = FMath::Clamp(J - 1 + B, 0, Resolution);
		for (int32 A = 0; A < 4; ++A)
		{
			const int32 X = FMath::Clamp(I - 1 + A, 0, Resolution);
			Level += Heights[First + Y * Row + X] * (Wu[A] * Wv[B]);
		}
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
