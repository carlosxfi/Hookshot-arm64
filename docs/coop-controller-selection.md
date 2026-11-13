# Separate controllers for test10 Hookshot instances

This branch targets daurosk/oot-remake-multiplayer **v0.1.0-test10**, whose Hookshot pin is
`4aee23659b9436dab2cd7be1ac07333c06246b7c` (v2.2.0-rc.1). It includes that release's three co-op patches,
unchanged, followed by the controller change. The separate `arm64-linux` branch remains the Odin work.
The multiplayer module, server and puppet-pack builder still come from the original test10 kit.

## Install in the Windows co-op kit

Close all running games and builders. From PowerShell, run `Install-Test10-Controllers.ps1 -Kit C:\oot-coop`
(use the actual extracted kit folder). The installer checks the Hookshot pin and the three existing patch names,
adds `hookshot\patches\0004-controller-selection.patch`, and puts `Launch-Hookshot-Controller.ps1` at the kit root.
It does not change the three co-op patches, the source archive pin, saved games or compiled binaries.

Run **Build-Hookshot-Coop.bat** again in that kit. Its existing patch-hash check notices the new patch and rebuilds
Hookshot using the original kit's tools and your own dump. This changes host code, so a renderer-only build is
insufficient. The build may take several minutes. No ready-made game binaries are provided.

For two instances, use **separate kit/game folders** so their `coop.ini`, saves, settings and logs do not collide.
You can copy the finished kit into a second folder after building, with both games closed. Give each player a
different co-op name through the original Host/Join setup. Connect both controllers **before launching either game**.

In one PowerShell window:

```powershell
& 'C:\oot-coop-p1\Launch-Hookshot-Controller.ps1' -Controller 1 -Role Host
```

In another:

```powershell
& 'C:\oot-coop-p2\Launch-Hookshot-Controller.ps1' -Controller 2 -Role Join
```

The launcher preserves the original kit's server/invite/name/password flow, sets `HOS_CONTROLLER` only for its child
processes, and enables the existing background-controller setting. It restores the calling shell's environment afterward.
The normal host and join connection steps still apply. This provides two windows, not an automatic split-screen layout.

## Selection behavior

- `HOS_CONTROLLER=1` through `32`: select that **SDL-recognised gamepad's ordinal**. This is not a Windows/XInput slot
  and does not identify a model by name; two identical controllers can be selected separately.
- `none` or `0`: no physical controller for this instance. Unset, empty or `all`: Hookshot's existing merged behavior.
- An invalid value disables controllers and logs a diagnostic. A missing selection waits without reading other pads.
- The selected ordinal locks to one SDL instance ID for the run. Removing another controller or reordering the list
  cannot switch it to a neighbour. **After reconnecting the selected controller, restart that game**; persistent
  hardware identity/reconnect selection is not implemented.
- Only the selected controller is opened. Buttons, triggers, sticks, paddle/touchpad menu controls, gyro, motor rumble
  and HD rumble therefore follow the same device. Explicit selection suppresses the separate XInput merge, including
  with `HOS_XINPUT=1`, so losing a controller or SDL cannot silently admit every Xbox pad.
- SDL's order is local to each process; inspect `hid: controller ... locked to SDL instance ...` and the following
  controller-name line in each game's log. If order differs between launches, exchange the ordinal choices.
- Keyboard/mouse still belong to the focused window under normal settings. Steam Input/controller wrappers can expose
  extra virtual pads or keyboard input; choose the intended SDL gamepad and use a consistent layout for both instances.

## Validation

`tests/test_controller_selection.py` compiles the actual polling function against three fake SDL controllers and checks
buttons, axes, gyro setup candidates, motor/HD rumble, invalid/missing selections, disconnect/list reorder, default merge,
the cached process setting, and the real XInput eligibility expression. Real Windows devices and two-game co-op execution
still need testing. This change does not establish that a PC has enough RAM/GPU capacity for two game instances.

Local validation: ten focused input/SDL/motion/rumble Python checks pass, including background input after providing
the test10 bridge symbols in its fixture. Both changed C units pass a GCC Linux syntax check (instruction-helper
headers replaced by empty game-free placeholders, since these units use none of those helpers). The original kit's
strict patcher applies all four patches in order to the pinned source (23 files). The full upstream test suite could
not be run from this sparse checkout; four GPU test sources are absent locally. No Windows compiler, PowerShell,
physical controllers or playable game are available here, so Windows execution, launcher/installer execution and
two-instance gameplay remain unverified.

Co-op source attribution: the unchanged patches under `patches/` are from
https://github.com/daurosk/oot-remake-multiplayer/tree/v0.1.0-test10/hookshot/patches (GPL-3.0).
Hookshot is by NotRexed and contributors (GPL-3.0-or-later). Controller additions use the same compatible license.
