Reference firmware, not yet patch-ready.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/cypress/cyfmac43455-sdio-standard.bin`
- Internal chip string: `43455c0-roml/...`
- Version: `7.45.265 (28bca26 CY)`, CRC `68bafb8c`
- Date: Tue 2023-08-29 01:51:02 PDT
- Fetched: 2026-08-20

Newer "standard" build than the already-patched `7_45_241` version in this
repo (which is the "minimal" build, version `7.45.241`, dated 2021-11-01 -
also present in `RPi-Distro/firmware-nonfree` if a minimal-build port is
preferred instead). No `definitions.mk`/`structs.h`/patch code exists for
this specific `7_45_265` build yet - the `7_45_241` and `7_45_234_4ca95bb_CY`
directories in this same chip are the closest existing reference for what a
port would need to produce.
