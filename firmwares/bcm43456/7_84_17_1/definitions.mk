NEXMON_CHIP=CHIP_VER_BCM43456
NEXMON_CHIP_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_CHIP)`
NEXMON_FW_VERSION=FW_VER_7_84_17_1
NEXMON_FW_VERSION_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_FW_VERSION)`

NEXMON_ARCH=armv7-r

RAM_FILE=brcmfmac43456-sdio.bin
RAMSTART=0x198000
RAMSIZE=0xC8000

ROM_FILE=rom.bin
ROMSTART=0x0
ROMSIZE=0xB0000

# bcm43456 is the "43455c5" stepping of the 43455 die (internal string
# "43455c5-roml/..."), firmware 7.84.17.1 - a different stepping AND a different
# firmware lineage from this repo's bcm43455c0 work, so no address below is
# copied from it. Every value here is derived by ./derive.py, which re-derives
# the known-good values for bcm43455c0 7_45_206 / 7_45_234_CY / 7_45_241 /
# 7_45_265 and refuses to emit these unless all four reproduce exactly.
# Re-run it after any change:  python3 derive.py
#
# NOT hardware-verified. The ROM has never been dumped for this stepping - see
# patches/bcm43456/7_84_17_1/PORTING_STATUS.md for what must be confirmed on
# device and in what order.

WLC_UCODE_WRITE_BL_HOOK_ADDR=0x1F3854
HNDRTE_RECLAIM_0_END_PTR=0x19A3F8
HNDRTE_RECLAIM_0_END=0x211020

PATCHSIZE=0x4000
PATCHSTART=$$(($(HNDRTE_RECLAIM_0_END) - $(PATCHSIZE)))

UCODESTART=0x203A98
UCODESIZE=0xD588

# This build has NO templateram region: the ucode runs exactly up to
# HNDRTE_RECLAIM_0_END (0x203A98 + 0xD588 == 0x211020), so there is no separate
# template-RAM blob to relocate. patch.c skips its GenericPatch4 via
# "#if TEMPLATERAMSTART_PTR != 0", and the Makefile's all: target omits
# templateram.bin. Same situation as bcm43455c0 7_45_154/189/206/241.
TEMPLATERAMSTART_PTR=0x0
TEMPLATERAMSTART=0x0
TEMPLATERAMSIZE=0x0

# Stock flash-patch table: 88 entries of 12 bytes (rom_target, size, data_ptr)
# at 0x199800..0x199C20, with data at 0x199000 (stride 8). Note both bases sit
# 0x800 higher than on bcm43455c0, which is why FP_CONFIG_ORIGBASE is derived
# rather than assumed.
FP_DATA_END_PTR=0x1E56B0
FP_CONFIG_BASE_PTR_1=0x1E7AA8
FP_CONFIG_END_PTR_1=0x1E7AA4
FP_CONFIG_BASE_PTR_2=0x1E7D2C
FP_CONFIG_END_PTR_2=0x1E7D28
FP_CONFIG_SIZE=0xC00
FP_CONFIG_BASE=$$(($(PATCHSTART) - $(FP_CONFIG_SIZE)))
FP_DATA_BASE=0x199000
FP_CONFIG_ORIGBASE=0x199800
FP_CONFIG_ORIGEND=0x199C20

VERSION_PTR_1=0x1A0F18
VERSION_PTR_2=0x1E7334
VERSION_PTR_3=0x1E8D20
VERSION_PTR_4=0x1EF7A0
DATE_PTR=0x1A0F24
TIME_PTR=0x1A0F14
