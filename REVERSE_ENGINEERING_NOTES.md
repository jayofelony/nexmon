# Reverse engineering notes (BCM43430/1, Pi Zero 2 W, Debug branch)

Working notes from live debugging of `patches/bcm43430a1/7_45_98` against a
real chip (hostname `Jayofelony`, `10.12.194.1`). Kept here so the next
session (or the next chip) doesn't have to rediscover the method from
scratch. See also git history on the `Debug` branch for the actual fixes.

## The core bug pattern: silently-missing wrapper addresses

`patches/common/wrapper.c` declares vendor ROM/RAM functions via a stack of
`AT(CHIP_VER_x, FW_VER_y, ADDR)` attributes above one declaration, e.g.:

```c
AT(CHIP_VER_BCM43430a1, FW_VER_7_45_41_26_r640327, 0x9F38)
AT(CHIP_VER_BCM43430a1, FW_VER_7_45_41_46, 0xA024)
int
wlc_d11hdrs(void *wlc, void *p, void *scb, ...)
RETURN_DUMMY
```

The nexmon GCC plugin (`buildtools/gcc-nexmon-plugin/nexmon.c`) matches the
build's `chipver`/`fwver` against these lines. **If no line matches the
firmware version being built, this fails silently** — no compiler error, no
linker error. The plugin just compiles the `RETURN_DUMMY`/`VOID_DUMMY` body
(`{ ; return 0; }` / `{ ; }`) as an ordinary local function, and the linker
places it at whatever address function-section layout happens to give it.
The build looks completely clean. At runtime, calling that "wrapped"
function does *not* call the real vendor code — it either no-ops or, if the
linker placed it somewhere that gets overwritten by other RAM content before
it's called (heap, other data), it can jump into garbage and trap the
firmware.

This is exactly what happened porting `7_45_41_46` → `7_45_98` for
`bcm43430a1`: someone (an earlier session) relocated the flash-patch-table
bookkeeping addresses in `definitions.mk` (`WLC_UCODE_WRITE_BL_HOOK_ADDR`,
`HNDRTE_RECLAIM_0_END`, `UCODESTART`, `TEMPLATERAMSTART_PTR`,
`FP_DATA_END_PTR`, `FP_CONFIG_BASE_PTR_1/2`) but missed five functions used
by `sendframe.c` (injection) and `monitormode.c` (RX radiotap wrapping):
`wlc_d11hdrs`, `wlc_get_txh_info`, `wlc_txfifo`, `wlc_bmac_read_tsf`,
`wlc_phy_channel2freq`. Frame injection reliably firmware-trapped
(`TRAP 3`) because `sendframe()` called a fake `wlc_d11hdrs` stub sitting on
top of unrelated memory. Fixed on `Debug` (commit `d12879ab`) by relocating
all five from the known-good `7_45_41_46` addresses via byte-signature
matching (see below). **Confirmed fixed**: injection no longer traps the
firmware after the fix (tested live, two rounds, no `TRAP` in dmesg).

### How to detect this for a new chip/firmware port

After `make` in a `patches/<chip>/<fwver>/nexmon` dir, check
`gen/nexmon.pre` for every wrapped function actually used by the patch
sources (`grep -rn '\bwlc_[a-z_0-9]*(' src/*.c` to enumerate calls, then
check each name appears as a `DUMMY <addr> <name>` line, not just as a
`.text.<name>` section in `log/linker.log` with no matching `DUMMY` line).
If a function is called but never shows up in the `DUMMY` list, it silently
compiled as a local stub — that is the bug pattern above. Cross-check
against `wrapper.c` / `local_wrapper.c`: does an `AT()` line exist for this
exact `CHIP_VER_x, FW_VER_y` pair? If not, it needs relocating.

### Byte-signature relocation method (what worked)

Given a function's known-good address in an older/reference firmware
version of the *same chip*, and the two stock (unpatched)
`brcmfmac*.bin` RAM images on disk (`firmwares/<chip>/<fwver>/`):

```python
data_ref = open('firmwares/<chip>/<known_good_fwver>/brcmfmac*.bin','rb').read()
data_new = open('firmwares/<chip>/<target_fwver>/brcmfmac*.bin','rb').read()
sig = data_ref[known_addr:known_addr+32]   # 32-byte prologue signature
offs = [i for i in range(len(data_new)) if data_new.startswith(sig, i)]
# want exactly one hit; shrink siglen (24/16/12/8) if zero hits, since
# minor recompiles can shift bytes a few instructions in; a single unique
# hit at 32 bytes was enough for all 5 functions fixed this session
```

Always sanity-check the resulting address by disassembling ~40 bytes there
(`arm-none-eabi-objdump -D -b binary -m arm -M force-thumb
--start-address=<addr> --stop-address=<addr+40> <file>`) — it should look
like a real function prologue (`push`/`stmdb` with a sensible register
list, followed by coherent instruction flow), not garbage ending in `udf`
(0xdebf, which is GNU as's Thumb padding/fill value — a strong tell that
you've landed on inter-function padding, not code).

RAM addresses (< `RAMSIZE`, i.e. below `0x80000` for this chip) are
firmware-build-specific and **must** be relocated per firmware version.
ROM addresses (>= `ROMSTART`, `0x800000`+ here) are chip-stepping-specific
and constant across firmware versions for the same physical chip revision
— but see the ROM-mismatch note below, they can still be wrong if the
*wrong reference chip revision* was used originally.

## Live ROM/RAM extraction from the actual chip

Added a raw-memory-read custom ioctl to confirm addresses against what's
*actually on this physical chip*, not just assumptions:

```c
// in patches/<chip>/<fwver>/nexmon/src/ioctl.c, inside wlc_ioctl_hook's switch:
case 603: // manual byte-loop memcpy, no dependency on the (possibly wrong) wrapped ROM memcpy
{
    volatile unsigned char *src = *(unsigned char **) arg;
    volatile unsigned char *dst = (unsigned char *) arg;
    int i;
    for (i = 0; i < len; i++) dst[i] = src[i];
    ret = IOCTL_SUCCESS;
}
break;
```

Build `utilities/nexutil` **natively on the target device** (aarch64 Pi:
`cd utilities/nexutil && make`, no cross-compile needed, no extra deps in
the default `USE_NETLINK` build path). Then:

```
nexutil -I wlan0 -g603 -i -v<hex addr> -l<len, up to 8192> -r > chunk.bin
```

Chunk in 8192-byte reads (matches `BRCMF_DCMD_MAXLEN`) and concatenate to
dump arbitrarily large ranges. Used this to pull this chip's full 640KB ROM
(`0x800000`-`0x8A0000`) into
`firmwares/bcm43430a1/7_45_98/rom.bin` for offline disassembly.

**Do NOT use a plain wrapped `memcpy()` for this** — on this specific chip,
`memcpy`'s assumed ROM address (`0x880B80`, `FW_VER_ALL` in `wrapper.c`) is
provably wrong (disassembles to an unrelated small thunk/trampoline, not a
copy routine; confirmed by testing writes/reads through it that silently
did nothing). `malloc` shares the exact same (wrong) address in
`wrapper.c` — also suspect, not yet independently verified. Always use the
manual byte-loop version above until/unless these are re-verified.

**Safety warning — do not scan low/unknown RAM addresses live.** Reading
ROM (`0x800000`+) this way is safe (full 640KB dump done cleanly, no
issues). Reading unverified RAM/peripheral-mapped low addresses is NOT
safe — one attempt to scan from `0x0` upward crashed the firmware hard
(`brcmf_sdio_bus_reset: giving up on SDIO card reset after 5 attempts, bus
stays down`) and required a **physical power cycle** to recover; `rmmod`/
`modprobe` alone hung in `D` state and could not recover it. If you need to
inspect RAM content, prefer reading the *static* `.bin` file on disk at the
same offset (RAM address == file offset, since `RAMSTART=0x0`) over a live
read, unless you specifically need to confirm runtime-only state (e.g. the
flash-patch config table, which is legitimately overwritten after boot —
see `FP_CONFIG_BASE`'s comment in `definitions.mk`, "will be overwritten
after it is read").

## Current unresolved issue on bcm43430a1/7_45_98: RX is completely silent

Frame injection is now fixed (crash resolved). **RX (monitor-mode capture)
still produces zero packets and zero SDIO interrupt activity**, even with:
- CLM blob, country/regulatory, channel-set all confirmed correct (`iw
  scan` in managed mode finds real APs with full details — RX genuinely
  works outside monitor mode).
- `SET_MONITOR`/`GET_MONITOR` round-trips correctly via the proper
  per-interface path (`brcmf_net_mon_open` in
  `patches/driver/brcmfmac_6.18.y-nexmon/core.c`), confirmed `monitor: 2`.
- `SET_PROMISC` added alongside `SET_MONITOR` in the same function (commit
  `ae2cb6c6`) — no change.
- The flashpatch table itself verified correct via static analysis of the
  built `.bin`: our `wl_monitor_hook` entry (`0x81F620`) is entry #213,
  correctly appended after the 212 stock entries, with both
  `FP_CONFIG_END_PTR` locations correctly extended. Not a build/install
  bug.
- `0x81F620` disassembles to a plausible RX-status-build-then-dispatch
  callsite (3-arg call matching `wl_monitor_hook(wl, sts, p)`'s shape).
- An unconditional `printf()` at the very top of `wl_monitor_hook()`
  (`patches/bcm43430a1/7_45_98/nexmon/src/monitormode.c`) — before any of
  the potentially-still-broken calls below — **never fires**, checked via
  `console_interval` debugfs polling and live beacon traffic on the tuned
  channel.

Not yet fixed/explained. `pkt_buf_get_skb` (used by `wl_monitor_radiotap`,
downstream of the hook) has **no `CHIP_VER_BCM43430a1` entry at all** in
`wrapper.c` (missing for every firmware version, not just `7_45_98`) — but
this can't explain the silence since the hook's `printf` runs before that
call is ever reached.

**Caller trace attempted, inconclusive.** Correctly identified the function
containing `0x81F620` starts at `0x81F414` (confirmed via a properly
instruction-aligned disassembly — a naive `--start-address=0x81f560` cut
mid-instruction the first time and produced misleading garbage; always
disassemble from a confirmed boundary, e.g. right after a `b.w`/`bx lr`, and
slice the region of interest out of one continuous pass, don't restart
objdump at an arbitrary address inside a Thumb function). Searched the
*entire* ROM dump and the stock RAM firmware image, both as disassembled
`bl`/`b.w`/`b.n` targets and as raw little-endian literal bytes (for
function-pointer/vtable-style indirect calls), for any reference to
`0x81F414` — zero hits, anywhere. Search method validated against a
known-good control (`wlc_bmac_read_tsf` at `0x20abc`, found referenced
twice, exactly as expected) — so the method works, the negative result is
real.

**But this doesn't prove `0x81F620` is wrong.** Ran the same zero-literal-
reference check against `wl_monitor` itself (`0x819510`, the ROM function
nexmon's own `wl_monitor_hook` calls for non-radiotap `MONITOR_IEEE80211`
mode) — also zero references anywhere in stock code. That's expected: this
whole class of function is vendor ROM code that stock firmware never calls
(consumer devices don't expose monitor mode) — nexmon's model is
specifically to hijack otherwise-dead ROM code via flashpatch redirection,
so "no static caller found" is the *normal* signature for every nexmon
monitor-hook target, not evidence specific to this address being wrong. A
static caller/xref search cannot distinguish "correct hook, naturally
unreferenced by design" from "wrong address" for this class of patch.

**What would actually be decisive** (not yet done, would need either more
tooling or a second data point):
- A real ROM dump from a *different, known-working* BCM43430/1 device
  running the same nexmon monitor-mode setup, to diff against this chip's
  `rom.bin` — if `0x81F620`'s neighborhood differs between the two, that's
  a smoking gun either way. Nobody had this available this session.
- Proper decompilation (Ghidra + the `armv7-m` processor module) instead of
  raw `objdump` text-grepping, so real cross-reference analysis (including
  through indirect/computed dispatch tables, which this ROM uses heavily —
  see the `blx r7` example near `0x81f3b8`) is possible. `objdump`/manual
  grepping cannot follow computed jump tables or vtable-style dispatch, and
  this ROM has plenty of both; that's the real blind spot in this session's
  method, not the direct-reference search itself.
- Alternatively, single-step/hardware-trace the actual RX interrupt handler
  on the live chip (out of scope for what's available here — no JTAG/SWD
  access set up, only the SDIO-side custom-ioctl memory read/write).

## TODO (explicit ask from the user): apply this to other chips

The user wants the same audit-and-fix treatment for:
- **bcm43436b0** — already has a `rom_extraction` reference implementation
  at `patches/bcm43436b0/9_88_4_65/rom_extraction/` (a full separate
  firmware build with a `wlc_ioctl_hook` at `0x4B274` implementing memory
  read codes `0x600-0x603` — same idea as the injected `case 603` above,
  worth comparing against). Also relevant: the user's original hypothesis
  this session was that their physical chip might actually be a
  COVID-shortage `bcm43436b0` substitute rather than genuine `43430/1` —
  tested and effectively disproven (stock 43436 firmware+CLM caused a hard
  SDIO attach failure on this chip: `clmload ... failed (-52)`,
  `dongle is not responding`). So this Pi's chip is genuine `43430/1`; the
  `43436b0` audit is for the user's *other* hardware, not this device.
- **bcm43455c0** — also has an existing `rom_extraction/` reference impl to
  compare against (`patches/bcm43455c0/7_45_154/rom_extraction/`).
- **Latest firmware versions** for whichever chips are relevant (check
  `firmware-brcm80211` package upstream for current stock firmware/CLM,
  same way this session confirmed `7.45.98` is the current stock version
  for `43430/1` — extract via `dpkg-deb -x` without installing, to avoid
  clobbering the working `firmware-nexmon` package).

Methodology to reuse for each: (1) build normally, (2) grep
`gen/nexmon.pre` for missing `DUMMY` entries against actual calls in the
patch `src/*.c` files, (3) byte-signature relocate any gaps from the
nearest known-good same-chip firmware version, (4) where no same-chip
reference address exists at all (like `pkt_buf_get_skb` here), extract the
live ROM via the `case 603` ioctl technique and either disassemble by hand
or trace via a caller that *is* correctly resolved.
