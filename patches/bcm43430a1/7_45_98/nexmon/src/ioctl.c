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
#include <debug.h>              // contains macros to access the debug hardware
#include <wrapper.h>            // wrapper definitions for functions that already exist in the firmware
#include <structs.h>            // structures that are used by the code in the firmware
#include <helper.h>             // useful helper functions
#include <patcher.h>            // macros used to craete patches such as BLPatch, BPatch, ...
#include <rates.h>              // rates used to build the ratespec for frame injection
#include <nexioctls.h>          // ioctls added in the nexmon patch
#include <capabilities.h>       // capabilities included in a nexmon patch
#include <sendframe.h>          // sendframe functionality
#include <argprintf.h>

int 
wlc_ioctl_hook(struct wlc_info *wlc, int cmd, char *arg, int len, void *wlc_if)
{
    argprintf_init(arg, len);
    int ret = IOCTL_ERROR;

    switch (cmd) {
        case NEX_GET_CAPABILITIES:
            if (len == 4) {
                memcpy(arg, &capabilities, 4);
                ret = IOCTL_SUCCESS;
            }
            break;

        case NEX_WRITE_TO_CONSOLE:
            if (len > 0) {
                arg[len-1] = 0;
                printf("ioctl: %s\n", arg);
                ret = IOCTL_SUCCESS;
            }
            break;

        case 600: // raw memory read via ROM memcpy: arg[0:4] holds source address, returns len bytes from *addr
            memcpy(arg, *(char **) arg, len);
            ret = IOCTL_SUCCESS;
            break;

        case 601: // sanity: unconditionally write a known constant into arg
            ((unsigned int *) arg)[0] = 0xDEADBEEF;
            ret = IOCTL_SUCCESS;
            break;

        case 602: // sanity: read-modify-write arg itself, no external memory touched
            ((unsigned int *) arg)[0] = ((unsigned int *) arg)[0] + 1;
            ret = IOCTL_SUCCESS;
            break;

        case 603: // raw memory read via manual byte loop, no ROM memcpy dependency
            {
                volatile unsigned char *src = *(unsigned char **) arg;
                volatile unsigned char *dst = (unsigned char *) arg;
                int i;
                for (i = 0; i < len; i++)
                    dst[i] = src[i];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 604: // dump live D11 core registers: maccontrol, maccommand, macintstatus, macintmask (16 bytes)
            if (len >= 16) {
                unsigned int *out = (unsigned int *) arg;
                out[0] = wlc->regs->maccontrol;
                out[1] = wlc->regs->maccommand;
                out[2] = wlc->regs->macintstatus;
                out[3] = wlc->regs->macintmask;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 606: // probe how many packet buffers the firmware can still hand out
            /* Allocates until pkt_buf_get_skb() fails, reports the count, then
             * frees every one again. Used to test whether channel hopping leaks
             * packet buffers: sample this before, during and after hopping and
             * watch whether the available count decays. Bounded at 64 so this
             * can never itself exhaust the pool. */
            if (len >= 4) {
                void *bufs[64];
                int n = 0, got;
                for (n = 0; n < 64; n++) {
                    bufs[n] = pkt_buf_get_skb(wlc->osh, 128);
                    if (bufs[n] == 0)
                        break;
                }
                got = n;
                while (n-- > 0)
                    pkt_buf_free_skb(wlc->osh, bufs[n], 0);
                ((unsigned int *) arg)[0] = got;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 500: // dump wlif list
            {
                struct wlc_if *wlcif = wlc->wlcif_list;

                for (wlcif = wlc->wlcif_list;  wlcif != 0; wlcif = wlcif->next) {
                    char ifname[32];

                    strncpy(ifname, wlcif->wlif == 0 ? wlc->wl->dev->name : wlcif->wlif->dev->name, sizeof(ifname));
                    ifname[sizeof(ifname) - 1] = '\0';

                    argprintf(" \"%s\" 0x%p type=%02x index=%02x flags=%02x\n", ifname, wlcif, wlcif->type, wlcif->index, wlcif->flags);
		}

                ret = IOCTL_SUCCESS;
            }
            break;

        default:
            ret = wlc_ioctl(wlc, cmd, arg, len, wlc_if);
    }

    return ret;
}

__attribute__((at(0x4a8bc, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
GenericPatch4(wlc_ioctl_hook, wlc_ioctl_hook + 1);

