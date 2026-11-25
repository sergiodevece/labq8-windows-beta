# IAQ8 assets

## ui/iaq8_chassis.png (1672 x 941)
IAQ8 chassis, VitaPlug's own artwork. Derived from LABQ8's `assets/ui/background.png`
by `docs/superpowers/specs/iaq8-mockup-v2/make_chassis.py` (adds the "IAQ8" logo and
IAQ8 panel layout). Regenerate from the repo root (needs numpy and Pillow):

    python docs/superpowers/specs/iaq8-mockup-v2/make_chassis.py assets/ui/background.png iaq8/assets/ui/iaq8_chassis.png

The result must be 1672 x 941.

## fonts/ (SIL Open Font License 1.1)
Downloaded from the official google/fonts repository (`ofl/` folder), with the
author's permission:

| File | Source path | Licence text |
|---|---|---|
| VT323-Regular.ttf | ofl/vt323 | OFL-VT323.txt |
| Barlow-Medium.ttf | ofl/barlow | OFL-Barlow.txt |
| Barlow-SemiBold.ttf | ofl/barlow | OFL-Barlow.txt |
| BarlowSemiCondensed-SemiBold.ttf | ofl/barlowsemicondensed | OFL-BarlowSemiCondensed.txt |

## Other sprites
`meter_lit.png`, `mode_face_lit.png`, `mode_face_unlit.png`, `background.png` are
LABQ8's own files from `../assets/ui/`, embedded by reference (not copied) into the
`iaq8_ui_assets` binary-data target (header `LabQ8Assets.h`, namespace `LabQ8Assets`).
