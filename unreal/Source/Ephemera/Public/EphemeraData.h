#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace EphemeraData
{
using Object = TSharedPtr<FJsonObject>;
inline FString String(const Object& O, const TCHAR* K, const FString& Default = {})
{
    FString Value;
    return O.IsValid() && O->TryGetStringField(K, Value) ? Value : Default;
}
inline double Number(const Object& O, const TCHAR* K, double Default=0)
{
    double Value;
    return O.IsValid() && O->TryGetNumberField(K, Value) ? Value : Default;
}
inline bool Boolean(const Object& O, const TCHAR* K, bool Default=false)
{
    bool Value;
    return O.IsValid() && O->TryGetBoolField(K, Value) ? Value : Default;
}
inline Object Child(const Object& O, const TCHAR* K)
{
    const Object* Value=nullptr;
    return O.IsValid() && O->TryGetObjectField(K, Value) ? *Value : nullptr;
}
inline const TArray<TSharedPtr<FJsonValue>>& Array(const Object& O, const TCHAR* K)
{
    static const TArray<TSharedPtr<FJsonValue>> Empty;
    const TArray<TSharedPtr<FJsonValue>>* Value=nullptr;
    return O.IsValid() && O->TryGetArrayField(K, Value) ? *Value : Empty;
}
inline FVector Raw(const TArray<TSharedPtr<FJsonValue>>& A)
{
    return A.Num()>=3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : FVector::ZeroVector;
}
inline FVector Raw(const Object& O, const TCHAR* K) { return Raw(Array(O, K)); }
// Godot right-handed Y-up metres -> Unreal left-handed Z-up centimetres.
inline FVector Axis(FVector V) { return FVector(-V.Z, V.X, V.Y); }
inline FVector Position(const Object& O, const TCHAR* K) { return Axis(Raw(O,K))*100.0; }
inline FTransform Transform(const Object& O)
{
    if (!O.IsValid()) return FTransform::Identity;
    const FVector X=Axis(-Raw(O,TEXT("z"))), Y=Axis(Raw(O,TEXT("x"))), Z=Axis(Raw(O,TEXT("y")));
    FMatrix M=FMatrix::Identity;
    M.SetAxes(&X,&Y,&Z);
    FTransform Result(M);
    Result.SetTranslation(Position(O,TEXT("p")));
    return Result;
}
inline FLinearColor Color(const Object& O, const TCHAR* K)
{
    const auto& A=Array(O,K);
    return A.Num()>=3 ? FLinearColor(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber(),A.Num()>3?A[3]->AsNumber():1) : FLinearColor::White;
}
Object Load(const FString& Filename);
}
