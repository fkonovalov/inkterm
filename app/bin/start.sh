#!/bin/sh
# Name: InkTerm
# DontUseFBInk
base=/mnt/us/extensions/inkterm
umask 077
inkterm_ssh_home="$base/config/ssh-home"
mkdir -p "$inkterm_ssh_home/.ssh" || exit 1
exec >>"$base/config/app.log" 2>&1
if test -f "$base/config/known_hosts"; then
    cp "$base/config/known_hosts" "$inkterm_ssh_home/.ssh/known_hosts" || exit 1
else
    : > "$inkterm_ssh_home/.ssh/known_hosts" || exit 1
fi
chmod 700 "$inkterm_ssh_home" "$inkterm_ssh_home/.ssh"
chmod 600 "$inkterm_ssh_home/.ssh/known_hosts" "$base/config/client.dropbear" 2>/dev/null || true
export TERM=xterm TERMINFO="$base/terminfo" DISPLAY=:0
exec env HOME="$inkterm_ssh_home" "$base/bin/inkterm"
