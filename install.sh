#!/bin/bash
# PiKnytt: installs Knytt on 64-bit Raspberry Pi OS.
#
# Run it from the Pi's desktop, from the folder it came in.
# The game goes into ~/Games/Knytt, with an entry in the Games menu and a shortcut on
# the desktop. Run it again to reinstall or remove.
set -u
: "${HOME:?}"

TITLE="PiKnytt"
GAME="Knytt"
HERE=$(dirname "$(readlink -f "$0")")
RUNTIME="$HERE/runtime.tar.xz"
RUNTIME_FILES=505   # entries in runtime.tar.xz, for the progress bar
ICON="$HERE/icon.png"
EXE="$HERE/Knytt.exe"
FILTER="$HERE/Scale.sft"            # sizes the picture; tools/scale.c
PATCH="$HERE/tools/patch-game.py"   # adds the filter to a copy of the game
DEST="$HOME/Games/Knytt"
# The Pi's taskbar finds a window's icon through a menu entry named after the window's
# class, stdrt.exe for this game, the engine Knytt unpacks and runs. The desktop shortcut carries the same name.
MENU="${XDG_DATA_HOME:-$HOME/.local/share}/applications/stdrt.exe.desktop"
SHORTCUT="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")/stdrt.exe.desktop"
# The names both had in earlier versions, cleared on install and on remove.
OLD_MENU="$(dirname "$MENU")/knytt.desktop"
OLD_SHORTCUT="$(dirname "$SHORTCUT")/knytt.desktop"
BOOTCFG=/boot/firmware/config.txt

if ! command -v zenity >/dev/null || [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
  echo "Run this installer from the Raspberry Pi desktop. It needs zenity (sudo apt install zenity)." >&2
  exit 1
fi

# A page with up to three buttons, left to right: --cancel-label, --ok-label, --extra-button.
# OK returns 0. The extra button prints its label and returns 1, like Cancel.
page() { zenity --question --title="$TITLE" --width=560 --icon="$ICON" "$@" 2>/dev/null; }
note() { zenity --info --title="$TITLE" --width=560 --icon="$ICON" --text="$1" 2>/dev/null; }
fail() { zenity --error --title="$TITLE" --width=560 --no-markup --text="$1" 2>/dev/null; exit 1; }
busy() { zenity --progress --title="$TITLE" --width=560 --no-cancel --auto-close "$@" 2>/dev/null; }
as_root() { if sudo -n true 2>/dev/null; then sudo "$@"; else pkexec "$@"; fi; }

# Prints progress for the bar on stdout (a number, or "# text"); errors go to stderr.
install_files() (
  set -e -o pipefail
  mkdir -p "$DEST/game" "$(dirname "$MENU")" "$(dirname "$SHORTCUT")"
  rm -rf "$DEST/wine" "$DEST/prefix"
  echo "# Unpacking Wine, which runs the game..."
  count=0
  tar -xvJf "$RUNTIME" -C "$DEST" | while read -r _; do
    count=$((count + 1))
    if [ $((count % 8)) -eq 0 ] && [ "$count" -lt "$RUNTIME_FILES" ]; then echo $((count * 90 / RUNTIME_FILES)); fi
  done
  echo 92
  echo "# Copying $GAME..."
  python3 "$PATCH" "$EXE" "$FILTER" "$DEST/game/Knytt.exe"
  echo 96
  echo "# Adding the menu entry and the desktop shortcut..."
  cp "$ICON" "$DEST/icon.png"
  cat >"$MENU" <<EOF
[Desktop Entry]
Type=Application
Name=$GAME
Comment=An exploration platformer by Nifflas
Exec="$DEST/run.sh"
Icon=$DEST/icon.png
Terminal=false
Categories=Game;
StartupWMClass=stdrt.exe
EOF
  rm -f "$SHORTCUT" "$OLD_MENU" "$OLD_SHORTCUT"
  cp "$MENU" "$SHORTCUT"
  "$DEST/wine/bin/wine" --version >&2
)

[ "$(dpkg --print-architecture 2>/dev/null)" = arm64 ] ||
  fail "This installer is for 64-bit Raspberry Pi OS. This system is $(uname -m)."
[ -f "$RUNTIME" ] && [ -f "$ICON" ] && [ -f "$EXE" ] && [ -f "$FILTER" ] && [ -f "$PATCH" ] ||
  fail "A file that belongs next to install.sh is missing. Download the whole package again."

if [ -e "$DEST/run.sh" ]; then
  choice=$(page --text="<b>$GAME is already installed on this Raspberry Pi.</b>\n\nReinstall sets it up again and keeps your saved games. Remove deletes the game, its saved games and its shortcuts." \
    --cancel-label="Cancel" --ok-label="Reinstall" --extra-button="Remove")
  status=$?
  if [ "$choice" = "Remove" ]; then
    rm -rf "$DEST" "$MENU" "$SHORTCUT" "$OLD_MENU" "$OLD_SHORTCUT"
    note "$GAME has been removed from this Raspberry Pi."
    exit 0
  fi
  [ "$status" -eq 0 ] || exit 0
else
  page --text="<b>Welcome to the $TITLE Installer</b>\n\nThis installer will set up $GAME, the exploration platformer by Nifflas, on this Raspberry Pi. It puts the game in the Games folder in your home folder, adds it to the Games menu and places a shortcut on the desktop.\n\nThe game needs about 150 MB of space." \
    --cancel-label="Cancel" --ok-label="Install" || exit 0
  [ "$(df -k --output=avail "$HOME" | tail -1)" -ge 200000 ] ||
    fail "There is less than 200 MB free in your home folder. Free up some space and run the installer again."
fi

# Wine needs 4K memory pages. A Pi 5 or Pi 500 starts a 16K kernel unless told otherwise.
if [ "$(getconf PAGESIZE)" != 4096 ]; then
  [ -f "$BOOTCFG" ] && [ -f /boot/firmware/kernel8.img ] ||
    fail "This system uses $(getconf PAGESIZE)-byte memory pages. Wine, which runs the game, needs 4096-byte pages."
  if grep -q '^kernel=kernel8.img' "$BOOTCFG"; then
    note "This Raspberry Pi is already set to start its 4K kernel. Restart it, then run the installer again."
    exit 0
  fi
  page --text="<b>This Raspberry Pi needs its 4K kernel for the game.</b>\n\nA Pi 5 or Pi 500 starts a kernel that uses 16K memory pages, and Wine, which runs the game, needs 4K pages. The installer will switch this Pi to the 4K kernel that is already on it, by adding three lines to $BOOTCFG.\n\nThe change takes effect after a restart. Run the installer again afterwards." \
    --cancel-label="Cancel" --ok-label="Switch Kernel" || exit 0
  printf '\n# 4K-page kernel, needed by Wine (added by the PiKnytt installer)\n[pi5]\nkernel=kernel8.img\n[all]\n' |
    as_root tee -a "$BOOTCFG" >/dev/null || fail "The installer could not change $BOOTCFG."
  page --text="<b>The kernel is switched.</b>\n\nRestart this Raspberry Pi, then run the installer again." \
    --cancel-label="Later" --ok-label="Restart Now" && as_root systemctl reboot
  exit 0
fi

log=$(mktemp)
{ install_files 2>"$log"; status=$?; echo 100; exit $status; } | busy --percentage=0 --text="Installing $GAME..."
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
  fail "The install did not finish. Wine needs 64-bit Raspberry Pi OS 13 (trixie) or newer.

$(tail -5 "$log")"
fi
rm -f "$log"

page --text="<b>$GAME is installed.</b>\n\nIt is in the Games menu and on the desktop. The Game menu has three window sizes and Full screen. Alt+Enter switches full screen on and off." \
  --cancel-label="Close" --ok-label="Play Now" && setsid "$DEST/run.sh" >/dev/null 2>&1 &
