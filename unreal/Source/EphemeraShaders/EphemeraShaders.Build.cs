using UnrealBuildTool;

// The game's own GPU passes (compute shaders in unreal/Shaders). A module of its own because the shader
// folder has to be registered before the engine compiles any shader, which the game module is loaded too
// late for: this one loads at PostConfigInit (Ephemera.uproject).
public class EphemeraShaders : ModuleRules
{
    public EphemeraShaders(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "RenderCore", "RHI" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects" });
    }
}
