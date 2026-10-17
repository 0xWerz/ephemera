# Controls: what the first version got wrong and what the new one is built on

Written 2026-10-13 after the user played the Outer Wilds-style controller on both machines and called it stiff, unpleasant
and "delayed". The three reports this draws on are beside it: `controls_ground.md` (how the best first-person games move a
body), `controls_camera_input.md` (camera, mouse, stick, latency) and `controls_flight.md` (jetpacks, zero-g, free cameras).
Numbers below are the ones the game uses (`unreal/Source/Ephemera/Public/Player/EphemeraPlayer.h`), with where each came from.

## Why it felt delayed

| Cause | What it cost | Now |
|---|---|---|
| The jump fired when the key was **released** (a charged jump, as in Outer Wilds) | the whole length of the press, 80-200 ms | fires on the press |
| Vertical sync on by default; at 40 fps on a 60 Hz screen every frame waits for the next refresh | the game ran at 30 fps and each frame was shown up to 17 ms late | off by default (menu) |
| The game thread ran a frame ahead of the renderer (`r.OneFrameThreadLag=1`) | one frame: 25 ms on the PC, 45 ms on the Mac | `r.OneFrameThreadLag=0`, `r.GTSyncType=1` |
| The graphics card's queue of waiting frames | three frames behind a window (75 ms at 40 fps); with no window (a hidden test, a stream to a browser) sixteen, drawn in a rush and then a 0.25 s stall, twice a second | `FEphemeraFramePacer`: at the end of each frame the game waits until the card has finished the frame before, so nothing queues. Measured on the PC, hidden, same scripted run: 53 frames over 100 ms in 47 s before, none after; 25.0 ms usual, longest 28 ms; 40 fps both times. NVIDIA Reflex is also taken where the driver offers it |
| Jets too weak to lift (6 m/s² against 10.5 m/s² gravity): the key did nothing visible | everything | 17 m/s² lift, and a shove of 1.2 m/s the instant they light |
| Sideways jets locked until the stick turned 10° | air control seemed dead | steering always answers |
| A landing sound that begins with 60 ms of something else | heard late | every one-shot starts within 2 ms of its file (measured by `Tools/audio/make.py`) |

Epic's own figures for a 30 Hz game: up to 130 ms from input to photon with default syncing, about 66 ms at best.

## On foot

- **Speed**: jog 4.8 m/s, run 8.0 m/s (Apex Legends 5.1 / 7.6, Destiny 2 about 5-7 / 8.0-8.3). Sideways 92%, backwards 80%
  (Destiny strafes at about 85%).
- **Starting and stopping**: full jog in 107 ms (Quake 3 reaches 90% in about 130 ms, Counter-Strike in 370). Stopping is
  Quake's shape, friction in proportion to speed plus a constant: a jog stops in 0.2 s and 40 cm.
- **Jump**: on the press. 5.2 m/s, which is 1.3 m on Dawnhollow and 4 m on Hush. Let go within 0.16 s and the rise is cut
  to 55% (the variable jump of every platformer since Mario). Coyote time 0.13 s and a 0.13 s buffer before landing (Celeste
  ships 0.10 s of coyote time).
- **In the air** without jets: Quake's air steering, capped at 6 m/s in the direction asked.

## Jets

One tank: 7 s of lift, refilled in 1.6 s standing on anything, and creeping back in the air (Apex's Valkyrie: 6 s, refill
3 s after a 2.5 s wait, ground only). Space after a jump lifts once held 0.15 s (so a tap is only a hop); pressed in the
air it lifts at once. Sideways thrust 10 m/s² while lifting; Shift doubles everything at 2.2 times the burn. Adrift in
space every direction gets 8 m/s² and the tank lasts four times as long. Air drag gives a long fall about 60 m/s.

## The view

Mouse: raw counts, no smoothing or acceleration, 0.22° a count before the sensitivity setting. Stick: tilt to the power 1.8,
190°/s yaw and 130°/s pitch, half as much again after a third of a second held against the edge.

All of the following scale with one setting ("camera motion", 0 to 150%):

- landing dip and take-off lag on a spring (2.6 Hz, damping 0.8), about 7 cm for an ordinary jump, 30 cm at most;
- a stride whose length grows with speed (0.9 m strolling, 1.7 m jogging, 2.4 m running: three steps a second flat out),
  the eye sinking a centimetre onto each step and the step heard at that moment;
- a lean into sideways travel of under a degree (Quake used 2°);
- a field of view 4° wider at a run, more in a boost or a long fall.

Steps up and down are smoothed over a tenth of a second always (Quake 3 took 200 ms).

## Free flight (V)

The speed asked for is eased into over 0.09 s and out of over 0.13 s whatever it is. Minecraft's creative flight glides
for a second or more, which is too loose for placing a camera; the engine's straight ramps were too mechanical.

## Not done yet

Sliding down slopes (Tribes' skiing, Apex's slide), pulling up over ledges, roll and velocity matching when adrift. The
jets cover the ground those would on this terrain, so they wait for the ship.
