#!/usr/bin/env python3
"""Derive definitions.mk values for the bcm43456 (43455c5 stepping) firmware.

There is only ONE bcm43456 firmware in existence (7.84.17.1, and Broadcom has
shipped nothing newer since 2020), so a same-chip self-test is impossible.
Instead this re-derives the already-known values for four bcm43455c0 firmwares
- same die family, same definitions.mk shape - and refuses to print 43456's
values unless all four reproduce exactly. A derivation that cannot reproduce
known answers is not evidence.

Generalised from firmwares/bcm43455c0/7_45_265/derive.py. Three things there
were hard-coded to 43455c0 and are derived here instead:
  * FP_CONFIG_ORIGBASE was fixed at 0x199000; on 43456 it is 0x199800.
  * the ucode-hook slot was filtered with 'addr > 0x200000'; on 43456 the slot
    is at 0x1F3864, below that threshold.
  * the version regex assumed the '(<hash> CY)' Cypress form; 43456 uses
    '(r871554)'.
"""
import os, re, struct, sys

RS = 0x198000                                    # RAMSTART, whole 43455 family
UCODE_MAGIC = bytes.fromhex('4e10000360bc0100')  # start of the ucode blob


def derive(path):
    d = open(path, 'rb').read()
    r = {}

    def rd(a):
        return struct.unpack('<I', d[a - RS:a - RS + 4])[0]

    def slots(val):
        """Every 4-aligned offset holding `val`, as an absolute address."""
        b = struct.pack('<I', val)
        out, i = [], d.find(b)
        while i != -1:
            if i % 4 == 0:
                out.append(RS + i)
            i = d.find(b, i + 1)
        return out

    # --- ucode blob, and the BL hook whose literal pool holds UCODESTART at +0x10
    hits = [RS + i for i in range(len(d)) if d.startswith(UCODE_MAGIC, i)]
    assert len(hits) == 1, 'ucode magic must be unique: %s' % [hex(x) for x in hits]
    r['UCODESTART'] = us = hits[0]
    s = slots(us)
    if len(s) > 1:  # 43455c0 images carry a spurious low match; the hook is the high one
        s = [a for a in s if a > 0x200000]
    assert len(s) == 1, 'ucode hook slot must be unique: %s' % [hex(x) for x in s]
    r['WLC_UCODE_WRITE_BL_HOOK_ADDR'] = s[0] - 0x10

    # --- reclaim region end: the <end>,0x1980D4,0x198100,0x198000,0x198000 record
    for a in range(0x199000, 0x19C000, 4):
        if [rd(a + 4 * k) for k in (1, 2, 3, 4)] == [0x1980D4, 0x198100, RS, RS]:
            r['HNDRTE_RECLAIM_0_END_PTR'] = a
            r['HNDRTE_RECLAIM_0_END'] = rd(a)
            break
    else:
        raise AssertionError('reclaim_0_end record not found')

    # --- templateram: a slot just below the ucode pointing between it and reclaim end.
    # Absent on 43456 (its ucode runs right up to reclaim end), which patch.c
    # handles via '#if TEMPLATERAMSTART_PTR != 0'.
    t = rd(us - 0x16C)
    if us < t < r['HNDRTE_RECLAIM_0_END']:
        r['TEMPLATERAMSTART_PTR'] = us - 0x16C
        r['TEMPLATERAMSTART'] = t
        r['TEMPLATERAMSIZE'] = r['HNDRTE_RECLAIM_0_END'] - t
        r['UCODESIZE'] = t - us
    else:
        r['TEMPLATERAMSTART_PTR'] = r['TEMPLATERAMSTART'] = r['TEMPLATERAMSIZE'] = 0
        r['UCODESIZE'] = r['HNDRTE_RECLAIM_0_END'] - us

    # --- flash-patch config table: 12-byte (rom_target, size, data_ptr) records.
    # Locate the base by walking each 0x800-aligned candidate and keeping the
    # one that yields the longest run of valid records.
    def walk(base):
        a, n = base, 0
        while a - RS + 12 <= len(d):
            tg, sz, dp = struct.unpack('<III', d[a - RS:a - RS + 12])
            if not (0 <= tg < 0xB0000 and 0 < sz <= 0x40 and RS <= dp < RS + len(d)):
                break
            a += 12
            n += 1
        return n, a

    best = max((walk(RS + off)[0], RS + off) for off in range(0x800, 0x4000, 0x800))
    assert best[0] > 20, 'no flash-patch table found (best run %d)' % best[0]
    r['FP_CONFIG_ORIGBASE'] = ob = best[1]
    r['FP_CONFIG_ORIGEND'] = walk(ob)[1]
    r['FP_DATA_BASE'] = struct.unpack('<I', d[ob - RS + 8:ob - RS + 12])[0]

    # Six aligned slots hold FP_CONFIG_ORIGBASE; [0]-4 is the data-end pointer,
    # [1] and [5] are the two config-base pointers (each preceded by its end ptr).
    fp = slots(ob)
    assert len(fp) == 6, 'expected 6 FP_CONFIG_ORIGBASE slots, got %s' % [hex(x) for x in fp]
    r['FP_DATA_END_PTR'] = rd(fp[0] - 4)
    r['FP_CONFIG_BASE_PTR_1'], r['FP_CONFIG_END_PTR_1'] = fp[1], fp[1] - 4
    r['FP_CONFIG_BASE_PTR_2'], r['FP_CONFIG_END_PTR_2'] = fp[5], fp[5] - 4
    assert rd(r['FP_CONFIG_END_PTR_1']) == r['FP_CONFIG_ORIGEND'], 'ORIGEND vs END_PTR_1'
    assert rd(r['FP_CONFIG_END_PTR_2']) == r['FP_CONFIG_ORIGEND'], 'ORIGEND vs END_PTR_2'

    # --- version / date / time string slots
    m = re.search(rb'7\.\d+\.[0-9.]+ \([0-9a-fr]+(?: CY)?\)\x00', d)
    r['_VERSION'] = m.group()[:-1].decode()
    for n, v in enumerate(slots(RS + m.start()), 1):
        r['VERSION_PTR_%d' % n] = v
    dm = re.search(rb'[A-Z][a-z]{2} [ 0-9][0-9] 20[0-9]{2}\x00', d)
    r['DATE_PTR'] = slots(RS + dm.start())[0]
    tm = re.search(rb'[0-9]{2}:[0-9]{2}:[0-9]{2}\x00', d[dm.start():])
    r['TIME_PTR'] = slots(RS + dm.start() + tm.start())[0]

    # --- invariant that ties the ucode/templateram/reclaim triple together
    assert us + r['UCODESIZE'] + r['TEMPLATERAMSIZE'] == r['HNDRTE_RECLAIM_0_END']
    return r


# Known-good values for four bcm43455c0 builds, from their committed
# definitions.mk files. All four must reproduce before 43456 is printed.
EXPECT = {
    '../../bcm43455c0/7_45_206/brcmfmac43455-sdio.bin': dict(
        WLC_UCODE_WRITE_BL_HOOK_ADDR=0x211A4C, HNDRTE_RECLAIM_0_END=0x2307F0,
        UCODESTART=0x222ED8, UCODESIZE=0xD918, FP_DATA_END_PTR=0x2036B0,
        FP_CONFIG_BASE_PTR_1=0x20575C, FP_CONFIG_BASE_PTR_2=0x2059E0,
        FP_CONFIG_ORIGBASE=0x199000, FP_CONFIG_ORIGEND=0x199BF4),
    '../../bcm43455c0/7_45_234_4ca95bb_CY/cyfmac43455-sdio-standard.bin': dict(
        WLC_UCODE_WRITE_BL_HOOK_ADDR=0x215E58, HNDRTE_RECLAIM_0_END=0x2350F0,
        UCODESTART=0x2264C8, UCODESIZE=0xE348, TEMPLATERAMSTART=0x234810,
        TEMPLATERAMSIZE=0x8E0, FP_DATA_END_PTR=0x207B10,
        FP_CONFIG_BASE_PTR_1=0x209B84, FP_CONFIG_BASE_PTR_2=0x209E08,
        FP_CONFIG_ORIGBASE=0x199000, FP_CONFIG_ORIGEND=0x199BE8,
        VERSION_PTR_1=0x1A7EF8, DATE_PTR=0x1A7F04, TIME_PTR=0x1A7EF4),
    '../../bcm43455c0/7_45_241/cyfmac43455-sdio-standard.bin': dict(
        WLC_UCODE_WRITE_BL_HOOK_ADDR=0x213E84, HNDRTE_RECLAIM_0_END=0x2338A0,
        UCODESTART=0x2254C0, FP_DATA_END_PTR=0x205AC0,
        FP_CONFIG_BASE_PTR_1=0x207B70, FP_CONFIG_BASE_PTR_2=0x207DF4,
        FP_CONFIG_ORIGBASE=0x199000, FP_CONFIG_ORIGEND=0x199BF4),
    '../../bcm43455c0/7_45_265/cyfmac43455-sdio-standard.bin': dict(
        WLC_UCODE_WRITE_BL_HOOK_ADDR=0x20EF78, HNDRTE_RECLAIM_0_END=0x22CAD4,
        UCODESTART=0x21F24C, UCODESIZE=0xCFA8, TEMPLATERAMSTART=0x22C1F4,
        TEMPLATERAMSIZE=0x8E0, FP_DATA_END_PTR=0x200D10,
        FP_CONFIG_BASE_PTR_1=0x202D84, FP_CONFIG_END_PTR_1=0x202D80,
        FP_CONFIG_BASE_PTR_2=0x203008, FP_CONFIG_END_PTR_2=0x203004,
        FP_CONFIG_ORIGBASE=0x199000, FP_CONFIG_ORIGEND=0x199BF4,
        FP_DATA_BASE=0x198800, VERSION_PTR_1=0x1A2B78, VERSION_PTR_2=0x2025EC,
        VERSION_PTR_3=0x2040B8, VERSION_PTR_4=0x20ACCC,
        DATE_PTR=0x1A2B84, TIME_PTR=0x1A2B74),
}

TARGET = 'brcmfmac43456-sdio.bin'


def main():
    base = os.path.dirname(os.path.abspath(__file__))
    ok = True
    for rel, exp in EXPECT.items():
        got = derive(os.path.join(base, rel))
        bad = {k: (hex(v), hex(got.get(k, -1))) for k, v in exp.items() if got.get(k) != v}
        print(('OK   ' if not bad else 'FAIL '), rel.split('/')[-2], bad or '')
        ok &= not bad
    if not ok:
        sys.exit('self-test failed - do not trust the 43456 output')

    r = derive(os.path.join(base, TARGET))
    print('\n# bcm43456 / %s' % r.pop('_VERSION'))
    for k, v in r.items():
        print('%s=0x%X' % (k, v))


if __name__ == '__main__':
    main()
