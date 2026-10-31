/***************************************************************************
 *                                                                         *
 *          ###########   ###########   ##########    ##########           *
 *         ############  ############  ############  ############          *
 *         ##            ##            ##   ##   ##  ##        ##          *
 *         ##            ##            ##   ##   ##  ##        ##          *
 *         ###########   ####  ######  ##   ##   ##  ##    ######          *
 *          ###########  ####  #       ##   ##   ##  ##    #    #          *
 *                   ##  ##    ######  ##   ##   ##  ##    #    #          *
 *                   ##  ##    #       ##   ##   ##  ##    #    #          *
 *         ############  ##### ######  ##   ##   ##  ##### ######          *
 *         ###########    ###########  ##   ##   ##   ##########           *
 *                                                                         *
 *            S E C U R E   M O B I L E   N E T W O R K I N G              *
 *                                                                         *
 * This file is part of NexMon.                                            *
 *                                                                         *
 * Copyright (c) 2016 NexMon Team                                          *
 *                                                                         *
 * NexMon is free software: you can redistribute it and/or modify          *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation, either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * NexMon is distributed in the hope that it will be useful,               *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with NexMon. If not, see <http://www.gnu.org/licenses/>.          *
 *                                                                         *
 **************************************************************************/

#pragma NEXMON targetregion "patch"

#include <firmware_version.h>
#include <wrapper.h>	// wrapper definitions for functions that already exist in the firmware
#include <structs.h>	// structures that are used by the code in the firmware
#include <patcher.h>
#include <helper.h>
#include <capabilities.h>      // capabilities included in a nexmon patch

// NEX_CAP_FRAME_INJECTION deliberately omitted: injection.c's hook
// (wl_send_hook at 0x40fe0) is only defined for FW_VER_7_45_98, with no
// FW_VER_7_45_96_s1 entry - for this build it silently never installs, so
// claiming injection support here would be inaccurate. Only monitor mode
// (RX) has been derived and hardware-verified for this firmware version.
int capabilities = NEX_CAP_MONITOR_MODE | NEX_CAP_MONITOR_MODE_RADIOTAP;

// Hook the call to wlc_ucode_write in wlc_ucode_download
__attribute__((at(WLC_UCODE_WRITE_BL_HOOK_ADDR, "", CHIP_VER_BCM43430a1, FW_VER_7_45_96_s1)))
BLPatch(wlc_ucode_write_compressed, wlc_ucode_write_compressed);

__attribute__((at(HNDRTE_RECLAIM_0_END_PTR, "", CHIP_VER_BCM43430a1, FW_VER_7_45_96_s1)))
GenericPatch4(hndrte_reclaim_0_end, PATCHSTART);

// DISABLED for 7_45_96_s1: the address previously claimed for
// TEMPLATERAMSTART_PTR (0x2848) was found to actually be
// HNDRTE_RECLAIM_0_END_PTR's slot (confirmed via a byte-signature unique to
// that context in both the 41.46 and 98 reference firmwares) - applying
// both patches there would collide/corrupt. The real TEMPLATERAMSTART_PTR
// location for this firmware version was not found (the code immediately
// surrounding the literal in both references does not byte-match anywhere
// in 96.s1, even at 4 bytes, suggesting the surrounding function was
// compiled meaningfully differently due to 96.s1's different build-flag
// set). Leaving this patch disabled rather than guessing a wrong address.
// extern unsigned char templateram_bin[];
// __attribute__((at(TEMPLATERAMSTART_PTR, "", CHIP_VER_BCM43430a1, FW_VER_7_45_96_s1)))
// GenericPatch4(templateram_bin, templateram_bin);
