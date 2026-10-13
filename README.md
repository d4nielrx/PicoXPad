# PicoXPad

Turn your **Pico 4** controllers into a system-wide **Xbox One controller**.

A Magisk module (root) that reads the Pico controllers through the headset's own
controller service and re-emits every input as a virtual Xbox One Wireless
Controller (`045e:02e0`). Every Android app - games, emulators, streaming apps
like DroidDeck - sees a normal Xbox pad.

**Hold both grips (~0.5s) to toggle Xbox mode ON/OFF.**

## Status

Works on a rooted **Pico 4 (A8110), Android 10 / API 29** with Magisk.

- All buttons, both sticks, both analog triggers - calibrated and verified
- Toggle ON = controllers only drive the game, OFF = normal Pico controllers
- Emulation starts **OFF** after every reboot

Other Pico models/firmware are untested - the controller data layout was
reverse-engineered and may differ.

## Toggle

| Action | How |
|---|---|
| Enable / disable Xbox mode | **Squeeze and hold both grips** (the middle squeeze button on each controller) for ~0.5 seconds |

The controllers give a short haptic click on toggle (when awake).
Current state is published to `/data/local/tmp/picoxpad_status` (`ON` / `OFF`).

## Button mapping

| Pico input | Xbox input |
|---|---|
| A / B (right controller) | A / B |
| X / Y (left controller) | X / Y |
| Grip (left / right) | LB / RB |
| Trigger (left / right) | LT / RT (analog, full range) |
| Thumbstick (left / right) | Left / Right stick (full 6-axis) |
| Thumbstick click | L3 / R3 |
| Small button, left controller (menu) | Start |
| Small button, right controller (home) | Guide (Xbox button) |
| Both grips held ~0.5s | **Toggle emulation** |

Touchpad taps are deliberately **not** mapped: the capacitive touch sensors
flicker while merely holding the controller, which would spam input.

## Installation

### Pre-built

1. Download `PicoXPad-vX.X.zip` from Releases
2. Transfer it to the headset (ADB, USB, cloud)
3. Magisk app -> **Modules** -> **Install from storage** -> pick the ZIP
4. **Reboot**

### Build from source

Requires the Android NDK (r27 tested). On Windows there is a helper:

```powershell
.\download_ndk.ps1      # optional: installs NDK r27 to C:\Android\ndk
.\build_and_run.ps1     # or: .\build_and_run.ps1 -NdkPath <your ndk>
```

On Linux/macOS:

```bash
export ANDROID_NDK_HOME=/path/to/ndk
./build.sh
```

Both produce `PicoXPad-vX.X.zip` in the project root. The version comes from
`module.prop`.

> The ZIP layout matters on this device: `picoxpad_daemon` must sit at the ZIP
> **root**, not in a subdirectory (subdirectories don't extract properly). Both
> build scripts do this correctly.

### On-device install script

```bash
adb push install.sh /data/local/tmp/
adb shell su -c "sh /data/local/tmp/install.sh"   # ZIP must be in /sdcard/Download
```

## How it works

```
Pico controllers ──► pxrcontrollerservice (Pico system service)
                          │
                          ▼  libpxrcontrollerclient.pxr.so
                     PicoXPad daemon (root, started by Magisk)
                       • reads 80-byte controller state per slot
                       • maps Pico buttons/axes to Xbox layout
                       • toggle: both grips held ~0.5s
                          │
                          ▼  /dev/uinput
                     virtual "Xbox Wireless Controller" (045e:02e0)
                          │
                          ▼
                     Android input system ──► every app sees an Xbox pad
```

Key trick: the daemon fakes the VID/PID of an Xbox Wireless Controller
(`045e:02e0`), so Android translates all events through the stock
`/system/usr/keylayout/Vendor_045e_Product_02e0.kl`. The daemon emits exactly
the Linux key/axis codes that file expects, which is why buttons land on the
right Xbox functions.

The controller data layout (which byte is which button/stick) was mapped by
calibrating on-device: every button pressed individually and matched to its
byte in the 80-byte `controller_data_t` struct. See the extensive comments in
[`picoxpad_daemon.cpp`](picoxpad_daemon.cpp) for the full verified layout -
including the two gotchas (stick bytes are transposed, and several capacitive
touch bytes must never be mapped).

## Troubleshooting

```bash
# Emulation state
adb shell su -c "cat /data/local/tmp/picoxpad_status"

# Daemon alive?
adb shell ps -A | grep picoxpad

# Daemon logs (start, toggle, controller data dumps)
adb shell logcat -d -s PicoXPad

# Virtual pad visible to Android?
adb shell getevent -p | grep -A4 PicoXPad

# Is the module installed?
adb shell su -c "ls /data/adb/modules/picoxpad"
```

- **Toggle doesn't react**: wake the controllers (move a stick) - the daemon
  ignores stale/asleep controller data, so a toggle while they sleep does nothing.
- **Buttons do nothing in a game**: toggle off/on after the game launched;
  some streaming setups (e.g. DroidDeck on PC side) map the pad themselves and
  may need per-game configuration.
- **Module not starting**: check `/data/local/tmp/picoxpad.log` (written by
  `service.sh`) and that Magisk is v25+.

## Known limitations

- **The Pico system UI still reacts to the physical controllers** while Xbox
  mode is ON (e.g. the home button can still close apps). Pico ships an
  enterprise API to disable controller keys system-wide, but it is a dead stub
  on this firmware, so the system side cannot be muted yet.
- **Haptics**: Xbox vibration is not forwarded to the Pico controllers yet.
- No per-button remapping (layout is fixed, see the mapping table).
- Only tested on Pico 4 (A8110) / Android 10.

## Repository layout

```
picoxpad_daemon.cpp   # the daemon: controller reading, mapping, uinput output
module.prop           # Magisk module metadata (version lives here)
service.sh            # Magisk late_start service: starts the daemon after boot
post-fs-data.sh       # early boot: uinput permissions/node
sepolicy.rule         # SELinux allowances for the daemon
build_and_run.ps1     # Windows build (NDK) -> module ZIP
build.sh              # Linux/macOS build (NDK) -> module ZIP
download_ndk.ps1      # helper: downloads NDK r27 on Windows
install.sh            # optional on-device installer (uses magisk --install-module)
CMakeLists.txt        # alternative build (Android Studio / cmake)
Android.mk            # alternative build (ndk-build)
tools/                # research & calibration utilities (not shipped in the ZIP)
```

## Development

- Change mapping/logic in `picoxpad_daemon.cpp`, then rebuild with
  `build_and_run.ps1` (Windows) or `build.sh`.
- `tools/calib.c` - dumps raw controller structs while pressing buttons (this
  is how the byte layout was mapped).
- `tools/keytest.c` / `tools/kstate.c` - probes of the Pico controller service
  APIs (including the enterprise key-disable API, found to be a stub here).
- Update the ZIP by installing it via Magisk and rebooting.

## Credits & license

MIT - see [LICENSE](LICENSE). Built from reverse-engineering the Pico 4
controller stack; inspired by the gamepad emulation other XR headsets gained
natively.

---

**Disclaimer**: modifies system input behavior on a rooted device. Use at your
own risk; keep ADB access available in case you need to uninstall the module
(`magisk --remove-module picoxpad` or delete `/data/adb/modules/picoxpad` in
recovery).
