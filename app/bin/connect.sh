#!/bin/sh
set -eu
base=/mnt/us/extensions/inkterm
host=$1 user=$2 port=$3 session=$4
case "$session" in ''|*[!a-zA-Z0-9_.-]*) exit 2;; esac
exec "$base/bin/dropbearmulti" dbclient -t -K 30 -o BatchMode=yes -o StrictHostKeyChecking=yes -i "$base/config/client.dropbear" -p "$port" "$user@$host" "/usr/bin/printf '\\033]0;InkTerm connected\\007'; exec /usr/bin/env LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 TERM=xterm /usr/bin/screen -U -xRR -S $session"
