#!/bin/sh
set -eu
sdk=${1:?Pass the prepared kindlehf SDK toolchain directory}
vte=${2:?Pass the built VTE 0.28.2 source directory}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=arm-kindlehf-linux-gnueabihf
export PKG_CONFIG_SYSROOT_DIR="$sdk/$target/sysroot"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig"
build="$root/build"
mkdir -p "$build/include" "$root/bin"
ln -sfn "$vte/src" "$build/include/vte"
"$sdk/bin/$target-gcc" -O2 -std=gnu99 -Wall -Wextra -Wno-unused-local-typedefs \
  -DKINDLE -DHAVE_DBUS -I"$root/vendor/kterm" -I"$build/include" \
  $(pkg-config --cflags gtk+-2.0 gio-2.0 x11 dbus-1) \
  "$root/src/main.c" "$root/src/input.c" "$root/vendor/kterm/kindle.c" \
  "$vte/src/.libs/libvte.a" -Wl,--as-needed \
  -Wl,-rpath-link,"$PKG_CONFIG_SYSROOT_DIR/usr/lib" -Wl,-rpath-link,"$PKG_CONFIG_SYSROOT_DIR/lib" \
  $(pkg-config --libs gtk+-2.0 gio-2.0 x11 dbus-1) -ltermcap -lm -lutil -o "$root/bin/inkterm"
"$sdk/bin/$target-strip" "$root/bin/inkterm"
