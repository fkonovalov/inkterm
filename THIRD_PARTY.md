# Third-party components

InkTerm's source tree includes Kindle platform integration from [kTerm v2.6](https://github.com/bfabiszewski/kterm/tree/v2.6), copyright Bartek Fabiszewski. Original notices and the GPL license text remain in `app/vendor/kterm`. The D-Bus reply string ownership has been corrected locally.

The experimental native executable links VTE 0.28.2 and GNU termcap 1.3.1 statically and uses the Kindle's GTK2, GLib, X11 and D-Bus libraries. The development SSH client was extracted from USBNetLite and reports Dropbear 2024.85. These dependencies and firmware libraries are not included in the source archive.

Upstream dependency sources:

- [VTE 0.28.2](https://download.gnome.org/sources/vte/0.28/)
- [GNU termcap 1.3.1](https://ftp.gnu.org/gnu/termcap/)
- [Dropbear](https://github.com/mkj/dropbear)
- [Kindle SDK](https://github.com/KindleModding/kindle-sdk)

Before distributing native binaries, record the exact dependency versions, patches, build commands and source archives, include their required license notices, and review what the SDK permits redistributing. The source-only package command intentionally excludes all compiled dependencies and user configuration.
