NEXMON_CHIP=CHIP_VER_BCM43430a1
NEXMON_CHIP_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_CHIP)`
NEXMON_FW_VERSION=FW_VER_7_45_96_s1
NEXMON_FW_VERSION_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_FW_VERSION)`

NEXMON_ARCH=armv7-m

RAM_FILE=brcmfmac43430-sdio.bin
RAMSTART=0x0
RAMSIZE=0x80000

ROM_FILE=rom.bin
ROMSTART=0x800000
ROMSIZE=0xA0000

# Not relocated from 41.46/98 - the ucode-write hook body contains no
# relative branch here, disassembly-confirmed single call to the same
# stable ROM callee 0x844914 that both 41.46 and 98 call (chip-constant ROM
# address, unchanged across firmware versions).
WLC_UCODE_WRITE_BL_HOOK_ADDR = 0x56628

# CORRECTED (previous value 0x2684 was a leftover copy of 41.46's own raw
# address, never actually derived for 96.s1 - writing PATCHSTART there would
# corrupt whatever unrelated data lives at that offset in 96.s1's real
# layout, a very plausible cause of an immediate firmware trap).
#
# Properly re-derived via byte-signature: in both 41.46 and 98, the 16-byte
# sequence "fc00000000010000 0000000000000000" occurs EXACTLY ONCE in the
# whole binary, immediately following the 4-byte HNDRTE_RECLAIM_0_END
# literal (confirmed unique - not a generic recurring pattern). Searching
# 96.s1 for this same signature also gives exactly one hit, at 0x284c,
# placing HNDRTE_RECLAIM_0_END_PTR at 0x2848 with stored value 0x6bdec.
#
# Note: 0x6bdec equals our independently-derived TEMPLATERAMSTART (via the
# ucode-magic-anchored formula, unrelated to this search) - i.e. in this
# build, HNDRTE_RECLAIM_0_END == TEMPLATERAMSTART exactly (zero gap),
# unlike 41.46/98 where END = TEMPLATERAMSTART + TEMPLATERAMSIZE. This is
# architecturally sensible (reclaim everything up to where the
# non-reclaimable ucode/templateram blob begins) and is NOT a coincidental
# collision with a separate templateram pointer slot - verified via
# confirming this exact 16-byte signature is unique to the reclaim context
# in both reference firmwares (not found anywhere near their own
# TEMPLATERAMSTART_PTR locations, which are far away: 0x4f3b8 in 41.46,
# 0x55d98 in 98). The previous fork's claim that this same 0x2848 slot was
# TEMPLATERAMSTART_PTR was a mislabeling of this same reclaim-table hit;
# templateram's real patch is disabled in patch.c pending proper
# derivation, rather than guessing a colliding/wrong address.
HNDRTE_RECLAIM_0_END_PTR = 0x2848
HNDRTE_RECLAIM_0_END = 0x6bdec

PATCHSIZE=0x2000
PATCHSTART=$$(($(HNDRTE_RECLAIM_0_END) - $(PATCHSIZE)))

# High confidence - found directly via the ucode magic bytes (data anchor,
# architecture-independent), cross-validated against 7_45_98's own magic
# resolving correctly with the identical method.
UCODESTART = 0x60cd0
UCODESIZE = 0xb11b

# High confidence - TEMPLATERAMSTART_PTR found via a byte-pattern search
# (preceding-u32, then bytes fc000000 00010000) that produced a single
# unambiguous hit at 0x2848, whose preceding u32 (0x6bdec) exactly matches
# TEMPLATERAMSTART as independently derived via round_up_4(UCODESTART +
# UCODESIZE) = round_up_4(0x60cd0 + 0xb11b) = 0x6bdec. Two independent
# derivation methods agreeing is strong evidence.
TEMPLATERAMSTART_PTR = 0x2848
TEMPLATERAMSTART = 0x6bdec
TEMPLATERAMSIZE = 0x640

# NOT independently derived this session - copied from 7_45_98's value as a
# placeholder. Needs real derivation (byte-signature or structural search)
# before trusting the built patch layout.
FP_DATA_END_PTR = 0x40edc

# High confidence - found via 98's documented method: scanned for every
# 4-byte-aligned dword equal to FP_CONFIG_ORIGBASE (0x1800), which produced
# exactly 4 clustered candidates at deltas 0x64/0xec/0x128 from the first
# (0x4be10, 0x4be74, 0x4bf60, 0x4c088) - the SAME spacing pattern 98's own
# comment documents for its own cluster. Three of the four candidates'
# preceding "end" word independently reads back as 0x2214, matching
# FP_CONFIG_ORIGEND as derived below by a different method.
FP_CONFIG_BASE_PTR_1 = 0x4be10
FP_CONFIG_END_PTR_1 = 0x4be0c
FP_CONFIG_BASE_PTR_2 = 0x4c088
FP_CONFIG_END_PTR_2 = 0x4c084
# Placed at the end of our derived HNDRTE_RECLAIM_0_END boundary, to be
# overwritten after it's read at boot - same placement logic 7_45_98 used
# (its own end-of-firmware), not independently verified as safe/unused RAM.
FP_CONFIG_BASE=0x6C42C
FP_DATA_BASE = 0x1000
FP_CONFIG_SIZE = 0xc00
# High confidence - chip constant, unchanged from 41.46/98.
FP_CONFIG_ORIGBASE = 0x1800
# High confidence - computed directly by walking the flash-patch table from
# ORIGBASE until entries stop looking like valid ROM addresses (215 entries
# found, vs 98's 212); independently corroborated by 3 of the 4
# FP_CONFIG_*_PTR candidates above reading back this exact value.
FP_CONFIG_ORIGEND = 0x2214
