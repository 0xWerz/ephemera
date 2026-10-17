#include "Frame/EphemeraFramePacer.h"

#include "DynamicRHI.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RenderingThread.h"

FEphemeraFramePacer& FEphemeraFramePacer::Get()
{
	static FEphemeraFramePacer Pacer;
	return Pacer;
}

void FEphemeraFramePacer::SetEnabled(const bool bOn)
{
	if (!Handle.IsValid())
	{
		Handle = FCoreDelegates::OnEndFrame.AddRaw(this, &FEphemeraFramePacer::EndFrame);
	}
	if (bOn != bEnabled && !bGaveUp)
	{
		bEnabled = bOn;
		Last = nullptr;
		UE_LOG(LogTemp, Display, TEXT("Ephemera frames: %s"), bOn ? TEXT("held to the graphics card's pace (nothing queues behind it)") : TEXT("left to run ahead of the graphics card"));
	}
}

void FEphemeraFramePacer::EndFrame()
{
	if (!FApp::CanEverRender() || !GDynamicRHI || IsRunningCommandlet())
	{
		return;
	}
	if (bEnabled)
	{
		const double Began = FPlatformTime::Seconds();
		if (Last.IsValid())
		{
			// The frame before this one: once the card has finished it, this one can follow. Never
			// longer than a quarter of a second (a card that is busy elsewhere must not freeze the game).
			bool bDone = Last->Poll();
			while (!bDone && FPlatformTime::Seconds() - Began < 0.25)
			{
				FPlatformProcess::SleepNoStats(0.0003f);
				bDone = Last->Poll();
			}
			Lost = bDone ? 0 : Lost + 1;
			if (Lost >= 8)
			{
				bEnabled = false;
				bGaveUp = true;
				Last = nullptr;
				UE_LOG(LogTemp, Warning, TEXT("Ephemera frames: the graphics card never reported its frames finished; pacing is off for this session"));
			}
		}
		if (bEnabled)
		{
			FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("EphemeraFramePacer"));
			ENQUEUE_RENDER_COMMAND(EphemeraFramePacer)([Fence](FRHICommandListImmediate& RHICmdList) { RHICmdList.WriteGPUFence(Fence); });
			Last = Fence;
		}
		Waited += FPlatformTime::Seconds() - Began;
	}

	// The count. A frame's length is measured here, end to end, wait included: it is what the player saw.
	const double Now = FPlatformTime::Seconds();
	if (PreviousEnd > 0.0)
	{
		Lengths.Add(static_cast<float>((Now - PreviousEnd) * 1000.0));
		Counted += Now - PreviousEnd;
	}
	PreviousEnd = Now;
	if (Counted >= 10.0 && Lengths.Num() > 20)
	{
		Lengths.Sort();
		const float Median = Lengths[Lengths.Num() / 2];
		int32 Stutters = 0;
		for (const float Length : Lengths)
		{
			Stutters += Length > FMath::Max(Median * 2.5f, 50.f) ? 1 : 0;
		}
		UE_LOG(LogTemp, Display, TEXT("Ephemera frames: %d in %.1f s (%.0f a second), usually %.1f ms, 99 in 100 under %.1f ms, longest %.0f ms, %d stutters, %.0f%% of the time spent waiting for the graphics card"),
			Lengths.Num(), Counted, Lengths.Num() / Counted, Median, Lengths[FMath::Min(Lengths.Num() - 1, Lengths.Num() * 99 / 100)], Lengths.Last(), Stutters, 100.0 * Waited / Counted);
		Lengths.Reset();
		Counted = 0.0;
		Waited = 0.0;
	}
}
