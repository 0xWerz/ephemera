#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EphemeraData.h"
#include "SimulationMath.h"
#include "EphemeraWorld.generated.h"

class ACelestialBody;
class AEphemeraCharacter;
class AEphemeraShip;
class UEphemeraGameInstance;
class UEphemeraHUD;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UAudioComponent;
class UMaterialParameterCollection;
class UPostProcessComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class EPHEMERA_API AEphemeraWorld : public AActor
{
    GENERATED_BODY()
public:
    AEphemeraWorld();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    ACelestialBody* DominantBody(FVector Point) const;
    FVector GravityAt(FVector Point) const;
    double DilationAt(FVector Point) const;
    void PlacePlayer();
    void ResetLoop();
    void Die(const FString& Reason);
    void Interact();
    void OpenLog();
    void OpenMap();
    void TogglePause();
    void Photograph();
    void ToggleAntennae();
    bool Observed(FVector Point,double Range=16000) const;
    FString FocusPrompt() const;
    void Choose(int32 Index);
    UPROPERTY() TObjectPtr<AEphemeraCharacter> Player;
    UPROPERTY() TObjectPtr<AEphemeraShip> Ship;
    UPROPERTY() TObjectPtr<UEphemeraHUD> HUD;
    UPROPERTY() TObjectPtr<UEphemeraGameInstance> State;
    UPROPERTY() TMap<FString,TObjectPtr<ACelestialBody>> Bodies;
    UPROPERTY() TMap<FString,TObjectPtr<USceneComponent>> Groups;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PhotoTarget;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> PhotoCapture;
    UPROPERTY() TObjectPtr<UPointLightComponent> StarLight;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Starfield;
    UPROPERTY() TObjectPtr<UAudioComponent> Music;
    UPROPERTY() TMap<FString,TObjectPtr<UAudioComponent>> SignalAudio;
    UPROPERTY() TObjectPtr<UPostProcessComponent> Post;
    UPROPERTY() TObjectPtr<UMaterialParameterCollection> WorldParameters;
    EphemeraSim::Clocks Clocks;
    bool bFrozen=false,bEnding=false,bPhotoVisible=false;
    bool bAntennae=false;
    FString PanelMode;
private:
    EphemeraData::Object WorldData,AssetData;
    TMap<FString,EphemeraData::Object> GroupData;
    TMap<FString,FTransform> RestTransforms;
    TMap<UPrimitiveComponent*,EphemeraData::Object> InteractionData;
    TMap<FString,double> Timers;
    TMap<FString,int32> QuantumAt;
    TSet<FString> HiddenGroups;
    TArray<FString> ChoiceTargets;
    FString DialogueId,CurrentTrack;
    FTransform PhotoPose;
    double PhotoFov=80,DeathDelay=0,EndingTime=0,DeepTime=0;
    bool bDying=false,bEvensong=false;
    void Build();
    void Step(double Delta);
    void UpdateStory(double Delta);
    void UpdateQuantum(double Delta);
    void UpdateAudio();
    void OpenDialogue(const FString& Id);
    void DialogueNode(const FString& Id);
    void ReadText(const FString& Id);
    void SetPanel(const FString& Mode,const FString& Title,const FString& Body,const TArray<FString>& Choices,bool Freeze);
    void ClosePanel();
    void SetGroupVisible(const FString& Id,bool Visible);
    EphemeraData::Object Focused() const;
    void Use(const EphemeraData::Object& Interaction);
    UStaticMesh* Mesh(const FString& Id) const;
};
