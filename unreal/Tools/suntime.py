"""When is the sun where, at a place on Dawnhollow? For choosing the time of a photograph.

    python3 suntime.py x,y,z [time_seconds]

x,y,z is the place as a planet-local direction (what photo specs and Tools/terrain/scout.py use).
Prints local noon, the times the sun stands at 10, 25 and 40 degrees on either side of it, and the
sun's height at time_seconds if one is given. Times are solar-system seconds in the first day; the
day repeats every 1320 s.

The numbers below are Dawnhollow's orbit and spin from build_slice.py (build_bodies). With no axial
tilt the sun stays over the equator, so at latitude L it never climbs above 90 - L degrees.
"""
import math
import sys

ORBIT_PERIOD_S, ORBIT_PHASE = 6600.0, 0.4
SPIN_PERIOD_S, SPIN_PHASE = 1650.0, math.pi / 2.0 - 0.4
DAY_S = 1.0 / (1.0 / ORBIT_PERIOD_S + 1.0 / SPIN_PERIOD_S)


def sun_direction(time_s):
    """The sun's direction in the planet's own frame (checked against sun elevations the game logs)."""
    angle = ORBIT_PHASE + SPIN_PHASE + 2.0 * math.pi * time_s / DAY_S
    return -math.cos(angle), math.sin(angle), 0.0


def elevation_deg(place, time_s):
    sun = sun_direction(time_s)
    return math.degrees(math.asin(max(-1.0, min(1.0, sum(a * b for a, b in zip(place, sun))))))


def noon_s(place):
    longitude = math.atan2(place[1], place[0])
    return ((math.pi - longitude - ORBIT_PHASE - SPIN_PHASE) * DAY_S / (2.0 * math.pi)) % DAY_S


def main():
    place = [float(v) for v in sys.argv[1].split(",")]
    length = math.sqrt(sum(v * v for v in place))
    place = [v / length for v in place]
    noon = noon_s(place)
    print(f"day {DAY_S:.0f} s, latitude {math.degrees(math.asin(place[2])):.1f}, noon at {noon:.0f} s, sun then {elevation_deg(place, noon):.1f} deg")
    for wanted in (40.0, 25.0, 10.0):
        if elevation_deg(place, noon) < wanted:
            continue
        # Walk outward from noon to the first second the sun is below the wanted height.
        offset = next(s for s in range(int(DAY_S / 2)) if elevation_deg(place, noon + s) < wanted)
        print(f"  {wanted:.0f} deg: morning {(noon - offset) % DAY_S:.0f} s, afternoon {(noon + offset) % DAY_S:.0f} s")
    if len(sys.argv) > 2:
        print(f"at {float(sys.argv[2]):.0f} s: {elevation_deg(place, float(sys.argv[2])):.1f} deg")


if __name__ == "__main__":
    main()
