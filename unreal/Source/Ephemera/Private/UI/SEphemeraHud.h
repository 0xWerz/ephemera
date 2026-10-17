#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AEphemeraPlayer;

/**
 * Everything drawn over the game: the frame-rate readout, a notice while the engine is still
 * preparing shaders or meshes (in an editor build things pop in until that is done, and it is
 * better said than left to look like a fault), and the settings menu.
 */
class SEphemeraHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEphemeraHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AEphemeraPlayer>, Player)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void SetMenuOpen(bool bOpen) { bMenuOpen = bOpen; }
	bool IsMenuOpen() const { return bMenuOpen; }
	/** The loading screen: up from the start until the player calls it off, then faded out. */
	void SetLoading(bool bOn, const FText& What = FText::GetEmpty(), float Progress = -1.f);
	bool IsLoading() const { return bLoading; }
	/** A line at the foot of the screen for a couple of seconds: what a key just did. */
	void Note(const FText& Text) { NoteText = Text; NoteLeft = 2.5f; }
	/** True while the engine is still compiling shaders or building meshes and textures. */
	bool IsPreparing() const { return !Preparing.IsEmpty(); }

private:
	TSharedRef<SWidget> BuildMenu();

	TWeakObjectPtr<AEphemeraPlayer> Player;
	bool bMenuOpen = false;
	bool bLoading = true;
	/** 1 while loading, falling to 0 over most of a second once it is over. */
	float Veil = 1.f;
	FText LoadingWhat;
	FText NoteText;
	float NoteLeft = 0.f;
	/** 0..1: how far through loading; only ever moves forward. */
	float LoadingProgress = 0.f;
	float FrameMs = 16.7f;
	/** The jets' tank (0..1) and how visible its line is. */
	float FuelNow = 1.f;
	float FuelShow = 0.f;
	/** What is still being prepared, as a sentence; empty when nothing is. */
	FText Preparing;
};
