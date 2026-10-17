#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FEphemeraShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Shaders/ beside the project file, as /EphemeraShaders. A cooked game has its shaders compiled
		// already and need not have the folder.
		const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
		if (FPaths::DirectoryExists(Folder))
		{
			AddShaderSourceDirectoryMapping(TEXT("/EphemeraShaders"), Folder);
		}
	}
};

IMPLEMENT_MODULE(FEphemeraShadersModule, EphemeraShaders);
