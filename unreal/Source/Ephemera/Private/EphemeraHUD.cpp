#include "EphemeraHUD.h"
#include "EphemeraWorld.h"
#include "EphemeraCharacter.h"
#include "EphemeraShip.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UEphemeraHUD::RebuildWidget()
{
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget=Canvas;
    auto Text=[&](int Size,FLinearColor Color)
    {
        auto* T=WidgetTree->ConstructWidget<UTextBlock>();
        T->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size));
        T->SetColorAndOpacity(Color);
        T->SetAutoWrapText(true);
        return T;
    };
    const FLinearColor Cream(0.97,0.9,0.77),Gold(1,0.68,0.3);
    Status=Text(18,Cream);
    auto* Place=Canvas->AddChildToCanvas(Status);
    Place->SetPosition(FVector2D(36,28)); Place->SetSize(FVector2D(650,130));
    Prompt=Text(22,Gold);
    Place=Canvas->AddChildToCanvas(Prompt);
    Place->SetAnchors(FAnchors(0.5,0.76)); Place->SetAlignment(FVector2D(0.5,0.5)); Place->SetSize(FVector2D(650,60));
    Prompt->SetJustification(ETextJustify::Center);
    ToastText=Text(21,Cream);
    Place=Canvas->AddChildToCanvas(ToastText);
    Place->SetAnchors(FAnchors(0.5,0.2)); Place->SetAlignment(FVector2D(0.5,0.5)); Place->SetSize(FVector2D(900,100));
    ToastText->SetJustification(ETextJustify::Center);
    auto* Reticle=Text(18,FLinearColor(1,1,1,0.55));
    Reticle->SetText(FText::FromString(TEXT("·")));
    Place=Canvas->AddChildToCanvas(Reticle);
    Place->SetAnchors(FAnchors(0.5,0.5)); Place->SetAlignment(FVector2D(0.5,0.5)); Place->SetAutoSize(true);
    Photo=WidgetTree->ConstructWidget<UImage>();
    Place=Canvas->AddChildToCanvas(Photo);
    Place->SetAnchors(FAnchors(1,0)); Place->SetAlignment(FVector2D(1,0));
    Place->SetPosition(FVector2D(-30,30)); Place->SetSize(FVector2D(360,203));
    Photo->SetVisibility(ESlateVisibility::Collapsed);
    Panel=WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrushColor(FLinearColor(0.025,0.035,0.04,0.96));
    Panel->SetPadding(FMargin(32));
    Place=Canvas->AddChildToCanvas(Panel);
    Place->SetAnchors(FAnchors(0.5,0.5)); Place->SetAlignment(FVector2D(0.5,0.5)); Place->SetSize(FVector2D(960,650));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();
    Panel->SetContent(Scroll);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
    Scroll->AddChild(Column);
    PanelTitle=Text(30,Gold); Column->AddChildToVerticalBox(PanelTitle)->SetPadding(FMargin(0,0,0,20));
    PanelText=Text(23,Cream); Column->AddChildToVerticalBox(PanelText)->SetPadding(FMargin(0,0,0,24));
    ChoiceBox=WidgetTree->ConstructWidget<UVerticalBox>();
    Column->AddChildToVerticalBox(ChoiceBox);
    Panel->SetVisibility(ESlateVisibility::Collapsed);
    return Super::RebuildWidget();
}
void UEphemeraHUD::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    if (!Universe||!Universe->Player) return;
    auto* P=Universe->Player.Get();
    const int World=Universe->Clocks.world,Age=Universe->Clocks.proper;
    const FString ShipStatus=P->Ship?FString::Printf(TEXT("\nSEEDLING  Hull %.0f%%  Propellant %.0f%%"),P->Ship->Hull,P->Ship->Fuel*100):TEXT("");
    Status->SetText(FText::FromString(FString::Printf(TEXT("EPHEMERA\nDay %02d:%02d    Age %02d:%02d    %.1f×\nAir %.0f%%    Fuel %.0f%%%s"),
        World/60,World%60,Age/60,Age%60,Universe->Clocks.dilation,P->Oxygen*100,P->Fuel*100,*ShipStatus)));
    Prompt->SetText(FText::FromString(Universe->PanelMode.IsEmpty()?Universe->FocusPrompt():TEXT("")));
    if (ToastTime>0) { ToastTime-=Delta; if (ToastTime<=0) ToastText->SetText(FText::GetEmpty()); }
    if (Universe->bPhotoVisible&&Universe->PhotoTarget)
    {
        FSlateBrush Brush; Brush.SetResourceObject(Universe->PhotoTarget); Photo->SetBrush(Brush);
        Photo->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    else Photo->SetVisibility(ESlateVisibility::Collapsed);
}
void UEphemeraHUD::ShowPanel(const FString& Title,const FString& Body,const TArray<FString>& Choices)
{
    PanelTitle->SetText(FText::FromString(Title));
    PanelText->SetText(FText::FromString(Body));
    ChoiceBox->ClearChildren();
    for (int32 I=0;I<Choices.Num()&&I<8;++I)
    {
        auto* Button=WidgetTree->ConstructWidget<UButton>();
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(Choices[I]));
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),21));
        Label->SetColorAndOpacity(FLinearColor(0.95,0.88,0.73));
        Label->SetAutoWrapText(true);
        Button->SetContent(Label);
        Button->SetBackgroundColor(FLinearColor(0.08,0.14,0.15));
        ChoiceBox->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,5));
        switch(I)
        {
            case 0:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice0);break;
            case 1:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice1);break;
            case 2:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice2);break;
            case 3:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice3);break;
            case 4:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice4);break;
            case 5:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice5);break;
            case 6:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice6);break;
            case 7:Button->OnClicked.AddDynamic(this,&UEphemeraHUD::Choice7);break;
        }
        if (I==0) Button->SetKeyboardFocus();
    }
    Panel->SetVisibility(ESlateVisibility::Visible);
}
void UEphemeraHUD::HidePanel(){Panel->SetVisibility(ESlateVisibility::Collapsed);}
void UEphemeraHUD::Toast(const FString& Message){ToastText->SetText(FText::FromString(Message));ToastTime=6;}
#define EPHEMERA_CHOICE(N) void UEphemeraHUD::Choice##N(){if(Universe)Universe->Choose(N);}
EPHEMERA_CHOICE(0)
EPHEMERA_CHOICE(1)
EPHEMERA_CHOICE(2)
EPHEMERA_CHOICE(3)
EPHEMERA_CHOICE(4)
EPHEMERA_CHOICE(5)
EPHEMERA_CHOICE(6)
EPHEMERA_CHOICE(7)
#undef EPHEMERA_CHOICE
