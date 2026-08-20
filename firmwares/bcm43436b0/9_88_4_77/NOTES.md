Reference firmware, not yet patch-ready.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/brcm/brcmfmac43436-sdio.bin`
- Internal chip string: `43436b0-roml/...`
- Version: `9.88.4.77`, CRC `143f9f15`
- Date: Thu 2022-03-31 17:25:16 CST
- Fetched: 2026-08-20

Newer than the `9_88_4_65` version already patched in this repo. No
`definitions.mk`/`structs.h`/patch code exists for this build yet - see the
existing `9_88_4_65` directory for what a full port would need to produce.
