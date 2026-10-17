using UnrealBuildTool;
using System.Collections.Generic;

public class EphemeraEditorTarget : TargetRules
{
    public EphemeraEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("Ephemera");
        ExtraModuleNames.Add("EphemeraEditor");
    }
}
