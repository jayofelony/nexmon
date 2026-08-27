# bcm43455c0 / 7.45.265 port — progress

Rig: target chip is `wlan1` on the Pi (`pi`/`raspberry`, passwordless sudo). Management
link is the AWUS036AXM on `wlan0`, `192.168.1.131`, carrying the default route — verified
before every stage. `wlan1` is `nmcli`-unmanaged so NetworkManager never grabs it back.

- [DONE] Stage 0 — device prep: `werk` (NM profile bound to wlan1) autoconnect disabled,
  `wlan1` set unmanaged, stock firmware backed up to `/root/cyfmac43455-sdio.stock.bin`
  (md5 `64410bcb1364a794ce4946bc40c7998f`, matches live alternative), boot-time fallback
  service `nexmon-fallback.service` installed+enabled, packages installed
  (gawk, dkms, libnl-3-dev, libnl-genl-3-dev). `nexutil` not yet built (Stage 5b).
- [DONE] Stage 1 — firmware directory: `derive.py` self-tests pass (206/234_CY/241 all
  OK), `definitions.mk`/`Makefile`/`structs.h`/`NOTES.md` written,
  `patches/include/firmware_version.h` has `FW_VER_7_45_265 115`. Build verified:
  ucode.bin 53160B (0xCFA8) starts with ucode magic, templateram.bin 2272B (0x8E0),
  flashpatches.c has 255 entries all < 0xB0000.
- [TODO] Stage 2 — patch directory (`patches/bcm43455c0/7_45_265/`): copy sources from
  206 (injection/monitormode/sendframe/ioctl/patch/console) and 234_CY (version.c
  scaffolding + Makefile), retarget every `at()` to the 265 addresses in the plan's
  tables, comment out `wlc_monitor_amsdu_patch`, add 5 wrapper.c AT() lines, disassemble
  every new address before building.
- [TODO] Stage 3 — build (`make clean && make firmware-only`), sanity-check image size
  and diffed regions against stock.
- [TODO] Stage 4 — wrapper audit (`gen/nexmon.pre` DUMMY cross-check); loop with Stage 2
  until clean. Do not deploy until this passes.
- [TODO] Stage 5 — deploy (5a boot, 5b console/ioctl via nexutil, 5c monitor mode +
  tcpdump cross-checked against wlan0, 5d injection cross-checked against wlan0).
- [TODO] Stage 6 — `wlc_monitor_amsdu_patch` address (optional, only after 5c/5d pass).

Next concrete action: Stage 2, copy+retarget patch sources.

Plan file: `/home/jayofelony/.claude/plans/i-need-you-to-bubbly-bonbon.md` (all address
tables, confidence levels, and verification commands live there — this file only tracks
stage status).
