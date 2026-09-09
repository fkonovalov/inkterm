# InkTerm

A native SSH terminal for jailbroken Kindle devices, with a touch keyboard designed for E Ink.

Open a shell, edit code or use a terminal-based coding agent on your Mac while holding your Kindle. The programs run on the Mac. InkTerm handles the display and keyboard input over Wi-Fi.

**Status: experimental, source-only.** The application has been tested on a Kindle Basic 2022 (KT5) running firmware 5.19.2.0.1. A public binary installer and automated dependency setup are not available yet.

[Setup and build guide](app/README.md) · [Controls](#controls) · [Development](#development) · [Known limitations](#known-limitations)

## What works

- Native GTK2/VTE terminal rendering with black text on a white background.
- English, Russian, German, French and Spanish layouts. Enable the languages you need in Settings.
- One-shot Shift, Caps Lock, Ctrl, Alt, punctuation and programming symbols.
- A navigation pad with arrow keys, Home, End, Page Up and Page Down.
- A collapsible keyboard that gives the terminal more screen space.
- SSH key authentication, pinned host keys and a persistent remote session.

There is no autocorrection or automatic capitalization. Typed commands are sent as entered.

## How it works

```mermaid
flowchart LR
    K[Kindle: InkTerm + touch keyboard] <-->|SSH over Wi-Fi| M[Mac: SSH server]
    M <--> S[Persistent screen session]
    S <--> P[Shell and terminal applications]
```

InkTerm starts a Dropbear SSH client and attaches to a named `screen` session on the host. The default session name is `kindle`. Reopening InkTerm attaches to that session again.

**Close exits InkTerm. It leaves the programs on the Mac running.** To stop or leave a remote program, use that program's commands or keyboard shortcuts. The Program menu provides common control keys.

## Getting started

You need the following before using the current source version:

| Component | Requirement |
| --- | --- |
| Kindle | A jailbroken device with compatible GTK2/GLib libraries and a way to launch native applications, such as KUAL |
| Host | An awake Mac with an accessible SSH server and `/usr/bin/screen` |
| Network | A route from the Kindle to the host, usually the same Wi-Fi network |
| Authentication | A dedicated SSH key in Dropbear format, its public key authorized on the host, and a verified host-key entry |
| Native build | A prepared Kindle ARM hard-float toolchain, VTE and termcap, plus a compatible Dropbear client |

The repository provides the app source, keyboard layouts, launch scripts, configuration helper and checks. It does not install a jailbreak, set up the Mac's SSH server or download the build dependencies.

Follow the [setup and build guide](app/README.md) to prepare the native files, configure the connection and launch InkTerm. To inspect or contribute to the source without a Kindle, start with the [development checks](#development).

## Controls

| Control | Action |
| --- | --- |
| Settings → Keyboard | Enable layouts and change terminal text size |
| Language key | Cycle through enabled layouts; hidden when only one is enabled |
| Shift | Uppercase the next letter |
| Double-tap Shift | Enable Caps Lock; tap Shift again to disable it |
| Ctrl / Alt | Apply the modifier to the next key; tap the modifier again to cancel |
| `123` / `#+=` / `ABC` | Switch between letters and symbol pages |
| Nav | Open the directional pad; ABC returns to typing |
| Bottom-right chevron | Hide or show the keyboard without reconnecting |
| Program | Send Ctrl+C, Ctrl+D or Ctrl+Z to the running program |
| Settings → Connection | Edit the host, user, port and session; save or reconnect |
| Close | Exit the Kindle app and preserve the host session |

Ctrl+C usually interrupts a program, Ctrl+D signals end of input, and Ctrl+Z can suspend a shell job. Applications can handle these keys differently. Use `fg` in the shell to resume a suspended job. For letter-based Ctrl shortcuts, select the English layout; the dedicated Ctrl+C button works in every layout.

## Known limitations

- Only the KT5 and macOS combination above has been tested. Other Kindle models, firmware versions and host systems need verification.
- The `screen` 4.00.03 bundled with the tested Mac moves the prompt down when the terminal grows. Hiding the keyboard can therefore leave the prompt partway down the screen.
- E Ink refresh and touch typing limit the experience with rapidly updating applications. This is an experimental terminal for a small display.
- The interface is in English. The supplied layouts do not implement Chinese/Japanese composition, dead keys, predictive input or swipe typing.
- Reconnection is manual through Settings. The host must remain awake and reachable.
- Native dependency setup is not automated. Binary distribution still needs complete dependency sources, build instructions and license notices. See [third-party components](THIRD_PARTY.md).

## Development

Run these commands from the repository root. They require Python 3 and no third-party Python packages:

```sh
python3 app/tools/check_release.py
python3 app/tools/repair_sdk.py --check
python3 app/tools/package.py
```

The first command checks source/archive contents, privacy patterns, version consistency, configuration validation and SSH pin replacement. The second tests the SDK repair logic without modifying an SDK. The third creates `dist/inkterm-<version>-source.zip` using the version in [`app/VERSION`](app/VERSION).

For native input checks and cross-compilation, see [building and testing](app/README.md#building-and-testing). Passing host checks does not establish that a GUI change works on a Kindle.

### Code map

| Path | Responsibility |
| --- | --- |
| [`app/src/main.c`](app/src/main.c) | GTK interface, keyboard rendering, settings and VTE/SSH process lifecycle |
| [`app/src/input.c`](app/src/input.c) | Layout loading, UTF-8 input, modifiers, profile validation and input self-test |
| [`app/layouts/`](app/layouts/) | Declarative keyboard layouts |
| [`app/bin/start.sh`](app/bin/start.sh) | Device launch, isolated SSH home and log setup |
| [`app/bin/connect.sh`](app/bin/connect.sh) | SSH client invocation and attachment to the host session |
| [`app/tools/`](app/tools/) | Build, provisioning, packaging and checks |
| [`app/vendor/kterm/`](app/vendor/kterm/) | Kindle window/orientation integration retained from kTerm |

### Working on this repository as an agent

1. Read the relevant source and the setup guide before changing behavior. Treat `app/VERSION` as the release version, and keep `app/config.xml` in sync.
2. Keep language selection in Settings and preserve predictable terminal input. Do not add autocorrection, app-specific exit buttons or injected shell commands to work around rendering issues.
3. Preserve remote sessions during UI work. Closing the app, stopping a foreground job and terminating a host session are distinct operations.
4. Run the checks above. Run the native input check for input changes. For GUI changes, verify on hardware and state explicitly when hardware verification was unavailable.
5. New publishable files must be added to both `.gitignore` and `source_files()` in `app/tools/package.py`. Both use explicit allowlists. Keep keys, device configuration, logs, photos, local paths and experimental artifacts outside them.
6. Make local changes for review. Commit, push or publish a release only when explicitly requested.

When reporting a device issue, include the Kindle model, firmware, InkTerm version, host system and steps to reproduce. Remove connection details and terminal content you do not want to share.

## License and credits

InkTerm is GPL-3.0-or-later. Kindle platform integration comes from [kTerm](https://github.com/bfabiszewski/kterm), with its original notices preserved. VTE provides the terminal emulator and Dropbear provides SSH.

See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md) for component details and the remaining work before binary distribution.
