using UnrealBuildTool;

public class EphemeraEditor : ModuleRules
{
    public EphemeraEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Ephemera", "EnhancedInput", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "MeshDescription", "StaticMeshDescription", "AssetRegistry", "PhysicsCore", "MaterialEditor"
        });
    }
}
