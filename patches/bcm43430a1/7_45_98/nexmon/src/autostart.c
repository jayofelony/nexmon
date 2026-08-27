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

#include <firmware_version.h>   // definition of firmware version macros
#include <wrapper.h>            // wrapper definitions for functions that already exist in the firmware
#include <structs.h>            // structures that are used by the code in the firmware
#include <helper.h>             // useful helper functions
#include <patcher.h>            // macros used to craete patches such as BLPatch, BPatch, ...

void
autostart(void)
{
	printf("autostart\n");
}

__attribute__((at(0x2c40, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
HookPatch4(hndrte_idle, autostart, "push {r4, lr}\nmov r4, r0");

/* Main-loop iteration counter.
 *
 * hndrte_idle at 0x2c40 is not called per iteration - it IS the main loop, and
 * is entered exactly once:
 *
 *   2c58:  mov r0, r4        <- loop top
 *   2c5a:  bl  0x807de4      <- per-iteration dispatch
 *   2c5e:  b.n 0x2c58        <- unconditional, never exits
 *
 * so counting entries to hndrte_idle would only ever report 1. Hooking the
 * dispatch call instead counts how many times the loop goes round.
 *
 * This is the measurement that splits the remaining search. The core never
 * idles during the wedge (CYCCNT pinned at 81.6M), and the heartbeat - an
 * hndrte timer dispatched from inside 0x807de4 - keeps firing on time, so the
 * loop is demonstrably still turning. What the rate says:
 *
 *   iterations/sec stays normal  -> the loop cycles and finds work every pass;
 *                                   the spin is inside something 0x807de4
 *                                   calls, and the wedge is livelock
 *   iterations/sec collapses     -> the loop is stuck inside a single dispatch
 *                                   call and the heartbeat is being serviced
 *                                   from somewhere else
 *
 * Needed the ARM debug hardware to be unavailable to justify doing it this way;
 * see REVERSE_ENGINEERING_NOTES.md - both PC-sampler routes brick this chip.
 */
unsigned int nex_loop_count = 0;

void
nex_hndrte_run_hook(void *arg)
{
    void (*orig)(void *) = (void (*)(void *)) (0x807de4 | 1);

    nex_loop_count++;
    orig(arg);
}

__attribute__((at(0x2c5a, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
BLPatch(nex_hndrte_run_hook, nex_hndrte_run_hook);
