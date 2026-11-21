# Notice

## What this repository contains

Source code written for the macOS port of this project, and nothing else. The MIT
licence in `LICENSE` covers that code.

## What it does not contain, and what that means for you

**No game assets.** There are no Spine skeletons (`.skel`, `.json`), no atlases, no
texture pages, no fonts, no voice clips or music, and no screenshots or recordings of
any game or its promotional material. Not one byte. The scripts under `macos/res/`
draw the icon and the disk image background; their output is a build product and is
not checked in either. Any pull request adding an asset will be closed.

**No Spine runtime.** spine-cpp is Esoteric Software's, distributed under the Spine
Runtimes License, and using it requires a Spine licence of your own: every user of a
product built on it needs their own Spine Editor licence. This project does not
redistribute the runtime and does not grant you anything with respect to it. Read their
terms and decide for yourself: <https://esotericsoftware.com/spine-licensing>

**Not the Windows sources.** The Mac build compiles the Windows sources it was ported
from. Of those, only the adapter is public, in
[helloyork/shittim-logon](https://github.com/helloyork/shittim-logon). This repository
therefore does not build on its own.

**Placeholder names.** Where the Mac code names a particular character's skeleton or
voice clip, the name is a placeholder (`character_a`, `voice::Who::CharacterA`). The
real identifiers belong to a particular game's content and are not something this
project wrote.

## If you want to run something like this

You need art you have the right to use, and a Spine licence if you use the Spine
runtime to animate it.

## Trademarks and affiliation

This project is not affiliated with, endorsed by, or connected to any game publisher,
developer, or rights holder, or to Apple. Any names appearing in commit history or
discussion are used descriptively.

## Reporting a problem

If you hold rights in something you believe is present here, open an issue or contact
the repository owner and it will be removed promptly. The design intent is that there
is nothing to remove.
