using UnrealBuildTool;

public class Ephemera : ModuleRules
{
    public Ephemera(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
            "UMG", "Slate", "SlateCore", "Json", "JsonUtilities", "Niagara", "ProceduralMeshComponent", "DeveloperSettings", "RenderCore", "RHI"
        });
    }
}
