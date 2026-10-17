#include "EphemeraInputBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/PackageName.h"
#include "Player/EphemeraInputConfig.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	template <typename T>
	T* MakeAsset(const FString& Folder, const FString& Name)
	{
		UPackage* Package = CreatePackage(*(Folder / Name));
		Package->FullyLoad();
		T* Asset = FindObject<T>(Package, *Name);
		if (!Asset)
		{
			Asset = NewObject<T>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Asset);
		}
		return Asset;
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Asset,
			*FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}

	UInputAction* Action(const FString& Folder, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = MakeAsset<UInputAction>(Folder, Name);
		A->ValueType = Type;
		SaveAsset(A);
		return A;
	}

	enum class EAxisFix { None, Negate, ToY, ToYNegated };

	void Map(UInputMappingContext* Context, const UInputAction* A, FKey Key, EAxisFix Fix = EAxisFix::None)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(A, Key);
		if (Fix == EAxisFix::ToY || Fix == EAxisFix::ToYNegated)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Mapping.Modifiers.Add(Swizzle);
		}
		if (Fix == EAxisFix::Negate || Fix == EAxisFix::ToYNegated)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}

	/** Outer Wilds' stick shaping: a radial dead zone remapped linearly, no response curve. */
	void MapStick(UInputMappingContext* Context, const UInputAction* A, FKey Key)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(A, Key);
		UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Context);
		DeadZone->Type = EDeadZoneType::Radial;
		DeadZone->LowerThreshold = 0.2f;
		DeadZone->UpperThreshold = 0.95f;
		Mapping.Modifiers.Add(DeadZone);
	}
}

FString UEphemeraInputBuilder::BuildInputAssets(const FString& Folder)
{
	UInputAction* Move = Action(Folder, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	UInputAction* LookMouse = Action(Folder, TEXT("IA_LookMouse"), EInputActionValueType::Axis2D);
	UInputAction* LookStick = Action(Folder, TEXT("IA_LookStick"), EInputActionValueType::Axis2D);
	UInputAction* Jump = Action(Folder, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	UInputAction* Jets = Action(Folder, TEXT("IA_Jets"), EInputActionValueType::Axis1D);
	UInputAction* Boost = Action(Folder, TEXT("IA_Boost"), EInputActionValueType::Boolean);
	UInputAction* Lamp = Action(Folder, TEXT("IA_Lamp"), EInputActionValueType::Boolean);
	UInputAction* Interact = Action(Folder, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	UInputAction* Fly = Action(Folder, TEXT("IA_Fly"), EInputActionValueType::Boolean);
	UInputAction* FlySpeed = Action(Folder, TEXT("IA_FlySpeed"), EInputActionValueType::Axis1D);

	UInputMappingContext* OnFoot = MakeAsset<UInputMappingContext>(Folder, TEXT("IMC_OnFoot"));
	OnFoot->UnmapAll();
	// Keyboard + mouse
	Map(OnFoot, Move, EKeys::W, EAxisFix::ToY);
	Map(OnFoot, Move, EKeys::S, EAxisFix::ToYNegated);
	Map(OnFoot, Move, EKeys::D);
	Map(OnFoot, Move, EKeys::A, EAxisFix::Negate);
	Map(OnFoot, LookMouse, EKeys::Mouse2D);
	Map(OnFoot, Jump, EKeys::SpaceBar);
	Map(OnFoot, Jets, EKeys::LeftControl, EAxisFix::Negate);
	Map(OnFoot, Boost, EKeys::LeftShift);
	Map(OnFoot, Lamp, EKeys::F);
	Map(OnFoot, Interact, EKeys::E);
	Map(OnFoot, Fly, EKeys::V);
	Map(OnFoot, FlySpeed, EKeys::MouseWheelAxis);
	// Gamepad, laid out like Outer Wilds on a DualSense: left stick move, right stick look, cross jump,
	// R2 / L2 jets up / down, R1 boost, R3 lamp, square interact.
	MapStick(OnFoot, Move, EKeys::Gamepad_Left2D);
	MapStick(OnFoot, LookStick, EKeys::Gamepad_Right2D);
	Map(OnFoot, Jump, EKeys::Gamepad_FaceButton_Bottom);
	Map(OnFoot, Jets, EKeys::Gamepad_RightTriggerAxis);
	Map(OnFoot, Jets, EKeys::Gamepad_LeftTriggerAxis, EAxisFix::Negate);
	Map(OnFoot, Boost, EKeys::Gamepad_RightShoulder);
	Map(OnFoot, Lamp, EKeys::Gamepad_RightThumbstick);
	Map(OnFoot, Interact, EKeys::Gamepad_FaceButton_Left);
	Map(OnFoot, Fly, EKeys::Gamepad_DPad_Up);
	SaveAsset(OnFoot);

	UEphemeraInputConfig* Config = MakeAsset<UEphemeraInputConfig>(Folder, TEXT("DA_InputConfig"));
	Config->OnFootContext = OnFoot;
	Config->Move = Move;
	Config->LookMouse = LookMouse;
	Config->LookStick = LookStick;
	Config->Jump = Jump;
	Config->Jets = Jets;
	Config->Boost = Boost;
	Config->Lamp = Lamp;
	Config->Interact = Interact;
	Config->Fly = Fly;
	Config->FlySpeed = FlySpeed;
	SaveAsset(Config);
	return Config->GetPathName();
}
