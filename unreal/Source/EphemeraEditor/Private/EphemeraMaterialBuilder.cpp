#include "EphemeraMaterialBuilder.h"

#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpression.h"

bool UEphemeraMaterialBuilder::ConnectDisplacement(UMaterialExpression* From, const FString& Output)
{
	return From && UMaterialEditingLibrary::ConnectMaterialProperty(From, Output, MP_Displacement);
}
