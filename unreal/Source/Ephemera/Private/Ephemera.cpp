#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS
void StartEphemeraRuntimeSmoke();
#endif

class FEphemeraModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
#if WITH_DEV_AUTOMATION_TESTS
        StartEphemeraRuntimeSmoke();
#endif
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FEphemeraModule, Ephemera, "Ephemera");
