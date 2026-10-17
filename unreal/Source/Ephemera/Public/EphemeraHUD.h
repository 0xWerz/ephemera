#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EphemeraHUD.generated.h"

class UTextBlock;
class UVerticalBox;
class UBorder;
class UImage;
class AEphemeraWorld;

UCLASS()
class EPHEMERA_API UEphemeraHUD : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry,float Delta) override;
    void ShowPanel(const FString& Title,const FString& Body,const TArray<FString>& Choices);
    void HidePanel();
    void Toast(const FString& Message);
    UPROPERTY() TObjectPtr<AEphemeraWorld> Universe;
    UPROPERTY() TObjectPtr<UTextBlock> Status;
    UPROPERTY() TObjectPtr<UTextBlock> Prompt;
    UPROPERTY() TObjectPtr<UTextBlock> ToastText;
    UPROPERTY() TObjectPtr<UBorder> Panel;
    UPROPERTY() TObjectPtr<UTextBlock> PanelTitle;
    UPROPERTY() TObjectPtr<UTextBlock> PanelText;
    UPROPERTY() TObjectPtr<UVerticalBox> ChoiceBox;
    UPROPERTY() TObjectPtr<UImage> Photo;
private:
    float ToastTime=0;
    UFUNCTION() void Choice0();
    UFUNCTION() void Choice1();
    UFUNCTION() void Choice2();
    UFUNCTION() void Choice3();
    UFUNCTION() void Choice4();
    UFUNCTION() void Choice5();
    UFUNCTION() void Choice6();
    UFUNCTION() void Choice7();
};
