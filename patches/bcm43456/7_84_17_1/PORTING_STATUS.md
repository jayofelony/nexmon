# bcm43456 (Pi 400 / CM4) — first-ever nexmon port

**Read this file first.** It is written as a self-contained handoff: it assumes
you have no prior context beyond this repo and `REVERSE_ENGINEERING_NOTES.md`.

Nobody has ported nexmon to this chip — not this repo, not upstream seemoo-lab.
There is no ROM dump for it anywhere that we know of. The goal is a full port
(monitor mode + radiotap + frame injection) built on top of a **complete,
annotated disassembly of the chip's ROM**, the ROM map being a deliverable in
its own right because it makes every future firmware for this chip cheap.

## What the chip actually is

`firmwares/bcm43456/7_84_17_1/brcmfmac43456-sdio.bin`
(sha256 `ddf83f2100885b166be52d21c8966db164fdd4e1d816aca2acc67ee9cc28d726`),
from `RPi-Distro/firmware-nonfree`. Internal string:

```
43455c5-roml/43455_sdio-pno-aoe-pktfilter-pktctx-lpc-pwropt-43455_ftrs-mfp-noclminc-clm_min
Version: 7.84.17.1 (r871554)  CRC: 72494685  Date: 2020-05-14  Ucode Ver: 1043.20424
```

"43456" is the **`43455c5` silicon stepping** — the same die family as this
repo's fully-ported `bcm43455c0`, but a different stepping *and* a different
firmware lineage (7.84.x vs 7.45.x). This is also why the driver logs both as
`BCM4345/6`: they share `BRCM_CC_4345_CHIP_ID` and are told apart only by
chiprev (43456 is chiprev 9, `BRCMF_FW_ENTRY(..., 0x00000200, 43456)`).

Consequences, and this is the whole difficulty of the port:

- The **process** from `bcm43455c0` transfers: `armv7-r`, ROM at `0x0`, RAM at
  `0x198000`, the same `definitions.mk` shape, the same `derive.py` technique.
- **No address transfers.** Different stepping ⇒ different ROM, so even the
  `FW_VER_ALL` ROM wrappers (`memset 0x37E8`, `printf 0x3834`,
  `wlc_ioctl 0x203B8`, …) are wrong for this chip. Different lineage ⇒ different
  RAM layout too.
- **There is nothing to relocate *from*.** Only one 43456 firmware has ever
  shipped (Broadcom released nothing after 2020), so the byte-signature
  relocation that carried `bcm43455c0` from 234_CY→265 has no counterpart here.
  Addresses must come from disassembling the ROM.
- When a structural comparison helps, compare against **`7_45_154`**: its
  feature string (`lpc-pwropt-43455_ftrs`) is the lineage 43456 belongs to, not
  the `234_CY`/`265` one. (See "do not go by version number alone" in
  `REVERSE_ENGINEERING_NOTES.md`.)

---

## Phase 0 — DONE (offline, committed)

`definitions.mk` is fully derived and the derivation is self-validating.

`firmwares/bcm43456/7_84_17_1/derive.py` re-derives the already-known values for
**four** `bcm43455c0` builds (`7_45_206`, `7_45_234_4ca95bb_CY`, `7_45_241`,
`7_45_265`) and refuses to print 43456's values unless all four reproduce
exactly. Run it any time you doubt a value:

```
cd firmwares/bcm43456/7_84_17_1 && python3 derive.py
```

Derived and cross-checked:

| value | result | evidence |
|---|---|---|
| `RAMSTART` | `0x198000` | the *only* 0x1000-aligned base for which both `RAMSTART+ucode_off` and `RAMSTART+templateram_off` appear as aligned dwords |
| `UCODESTART` / `UCODESIZE` | `0x203A98` / `0xD588` | ucode magic `4e10000360bc0100`, exactly one hit (file `0x6BA98`) |
| `WLC_UCODE_WRITE_BL_HOOK_ADDR` | `0x1F3854` | unique slot holding `UCODESTART`, minus `0x10` |
| `HNDRTE_RECLAIM_0_END(_PTR)` | `0x211020` / `0x19A3F8` | the `<end>,0x1980D4,0x198100,0x198000,0x198000` record |
| templateram | **absent** (`_PTR=0`) | `0x203A98 + 0xD588 == 0x211020` exactly — the ucode runs right up to reclaim end. Same as 43455c0 `154/189/206/241`; `patch.c`'s `#if TEMPLATERAMSTART_PTR != 0` handles it |
| `FP_CONFIG_ORIGBASE` / `ORIGEND` | `0x199800` / `0x199C20` | 88 records of 12 bytes `(rom_target, size, data_ptr)`. **Both FP bases sit 0x800 higher than on 43455c0**, which is why `derive.py` derives the base instead of assuming `0x199000` |
| `FP_DATA_BASE` | `0x199000` | first `data_ptr`; all 88 stride exactly 8 |
| `FP_DATA_END_PTR` | `0x1E56B0` | unique dword equal to `FP_DATA_BASE + 88*8` |
| `FP_CONFIG_BASE_PTR_1/2` | `0x1E7AA8` / `0x1E7D2C` | exactly six slots hold `ORIGBASE` (the same count 43455c0's heuristic asserts); `END_PTR = BASE−4`, and both end-pointers verifiably hold `ORIGEND` |
| `VERSION_PTR_1..4` | `0x1A0F18`, `0x1E7334`, `0x1E8D20`, `0x1EF7A0` | slots pointing at the version string |
| `DATE_PTR` / `TIME_PTR` | `0x1A0F24` / `0x1A0F14` | |
| ROM extent | `0x0` … ≥ `0x91000` | flashpatch ROM targets span `0x5DF0`…`0x90A5C`; dump `0x0`–`0xB0000` and trim |

Validated end-to-end: `make` in the firmware dir produces `ucode.bin` of exactly
54664 B (= `0xD588`) and a `flashpatches.c` with exactly **88** entries, first
target `0x000096F4`, last `0x00070668` — i.e. `fpext` independently agrees with
the derivation.

Also done: `CHIP_VER_BCM43456 = 110` and `FW_VER_7_84_17_1 = 120` registered in
`patches/include/firmware_version.h` (both confirmed to resolve via
`buildtools/scripts/getdefine.sh`); `firmwares/bcm43456/{Makefile,structs.common.h}`
and `7_84_17_1/{Makefile,structs.h}` copied from the 43455c0 shapes.

**Still unverified** (needs the device): `RAMSIZE=0xC8000` and `ROMSIZE=0xB0000`
are carried over from 43455c0 and are plausible but unconfirmed — the driver
logs the real ramsize at attach. And `FP_CONFIG_BASE_PTR_1/2` are structurally
sound but the analogous value was the one thing the `bcm43436b0` port had to
confirm live, so confirm it here too.

---

## Phase 1 — dump the ROM **from the driver**, not from a firmware patch

This is the most important decision in the plan and it departs from how nexmon
normally bootstraps a chip. Do not skip to the conventional route.

**The circularity:** nexmon's `rom_extraction` needs a
`GenericPatch4(wlc_ioctl_hook, …)` at the RAM slot holding `&wlc_ioctl|1` — but
`wlc_ioctl` lives in ROM, so you need a ROM address to dump the ROM. Every
previously-ported chip escaped this by copying the address from an earlier
firmware of the same chip. **Here there is nothing to copy from.**

**The escape:** `brcmf_sdiod_ramrw()` —
`patches/driver/brcmfmac_6.18.y-nexmon/bcmsdh.c:671`, declared in `sdio.h:340` —
reads *arbitrary backplane addresses* over SDIO, ROM at `0x0` included, and it
works with **stock, unmodified firmware**. `sdio.c` already calls it in a dozen
places (console reads, trap dumps, memdump).

Add a debugfs entry beside `brcmf_sdio_debugfs_create()` (`sdio.c:3229`) that
exposes `read(addr, len)` via `brcmf_sdiod_ramrw(..., /*write=*/false, ...)`,
load the driver against **stock firmware**, and dump `0x0`–`0xB0000`.

Why this is strictly better here:

- **Zero brick risk** — no patched firmware is ever flashed. In the
  `bcm43436b0` session every bad image hard-wedged the SDIO backplane
  (`failed backplane access over SDIO … err=-84`) and cost a physical power
  cycle; `rmmod`/`modprobe` does not clear that state.
- **No addresses required** — breaks the circularity outright.
- **It also dumps RAM**, giving a live image to diff against the on-disk `.bin`,
  which is how flashpatch application gets verified later.

Deliverables: `firmwares/bcm43456/7_84_17_1/rom.bin` (gitignored via
`firmwares/.gitignore` — keep it out of git but **back it up**, it is the
expensive artifact) plus a live RAM dump. Sanity-check the dump: valid Thumb-2
at `0x0`, readable strings (`wlc_`, `%s: …`), and all 88 flashpatch ROM targets
landing on plausible instruction boundaries.

**If `brcmf_sdiod_ramrw` refuses ROM reads** (the backplane window may be
restricted to the RAM aperture): read `0x198000` first to prove the path works,
then walk downward to find the limit. Only if ROM is genuinely unreachable, fall
back to the conventional `rom_extraction`, deriving the `wlc_ioctl` slot by
scanning the RAM image for odd-valued dwords in ROM range and disassembling each
candidate's referent — slower, and it reintroduces flash risk.

---

## Phase 2 — complete annotated ROM map

1. **Linear sweep + recursive descent.**
   `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb` over the whole ROM
   for a baseline, then a recursive walk from every entry point (vector table at
   `0x0`, all 88 flashpatch ROM targets, every `bl`/`b.w` target) to separate
   code from data. Cortex-R4 / Thumb-2 means instruction-boundary ambiguity is
   real — where the two disagree, trust the recursive walk.
   `buildtools/fwmap/build.py` already builds a call graph (`calls.txt`,
   `calls_by_caller.txt`, `indirect.txt`) from objdump output, but **its RAM/ROM
   base and size are hard-coded for bcm43430a1 near the top and must be edited**.
   See `buildtools/fwmap/README.md` for its limits (no symbols, ~1800 indirect
   call sites invisible, data decoding as instructions). `up.py <addr> <depth>`
   walks callers upward. `relocate.py`'s `value`/`ptr` modes are useful; its
   `sig` mode is not (nothing to relocate from).
2. **Function boundaries.** Prologues (`push {…,lr}`, `stmdb sp!,{…}`) paired
   with epilogues (`pop {…,pc}`, `ldmia.w sp!,{…,pc}`, `bx lr`), plus every `bl`
   target as a forced boundary.
3. **Cross-references.** Per function: callers, callees, literal-pool constants,
   referenced strings.
4. **Symbol naming — do this first, it is the highest-yield step.** Broadcom
   ROMs are full of `wl%d: %s: …` format strings and literal function-name
   strings (`"wlc_keymgmt_set_bss_tx_key_id"` and similar were confirmed present
   in the sibling bcm43436b0 ROM). Any `bl` into the `printf` family with a
   function-name literal in an argument register names its enclosing function
   for free, and that alone will name a large fraction of the ROM.
5. **Diff against `bcm43455c0`.** Same die family, so many ROM functions are
   structurally identical modulo relocation. Where 43455c0 already names a
   function in `patches/common/wrapper.c`, structural matching carries the
   *name* across — never the address.

Commit: `firmwares/bcm43456/7_84_17_1/rom_map.md` (annotated map),
`symbols.txt` (machine-readable `addr name` pairs), and the scripts that
produced them so the map is reproducible.

---

## Phase 3 — hook inventory

With a named ROM, hooks stop being guesswork. Target the **33-wrapper minimal
set** that `bcm43455c0/7_45_265`'s audit actually resolved. Every one needs a
fresh `AT(CHIP_VER_BCM43456, FW_VER_7_84_17_1, …)` row in
`patches/common/wrapper.c`:

`bcm_binit`, `bcm_bprintf`, `free`, `hndrte_free_timer`, `hndrte_init_timer`,
`hndrte_schedule_work`, `hndrte_time_ms`, `lb_alloc`, `memcpy`, `memset`,
`pkt_buf_get_skb`, `pkt_buf_free_skb`, `printf`, `si_corereg`, `sprintf`,
`strlen`, `strncmp`, `udelay`, `vsnprintf`, `wlc_bmac_write_template_ram`,
`wlc_enable_mac`, `wlc_ioctl`, `wlc_iovar_op`, `wlc_pcb_fn_register`,
`wlc_phy_chan2freq_acphy`, `wlc_queue_80211_frag`, `wlc_recv`, `wlc_scan_ioctl`,
`wlc_sendctl`, `wlc_suspend_mac_and_wait`, `wl_monitor`, `wl_send`, `wl_sendup`.

Plus the patch *sites* the 265 port hard-codes — find each 43456 equivalent:

| 43455c0/265 | what it is |
|---|---|
| `0x20B988` | RAM slot holding `&wlc_ioctl\|1` → `GenericPatch4` |
| `0x200E20` | RAM slot holding `&wl_send\|1` → injection hook |
| `0x1A7604` | `bl wl_monitor` call site |
| `0x25AF2` | ROM flashpatch target for `monitormode.c` |
| `0x1B3E6A` → `0x1B3E86` | `wlc_monitor_amsdu_patch` and its skip target |
| `0x202AAC`, `0x202AC8` | console-size `mov.w rX,#1024` → `#2048` |

Also resolve, with evidence: the monitor-mode RX entry and whether the stock
flashpatch table already diverts it into RAM; and which of the six
`FP_CONFIG_ORIGBASE` slots the ROM actually loads.

**Technique when byte signatures fail** (relative-branch bodies never match):
search the target for `bl <chip-constant ROM address>` instead. That is how
`wlc_monitor_amsdu_patch` was finally pinned on 265, and it is exactly the
technique that works when there is nothing to relocate from.

### Two rules that each cost hours in the bcm43436b0 session

- **Never declare a function in a patch's `local_wrapper.c` if
  `patches/common/wrapper.c` already declares it.** The duplicate weak
  definition makes the nexmon plugin emit a local `RETURN_DUMMY` stub into the
  reclaimed ucode region; the ioctl hook's `default:` path then branches into
  freed RAM and hangs attach on the very first ioctl — **with no build error**.
  `patches/bcm43455c0/7_45_154/rom_extraction/src/local_wrapper.c` is the
  correct pattern: it declares only `fp_apply_patches`.
- **Run the wrapper-gap audit after every build** and treat any hit as blocking:
  ```
  grep -iE "DUMMY" gen/nexmon.pre | awk '{print $NF}' | sort -u > /tmp/resolved
  arm-none-eabi-nm gen/patch.elf | awk '$2=="W"{print $3,$1}' | sort |
    while read -r sym addr; do
      grep -qx "$sym" /tmp/resolved || echo "STUB: $sym -> 0x$addr"
    done
  ```
  Then confirm the hook tail-branches to the **real ROM `wlc_ioctl`**, not a low
  RAM address:
  `arm-none-eabi-objdump -d gen/patch.elf --disassemble=wlc_ioctl_hook`

---

## Phase 4 — `rom_extraction`, as validation

Template: `patches/bcm43455c0/7_45_154/rom_extraction/` — the only usable one in
the family (`bcm43455/7_45_77_0_hw`'s is a stub).

Everything hard-coded to 43455c0/154 that must be retargeted:

| where | 43455c0/154 | 43456 |
|---|---|---|
| `src/ioctl.c` `GenericPatch4` slot | `0x208F20` | Phase 3 |
| `src/local_wrapper.c` `fp_apply_patches` | `0x2005C4` | Phase 3 |
| `src/before_flash_patching.c` `BLPatch` site in `c_main` | `0x19CA50` | Phase 3 |
| `src/before_flash_patching.c` `fp_orig_data[N][2]` / `fp_orig_data_len` | `247` | **`88`** |
| `src/before_flash_patching.c` config-table base | `0x199000` | **`0x199800`** |
| `src/ioctl.c` case `0x605` console-config ptr | `0x208E38` | Phase 3, or drop the case |
| `Makefile` `dump-rom` chunk count | 705 × 1 KiB | size to `ROMSIZE` |

Its `struct fp_config` is `{target_addr, size, data_ptr}` — 12 bytes, exactly
the layout Phase 0 confirmed (88 × 12 = `0x420`, `0x199800`→`0x199C20`).
Consider raising `dump-rom`'s chunk size from `-l1024` to `-l8192`
(`BRCMF_DCMD_MAXLEN`), per `REVERSE_ENGINEERING_NOTES.md`.

**The payoff:** `rom_extraction`'s dump must match Phase 1's driver-side dump
byte-for-byte. That one comparison validates `definitions.mk`, the
ucode/reclaim patches, the flashpatch machinery and the `wlc_ioctl` hook all at
once — so the first flash of patched firmware is a test with a known-correct
answer rather than a leap of faith. (`0x602` reverts flashpatches while dumping,
`0x603` does not; the driver-side dump sees the *patched* ROM, so compare
`0x603` against it and use `0x602` for the clean ROM that Phase 2 disassembles.)

---

## Phase 5 — full port (monitor mode + injection)

Copy `patches/bcm43455c0/7_45_154/nexmon/` (matching lineage) to
`patches/bcm43456/7_84_17_1/nexmon/`, retarget every `at()` and wrapper address
from Phase 3, then bring up in this order, each step gated on the previous:

1. **Boot.** `make install-firmware`; dmesg must show the version string with
   the `nexmon.org` suffix. That suffix appearing is itself proof that
   `VERSION_PTR_1..4`/`DATE_PTR`/`TIME_PTR` and the patch-region placement are
   all correct.
2. **ioctl hook** — a debug case returning a known constant.
3. **Monitor mode + radiotap.** The vif order below is *mandatory* on this
   driver: `rfkill unblock all` → `wlan0` up → read the phy name from
   `/sys/class/net/wlan0/phy80211/name` (**it increments on every reload — never
   hardcode `phy0`**) → `iw phy $PHY interface add mon0 type monitor` → `mon0`
   up → `nexutil -Iwlan0 -m2`. Verify beacons decode with correct
   SSID/BSSID/channel and plausible RSSI against an independent adapter's scan.
   **Keep `wlan0` admin-up** — nexutil's netlink path needs `ndev_global`, which
   is only valid while the primary interface is up, otherwise you get
   `ndev_global is NULL, bus not ready`.
4. **Injection** — verify against a **second capture radio**, and align both
   radios' channels *before* suspecting the port. A fresh monitor vif defaults
   to a different channel after every reload; that mundane detail masqueraded as
   "injection is broken" for a while in the bcm43455c0 session.
5. **A-MSDU** — port `wlc_monitor_amsdu_patch`. On `bcm43455c0/7_45_265`,
   leaving it disabled produced a firmware crash that looked exactly like a
   channel-hopping bug but was an A-MSDU frame smashing memory via `memcpy` with
   a corrupt `p->len`. Do not treat it as low-risk.

Note on regulatory: 43456 is registered with `BRCMF_FW_DEF` (**no CLM**) while
43455 uses `BRCMF_FW_CLM_DEF`, yet the shipped firmware is a `noclminc`/`clm_min`
build that *does* ship a `.clm_blob`. Check early whether
`brcmf_c_process_clm_blob` actually runs; if it doesn't, the channel/regulatory
table comes entirely from the `.txt` NVRAM, which decides which channels monitor
mode can reach.

---

## Phase 6 — stress

Reuse `patches/bcm43436b0/9_88_4_77/test/{reload,hoptest,rxsoak}.sh` (retarget
interface names). Both are required — they catch different failure classes:

- **Channel-hop loop**, liveness-checked every hop.
- **Fixed-channel high-volume soak** on a busy channel — this is what catches
  content-triggered bugs (A-MSDU) that merely *look* like hop bugs.

Watch `macintstatus` bit 8 (`MI_RXOV` = `0x100`) via a debug ioctl. If it
latches, that is the RX-FIFO-overflow cascade documented at length in
`REVERSE_ENGINEERING_NOTES.md`: an RX packet buffer leaks, the pool drains in
~40 frames, and the core spins forever with every ioctl returning `-110`.
`rmmod`/`modprobe` does **not** clear it — only a power cycle does.

Finally strip all debug instrumentation, rebuild clean, and re-verify boot,
monitor and injection on that stripped build before calling the port done.

---

## Verification checklist

- [ ] `derive.py` self-tests pass (all four 43455c0 builds) before any value is trusted
- [ ] driver-side ROM dump and `rom_extraction`'s dump are byte-identical
- [ ] wrapper-gap audit reports zero stubs; `wlc_ioctl_hook` tail-branches to real ROM
- [ ] every ROM byte accounted for as code or data in `rom_map.md`
- [ ] boot shows the `nexmon.org` version suffix
- [ ] `tcpdump` on the monitor vif decodes frames matching an independent capture
- [ ] injection lands on a second radio
- [ ] hop + soak both clean; debug-free rebuild re-passes everything

## Hard-won cautions

- **Never read unverified low RAM or peripheral addresses live.** Doing so
  hard-wedged a chip in an earlier session and needed a physical power cycle.
  Confine live reads to ROM and to RAM offsets backed by the on-disk image.
- **The ARM debug hardware is a dead end on this chip family** — don't spend
  time on it. See `REVERSE_ENGINEERING_NOTES.md` §"DEAD END: the ARM debug
  hardware is unusable on this chip"; both PC-sampler routes brick the chip.
- **`make clean` after changing any `$(FW_PATH)` input**, or the image is
  double-patched.
- **`nexutil -l` defaults to 4 bytes** and silently truncates — always pass `-l`
  explicitly and generously (`-l64`, `-l512`).
- Keep a stock firmware backup at
  `/lib/firmware/brcm/brcmfmac43456-sdio.bin.stock` and a `restore-stock.sh`
  from the very first flash onward. Note the driver loads this chip through a
  symlink chain (`brcmfmac43456-sdio.raspberrypi,400.bin` → the base file), so
  confirm which file is actually being read before concluding a flash "did
  nothing".
- Being genuinely first means there is no oracle. **The ROM map is the oracle** —
  which is why Phases 1–2 come before any patching.
