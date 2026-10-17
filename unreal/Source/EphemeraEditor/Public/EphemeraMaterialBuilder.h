#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EphemeraMaterialBuilder.generated.h"

class UMaterialExpression;

/** Material wiring the editor's Python library cannot do itself. */
UCLASS()
class EPHEMERAEDITOR_API UEphemeraMaterialBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Wires a node into its material's Displacement input (the height Nanite tessellation moves
	 * the surface by). The input is hidden from scripting, so connect_material_property cannot name it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ephemera")
	static bool ConnectDisplacement(UMaterialExpression* From, const FString& Output);
};
