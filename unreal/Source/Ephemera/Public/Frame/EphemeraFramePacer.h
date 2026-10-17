#pragma once

#include "CoreMinimal.h"
#include "RHIFwd.h"

/**
 * Keeps the game from running ahead of the graphics card.
 *
 * The game's own work on a frame takes a couple of milliseconds; drawing it takes twenty-five.
 * Left alone, the game works out frame after frame from what the hands were doing long ago and
 * queues them behind the card: three deep behind a window (75 ms between a key and its picture
 * at 40 frames a second), and without a window to hold it back (a hidden test, a stream to a
 * browser) sixteen deep, drawn in a rush and then a quarter-second stall, twice a second.
 *
 * So at the end of each frame the game waits until the card has finished the frame before. One
 * frame is always being drawn while the next is being worked out, which costs no speed at all,
 * and nothing ever waits in a queue: the picture is as young as it can be, and every frame is
 * the same length. (It is what NVIDIA's Reflex does, done here with the engine's own fences so
 * it works on any card, on the Mac, and with no window.)
 *
 * It also keeps the count: every ten seconds the log says how long frames really were, the
 * longest among them, and how many were stutters. That is the only way to know how a session on
 * someone else's machine felt.
 */
class EPHEMERA_API FEphemeraFramePacer
{
public:
	static FEphemeraFramePacer& Get();
	/** Starts counting frames, and holds the game to the card's pace or lets it run (the settings menu). */
	void SetEnabled(bool bOn);

private:
	void EndFrame();

	FGPUFenceRHIRef Last;
	FDelegateHandle Handle;
	bool bEnabled = false;
	/** Fences in a row that never came back: after a few the pacing gives up rather than slow the game. */
	int32 Lost = 0;
	bool bGaveUp = false;
	TArray<float> Lengths;
	double PreviousEnd = 0.0;
	double Counted = 0.0;
	double Waited = 0.0;
};
