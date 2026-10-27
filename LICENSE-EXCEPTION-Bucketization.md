# Bucketization License Exception

Copyright (C) 2026 Attila Torda

SpeedUp, including Bucketization, is licensed under the GNU Affero General
Public License, version 3 (see [LICENSE](LICENSE)). That stays as it is.
This file adds an **additional permission** under section 7 of that license,
so that the open-source projects listed below can take the Bucketization
code under their own licenses.

## 1. Covered code

- `include/speedup/bucketization.hpp`
- everything under `include/speedup/bucketization/`
- everything under `bindings/java/bucketization/src/`
- the design and specification in `docs/bucketization/README.md`

"Covered code" means these files, any part of them, and any work based on
them, including modified versions and translations into other programming
languages (for example a C, Go or Rust port).

## 2. Eligible projects

| Project | Repository | License (as of 2026-10-27) |
|---|---|---|
| Dolphin (KDE) | <https://invent.kde.org/system/dolphin> | GPL-2.0-or-later |
| KIO (KDE Frameworks) | <https://invent.kde.org/frameworks/kio> | LGPL-2.0-only / LGPL-2.1-or-later |
| Krusader (KDE) | <https://invent.kde.org/utilities/krusader> | GPL-2.0-or-later |
| Thunar (Xfce) | <https://gitlab.xfce.org/xfce/thunar> | GPL-2.0-or-later |
| Nemo (Linux Mint) | <https://github.com/linuxmint/nemo> | GPL-2.0-or-later |
| Caja (MATE) | <https://github.com/mate-desktop/caja> | GPL-2.0-or-later |
| Nautilus / GNOME Files | <https://gitlab.gnome.org/GNOME/nautilus> | GPL-3.0-or-later |
| GLib | <https://gitlab.gnome.org/GNOME/glib> | LGPL-2.1-or-later |
| GTK | <https://gitlab.gnome.org/GNOME/gtk> | LGPL-2.1-or-later |
| PCManFM-Qt (LXQt) | <https://github.com/lxqt/pcmanfm-qt> | GPL-2.0-or-later |
| libfm-qt (LXQt) | <https://github.com/lxqt/libfm-qt> | LGPL-2.1-or-later |
| yazi | <https://github.com/sxyazi/yazi> | MIT |
| lf | <https://github.com/gokcehan/lf> | MIT |
| nnn | <https://github.com/jarun/nnn> | BSD-2-Clause |
| Godot Engine | <https://github.com/godotengine/godot> | MIT |

The licenses are listed for information. What counts is the license the
project actually uses for the file or component that receives the code.

## 3. Permission

As an additional permission under section 7 of the GNU Affero General Public
License, version 3, the copyright holder grants you permission to copy,
modify and distribute covered code **as part of an eligible project**, under
the license that project uses for the file or component containing it,
instead of the GNU Affero General Public License.

The only condition is that you keep the copyright notice
"Copyright (C) 2026 Attila Torda" with the covered code, as every one of
those licenses requires anyway. A pointer to the origin, such as
"Based on Bucketization, https://github.com/attilatorda/SpeedUp", is welcome
but not required.

## 4. What follows from it

- Once covered code is part of an eligible project, everyone who receives it
  from that project, including its forks and downstream distributions, gets
  it under that project's license, with all the rights that license gives.
- This permission does not change the license of SpeedUp itself. Outside the
  eligible projects, the covered code stays under the GNU Affero General
  Public License, version 3.
- As section 7 allows, you may remove this additional permission from your
  own copies of SpeedUp.
