# Surface-relative zero-G free-look camera

Separate themed branch: `fix/zero-g-camera`, based on production `main` rather
than `playtest`. The encounter-lock fix remains on its own branch/PR and is
included with this change only in the combined playtest build.

## Cause and scope

While standing still with Free Look enabled, the port orbits the camera without
turning the player. The previous orbit rotated world X/Z around world Y. Native
`GameObject::adjustYaw` instead post-multiplies the body basis by a local-Y
quaternion rotation: in world space its yaw axis is the body's current up.

Consequently standing orbit and the native moving/aiming yaw paths disagreed:
upside-down surfaces reversed the sign, and walls supplied a different axis.
Interaction selection deliberately mirrored the old camera orbit, so changing
only the camera would also make the E-targeting volume face a different way.

`camera::YawRotation` now rotates the camera eye/target around the actual 3D
player pivot and rotates the camera up vector about the same normalised body-up
axis. Interaction selection/debug geometry uses the same rotation. It preserves
the upright-floor yaw convention, camera pitch, distances, and the player's
surface up. Invalid axis/angle inputs leave the native camera unchanged.

No native movement/aiming sensitivity, camera mode, collision, surface walking,
jump trajectory, grapple state, health/damage, script or save logic is changed.
This fixes the identified standing-orbit disagreement; it is not a claim to fix
every zero-G aiming, kinesis, clipping or out-of-bounds symptom in upstream #11.

## User checks

Use Free Look enabled for the main checks. On an ordinary floor, each available
wall, and the ceiling:

1. Stand still and move the mouse/stick horizontally in both directions. The
   orbit should follow the surface without reversing relative to walking look.
2. Keep a noticeable orbit, then start walking, aiming or an action. Direction
   should remain consistent as the body catches up; report any snap or roll.
3. Check vertical look and aiming. Neither should be newly inverted or tilted.
4. Cross a sloped/curved surface transition, stop, and repeat. The camera should
   follow the current body up rather than abruptly choosing a world axis.
5. Use E on a nearby wall/ceiling terminal if convenient: targeting should still
   face the view. Check kinesis separately, without assuming this fixes it.
6. Disable Free Look and check movement/aiming remains unchanged. Recheck an
   encounter reveal with the combined build so its scripted lock still holds.

The reported continued damage after pushing a monster away is investigated
separately. This camera change cannot be counted as a grapple/damage fix.

## Automated checks

Asset-free tests cover floor/ceiling/four-wall axes, a tilted surface, positive
and negative yaw, pivot and distance preservation, camera-up consistency,
interaction-frame agreement, transfer of orbit into body yaw, normalisation and
invalid inputs. Windows/Linux application builds and all six CTest suites are
required before delivery. Actual visual/gameplay confirmation remains user-led.
