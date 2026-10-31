Patch-ready. Monitor mode hardware-verified; injection not ported.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/brcm/brcmfmac43436s-sdio.bin`
- Internal chip string: `43430a1-roml/...` (same chip family as bcm43430a1, despite
  the "43436s" filename - see `../7_45_96_s1/NOTES.md` for provenance)
- Version: `7.45.96.s1 (gf031a129)`, CRC `6670a1e`
- Date: Wed 2023-06-14 07:28:04 CST

## What this is

A nexmon monitor-mode port for this firmware version, built on top of this
repo's own `7_45_98` port (one version older, same chip) as a template.
`7_45_98`'s own addresses did not transfer cleanly - 96.s1's code has
diverged more than the version-adjacency suggests, so most addresses below
were re-derived from scratch via disassembly (`arm-none-eabi-objdump`),
not simple relocation. Every value in `definitions.mk` and every new
`AT()` entry in `patches/common/wrapper.c` has a comment documenting the
derivation method and confidence level - please read those before trusting
or modifying any of them further.

## Verified on real hardware

Raspberry Pi Zero 2 W, BCM43430/1 chip, kernel `6.18.50+rpt-rpi-v8`. Clean
boot (no firmware trap), `mon0` interface created successfully, real
`tcpdump` capture confirmed: genuine radiotap frames, varying real signal
strengths, diverse real traffic (beacons from multiple APs, vendor action
frames, encrypted data frames, block-acks).

## Known gaps

- **`TEMPLATERAMSTART_PTR`**: could not be located for this firmware
  version (the code surrounding the literal in both reference firmwares
  does not byte-match anywhere in 96.s1, even at 4 bytes - the surrounding
  function appears to have been compiled meaningfully differently given
  96.s1's different build-flag set). The `templateram_bin` relocation
  patch is disabled in `patch.c` rather than guessing a wrong/colliding
  address. Monitor mode works without it; a correctly-relocated template
  RAM region may still be worth pursuing for full correctness.
- **`FP_DATA_END_PTR`**: not independently derived - copied from `7_45_98`'s
  value as a placeholder. Didn't block monitor mode working, but should be
  verified before relying on it.
- **`pkt_buf_free_skb`**: moderate confidence only (plausible shape, found
  adjacent to the high-confidence `pkt_buf_get_skb`, but not independently
  cross-validated via a stable ROM-callee anchor the way `pkt_buf_get_skb`
  was).
- **Frame injection**: not ported. `injection.c`'s hook
  (`wl_send_hook` at `0x40fe0`) is only defined for `FW_VER_7_45_98`, with
  no `FW_VER_7_45_96_s1` entry - for this build it silently never installs.
  `NEX_CAP_FRAME_INJECTION` is deliberately omitted from `patch.c`'s
  capabilities for this reason. Only `NEX_CAP_MONITOR_MODE` +
  `NEX_CAP_MONITOR_MODE_RADIOTAP` are claimed, matching what was actually
  tested.

## A genuine lesson for anyone porting a version forward from this one

The actual trap-causing bug in an earlier build of this patch was
`HNDRTE_RECLAIM_0_END_PTR` silently carrying over 41.46's raw file offset
unchanged, rather than having been re-derived for 96.s1's real memory
layout. It built cleanly (no compile error - this isn't something the
build catches) and trapped the firmware almost immediately on real
hardware. If porting this further (e.g. to a version newer than 96.s1),
double-check every numeric value actually has a derivation comment next to
it, not just a copied number that happens to compile.
