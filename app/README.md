# InkTerm setup and build guide

This guide covers the experimental source version. For an overview, controls and limitations, see the [repository README](../README.md). The version is recorded in [VERSION](VERSION).

All host commands below run from the repository root unless stated otherwise. Paths beginning with `/mnt/us` refer to the Kindle's USB storage as seen by the Kindle itself.

## Building and testing

### Check the source without a Kindle

```sh
python3 app/tools/check_release.py
python3 app/tools/repair_sdk.py --check
```

Both commands use Python's standard library. `repair_sdk.py --check` exercises a synthetic ELF sample; it does not modify your toolchain.

To check UTF-8 input, modifiers, layout selection and connection validation, use a C compiler, `pkg-config` and GLib development files:

```sh
cc $(pkg-config --cflags glib-2.0) -Iapp/src \
  app/src/input.c app/tools/check_input.c \
  $(pkg-config --libs glib-2.0) -o /tmp/inkterm-input-check
/tmp/inkterm-input-check app/layouts
```

### Prepare the native dependencies

Cross-compilation requires a Linux build environment with:

- A prepared KindleModding `arm-kindlehf-linux-gnueabihf` toolchain and target sysroot.
- Target GTK2, GLib/GIO, X11 and D-Bus headers, libraries and `pkg-config` metadata.
- VTE 0.28.2 built as a static library, with headers available in its source tree.
- GNU termcap 1.3.1 built for the target and installed in the toolchain sysroot.

The app links VTE and termcap statically and uses the Kindle's platform libraries dynamically. Dependency downloads, configuration and compilation are not automated in this repository. The build command below assumes those steps are complete. See [dependency sources and notices](../THIRD_PARTY.md).

The tested SDK contains incorrect ELF symbol section indices in some firmware-derived libraries. The repair helper fixes those indices in the **local SDK**, retaining `.elf-backup` files. It must not be run against device system libraries.

```sh
kindle_sdk="/path/to/arm-kindlehf-linux-gnueabihf"
vte_source="/path/to/vte-0.28.2"
python3 app/tools/repair_sdk.py \
  "$kindle_sdk/arm-kindlehf-linux-gnueabihf/sysroot"
sh app/tools/build.sh "$kindle_sdk" "$vte_source"
```

The executable is written to `app/bin/inkterm`. A successful cross-build alone does not verify runtime compatibility with a different Kindle model.

## Install on the Kindle

### 1. Assemble the extension

With Kindle USB storage mounted, place the native files in this structure:

```text
extensions/inkterm/
  bin/
    inkterm          compiled native app
    dropbearmulti    compatible ARM Dropbear binary with dbclient
    start.sh         from app/bin/start.sh
    connect.sh       from app/bin/connect.sh
  layouts/           all app/layouts/*.ini files
  terminfo/          compatible terminal definitions, including xterm
  config.xml         from app/config.xml
  menu.json          from app/menu.json
  config/            created or populated during provisioning
```

The tested client is Dropbear 2024.85. Neither it nor the terminfo files are supplied by the source package. Obtain them from your prepared dependency build. The native executable, Dropbear client and shell scripts must be executable on the device.

KUAL uses `config.xml` and `menu.json` to launch the app. For a jailbreak that supports library scriptlets, also copy `app/bin/start.sh` to `documents/inkterm.sh`. Library scriptlet support is a separate requirement; it is not provided by InkTerm.

### 2. Prepare SSH access

Before provisioning, have all of these ready:

- The Mac's reachable hostname or IP, SSH port and account name.
- A dedicated client private key in **Dropbear format**. Renaming an OpenSSH key does not convert it.
- The corresponding public key installed in the Mac account's `authorized_keys`.
- A `known_hosts` file containing the verified host public key for that endpoint, including its port when non-default.

Verify the host-key fingerprint through a trusted source on the host. Merely collecting a key from the network does not establish its identity. Keep strict host-key checking enabled.

The account must be able to run `/usr/bin/screen`. InkTerm currently invokes that exact path on the host. It does not configure SSH access or install `screen` for you.

### 3. Provision the connection

This example uses a documentation-only IP and a sample account. Replace them, the mounted volume path and key-file paths with your own values:

```sh
python3 app/tools/provision.py /Volumes/Kindle \
  --key client.dropbear \
  --known-hosts known_hosts \
  --host 192.0.2.10 \
  --user reader \
  --port 22 \
  --session kindle
```

The helper checks for an installed app, copies the supplied key and host-key file, and writes `config/settings.ini`. It enables English and sets the default font size. **Running it again replaces those files and resets the saved display/keyboard preferences.**

The private key resides on Kindle USB storage. It is readable when that storage is mounted. Use a dedicated key that you can revoke independently if the device is lost.

### 4. Launch and confirm

Eject USB storage, enable Wi-Fi and open InkTerm through KUAL or the supported library launcher. The Mac must be awake and reachable.

When connected, type `pwd` and press Enter. The result should be a directory on the Mac. Close and reopen InkTerm to confirm that it attaches to the same remote session.

Host-side attachment to the default session, from the same Mac account:

```sh
/usr/bin/screen -U -x kindle
```

Use your configured session name if you changed it. The session can outlive the SSH connection; host shutdown or terminating the session still ends its processes.

## Configuration and files

Settings → Connection edits the host, user, port and session. Save & connect saves those values and reconnects. Settings → Keyboard manages enabled languages and font size. See the [controls table](../README.md#controls) for typing and program controls.

| Device path, relative to `extensions/inkterm` | Purpose |
| --- | --- |
| `config/settings.ini` | Connection, display and keyboard preferences |
| `config/client.dropbear` | Private client key supplied during provisioning |
| `config/known_hosts` | Authoritative pinned host keys supplied during provisioning |
| `config/ssh-home/.ssh/known_hosts` | Runtime copy replaced on each launch |
| `config/app.log` | Launcher, SSH and application diagnostics |

The SSH home is isolated to the InkTerm child process. Launching the app does not append keys to another application's `known_hosts`. Updating keys requires replacing the corresponding files in `config/` and relaunching. Keys, settings and logs must never be included in a release archive.

## Adding a keyboard layout

Create a UTF-8 `.ini` file in `app/layouts/`. For example, the English layout uses this structure:

```ini
[layout]
name=EN
row1=qwertyuiop
row2=asdfghjkl
row3=zxcvbnm
```

Each row must contain 1 to 16 printable Unicode characters. `name` is the short label on the language-switch key. Uppercase is derived using GLib's Unicode case mapping. Layout files define letters; symbol pages and terminal control keys are implemented in `src/main.c`.

Add the file to the Git and package allowlists, run the native input check, then copy it to the installed `layouts/` directory and restart InkTerm. Enable it in Settings → Keyboard. At least one layout must remain enabled. This format does not implement input-method composition or dead-key sequences.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| InkTerm does not open | Check launcher support, executable permissions, target ABI and `config/app.log` |
| SSH key missing | Run provisioning with a Dropbear-format private key |
| Cannot connect | Check the saved endpoint, host wake state, SSH server and network reachability |
| Host-key verification fails | Verify the host's fingerprint and the endpoint/port in `config/known_hosts`; replace the pin only after verification, then relaunch |
| Letters remain uppercase | Tap Shift to release Caps Lock; double-tap is the Caps Lock gesture |
| Ctrl plus a letter produces no input | Select English for ASCII Ctrl shortcuts; the dedicated Ctrl+C key works in any layout |
| A program is still there after reopening | Close preserves the host session; exit or detach using the remote program's own controls |
| Prompt moves down when hiding the keyboard | Known behavior of macOS `screen` 4.00.03 during resize; see the repository limitations |

Before sharing logs or screenshots, remove host details, usernames, private paths and terminal content that should remain private.

## Device verification and source packaging

On a device with the app installed, run the input self-test without opening a window:

```sh
/mnt/us/extensions/inkterm/bin/inkterm --self-test
```

It checks input logic and layout files. For a GUI change, also verify typing, modifiers, language switching, navigation, keyboard hide/show, settings, reconnect and Close on the actual device. Check both whether the host PTY resizes and where its text appears.

Create the source archive from the repository root:

```sh
python3 app/tools/check_release.py
python3 app/tools/package.py
```

The output is `dist/inkterm-<version>-source.zip`. Packaging uses an exact source allowlist, deterministic ZIP metadata and fresh temporary storage. It excludes compiled dependencies, credentials and device state. It does not create a Kindle installer.
