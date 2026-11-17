#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print the steps for adding the AppImage to Steam as a non-Steam game
# (docs/HANDHELD.md). It only prints: it never reads or writes Steam's
# files (no shortcuts.vdf, no config), and changes nothing on the system.
#
#   packaging/steam/add-to-steam.sh [PATH/TO/CT_Recomp-x86_64.AppImage]
#
# Without an argument it looks for the AppImage in ~/Applications,
# ~/Downloads, ~/Documents, ~ and build-appimage/ in this checkout.
set -eu

name=CT_Recomp-x86_64.AppImage
img=${1:-}
if [ -z "$img" ]; then
    here=$(cd "$(dirname "$0")/../.." 2>/dev/null && pwd || echo .)
    for d in "$HOME/Applications" "$HOME/Downloads" "$HOME/Documents" "$HOME" \
             "$here/build-appimage"; do
        if [ -f "$d/$name" ]; then
            img="$d/$name"
            break
        fi
    done
fi
if [ -n "$img" ] && [ -f "$img" ]; then
    img=$(readlink -f "$img")
    found=1
else
    img="$HOME/Applications/$name"
    found=0
fi
dir=$(dirname "$img")
art=$(cd "$(dirname "$0")" 2>/dev/null && pwd || echo packaging/steam)/artwork
# The recommended Steam Input layout, next to this script (docs/HANDHELD.md).
vdf=$(cd "$(dirname "$0")" 2>/dev/null && pwd || echo packaging/steam)/ct-recomp_neptune.vdf

q() {   # quote for Steam's fields: double quotes around paths with spaces
    printf '"%s"' "$1"
}

cat <<EOF
Adding CT Recomp to Steam (Steam Deck, ROG Ally, any Linux PC)
==============================================================
EOF
if [ "$found" = 0 ]; then
    cat <<EOF

The AppImage was not found. These steps assume it is at:
  $img
Put it there (or pass its path: $0 PATH), then follow the steps.
EOF
fi
cat <<EOF

1. Make the AppImage executable (once). In a terminal (Konsole):

     chmod +x $(q "$img")

2. In Desktop Mode, open Steam and choose:
     Games > Add a Non-Steam Game to My Library... > Browse...
   Pick the file (set the file type filter to "All Files" if it is hidden):
     $img
   then "Add Selected Programs".

3. In your library, right-click the new entry > Properties... > Shortcut:
     Name:           CT Recomp
     Target:         $(q "$img")
     Start in:       $(q "$dir")
     Launch options: (leave empty; the original look is the default)
   Optional launch options:
     --hd2d               the HD-2D look (or choose it in the menu, Look tab)
     --preset subtle      a lighter HD-2D look (or: off, hd2d, deck)
     --pacing vsync       pace by the display's vsync (60 Hz screens)

4. Put your ROM in ~/Documents (or ~/Downloads, or next to the AppImage in
   $dir). The first launch finds it, checks it and builds the game code
   on this device: about 40 s on a ROG Ally, longer on a Steam Deck.

5. Controller: right-click > Properties... > Controller > Override for CT
   Recomp: "Gamepad" (Steam Input's standard gamepad layout). A keyboard or
   desktop layout sends keys instead of buttons.

6. Artwork (optional, original art made for this project, GPL-3.0-or-later).
   Right-click the entry > Manage > Set custom artwork, or in Desktop Mode
   open the entry's page and use the image's right-click menu. Files in
   $art:
     Capsule (vertical, 600x900):  $art/grid.png
     Wide capsule (920x430):       $art/capsule_wide.png
     Hero / background (1920x620): $art/hero.png
     Logo (transparent, 1280x720): $art/logo.png
     Icon (256x256):               $art/icon.png
   Pick each file in the matching slot. For the logo, afterwards set its
   position and size on the hero image from the same menu if you like.
7. Back buttons (optional, Steam Deck). Under Steam Input the game sees an
   Xbox-style pad without L4 L5 R4 R5, so bind them to the game's keys:
     L4 = F6 quick save      R4 = F9 quick load
     L5 = Tab fast-forward   R5 = F10 menu
   Either set them by hand (controller icon > Edit Layout > Back Grip
   Buttons > each one > Keyboard), or use the recommended layout:
   copy it into Steam's templates folder yourself (this script does not),
   restart Steam, then pick it under Controller > Templates:

     mkdir -p ~/.steam/steam/controller_base/templates
     cp $(q "$vdf") \\
        ~/.steam/steam/controller_base/templates/

   It is the gamepad layout with those four keys; gyro and trackpads off.
   Without Steam Input (Desktop Mode, started outside Steam) the game reads
   the back buttons itself and needs none of this.

Game Mode tips
--------------
- It starts fullscreen with no mouse cursor. View+Menu held for 2 s asks to
  quit (A: quit, B: back); the Steam button > Exit Game also quits.
- R3 changes the look, L3 the picture shape.
- Steam Deck OLED: in the Performance menu (... button), set the refresh
  rate to 60 Hz; the game runs at 60.1 frames per second and judders at 90.
- ROG Ally: 120 Hz with VRR shows the frames as they come.
- A frame limiter is not needed: the game paces itself.
- Your saves and the built game code are in ~/.local/share/ct-recomp/ct/.

This script changed nothing; Steam's files were not touched.
EOF
