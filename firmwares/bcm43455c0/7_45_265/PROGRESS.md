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
- [DONE] Stage 2 — patch directory (`patches/bcm43455c0/7_45_265/`): sources copied from
  206 (injection/monitormode/sendframe/ioctl/patch/console) and 234_CY (version.c
  scaffolding + Makefile), all `at()` retargeted to 7.45.265 addresses,
  `wlc_monitor_amsdu_patch` commented out (Stage 6, not yet derived), 5 wrapper.c
  `FW_VER_7_45_265` lines added (memcpy, wl_send, wl_sendup, wl_monitor, chan2freq).
  Every address confirmed by disassembly against the live 265 image before building:
  - `0x1A7604` disassembles to exactly `bl 0x1a323c` (the wl_monitor call site) - exact.
  - `0x1A323C` (wl_monitor) internally calls `bl 0x19a0f8` and tail-calls `b.w 0x1a2f68`
    - i.e. it independently corroborates the memcpy (0x19A0F8) and wl_sendup (0x1A2F68)
    addresses at the same time, resolving what was the weakest-evidence address in the
    plan (only 16B signature match) to very high confidence.
  - `0x202AAC`/`0x202AC8` both currently hold `mov.w rX, #1024` (0x400) - exactly the
    "console buffer size" pattern 206 doubles to 0x800, at both sites.
  - `0x20B988` holds `0x203b9` (wlc_ioctl+1) and `0x200E20` holds `0x1a2831`
    (wl_send+1) - both exact.
  All five addresses check out; none needed correction from the planning-stage values.
- [DONE] Stage 3 — build. Hit one real (non-address) bug: the 234_CY Makefile we based
  ours on dropped `-Wno-address-of-packed-member` (234_CY never compiles radiotap.c,
  since it has no monitor mode; our port does). Fixed by adding the flag back. Clean
  build after that. Verified: image size exactly matches stock (609309B); no linker
  errors (only expected gc-sections notes); version string shows
  `7.45.265 (28bca26 CY nexmon.org: ...)`; full byte-diff against stock shows 36378
  changed bytes and **zero unexplained** - every one falls inside the ucode/patch/
  fp_config regions or a known GenericPatch4 slot. Added
  `patches/bcm43455c0/7_45_265/nexmon/.gitignore` (matching the `7_45_98` precedent) to
  keep the built binary and BUILD_NUMBER untracked.
- [DONE] Stage 4 — wrapper audit. 266 weak (`W`) symbols in `gen/patch.elf`; 233 are
  `b_flash_patch_N` (expected stock ROM flashpatch entries, ignored per the audit method),
  the remaining 33 all appear as `DUMMY` in `gen/nexmon.pre`
  (bcm_binit/bcm_bprintf/free/hndrte_free_timer/hndrte_init_timer/hndrte_schedule_work/
  hndrte_time_ms/lb_alloc/memcpy/memset/pkt_buf_free_skb/pkt_buf_get_skb/printf/
  si_corereg/sprintf/strlen/strncmp/udelay/vsnprintf/wlc_bmac_write_template_ram/
  wlc_enable_mac/wlc_ioctl/wlc_iovar_op/wlc_pcb_fn_register/wlc_phy_chan2freq_acphy/
  wlc_queue_80211_frag/wlc_recv/wlc_scan_ioctl/wlc_sendctl/wlc_suspend_mac_and_wait/
  wl_monitor/wl_send/wl_sendup). **Zero real findings.** No silently-missing wrapper
  in this build - safe to deploy.
- [IN PROGRESS] Stage 5 — deploy. `scripts/deploy265.sh` / `scripts/revert265.sh` written
  and both tested (revert-while-stock-was-already-loaded confirmed the mechanism works
  and re-attaches cleanly, before ever loading the patched image).
  - [DONE] 5a boot: first deploy attempt succeeded outright -
    `Firmware: BCM4345/6 wl0: ... version 7.45.265 (28bca26 CY nexmon.org: 1065-dirty-1)`,
    no TRAP/corrupted/not-responding. wlan1 attached, wlan0 management link undisturbed.
  - [TODO] 5b console/ioctl via nexutil (need to build nexutil on the Pi first -
    libnexio then nexutil, libnl-3-dev already installed in Stage 0).
  - [TODO] 5c monitor mode + tcpdump, cross-checked against wlan0 (mt7921 can capture
    the same air independently).
  - [TODO] 5d injection, cross-checked against wlan0.
- [TODO] Stage 6 — `wlc_monitor_amsdu_patch` address (optional, only after 5c/5d pass).

Next concrete action: Stage 5, deploy (5a boot check first).

Plan file: `/home/jayofelony/.claude/plans/i-need-you-to-bubbly-bonbon.md` (all address
tables, confidence levels, and verification commands live there — this file only tracks
stage status).
