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

/* Frames seen by the monitor hook; defined in monitormode.c. */
extern unsigned int nex_rx_frames;

/* Heartbeat state. All initialised so they land in .data, not .bss. */
unsigned int nex_hb_seq = 0;
unsigned int nex_hb_armed = 0;

/* Stashed when the heartbeat is armed, so the timer callback can reach the
 * D11 registers without being handed a wlc pointer it has no way to get. */
struct wlc_info *nex_hb_wlc = 0;

/* Cortex-M3 DWT cycle counter. Confirmed present and unused on this chip:
 * DWT_CTRL reads 0x40000000, i.e. NUMCOMP=4 with CYCCNTENA clear.
 *
 * CYCCNT counts processor clock cycles and does not advance while the core is
 * in sleep, so the per-second delta separates the two ways the wl thread can
 * fail to come back: a delta near the core clock means nothing ever idles and
 * something is spinning, a delta well below it means the core sleeps between
 * interrupts and the thread is blocked waiting on something that never posts.
 *
 * TRCENA in DEMCR has to be set before the DWT block responds at all.
 */
/* macintstatus bit 8. Named MI_NSPECGEN_1 in Broadcom's d11.h and aliased
 * there to MI_RXOV, "rxfifo overflow interrupt" - set by the PSM. */
#define MI_RXOV         (1u << 8)

/* NEVER touch the D11 registers through struct d11regs.
 *
 * That struct is declared __attribute__((packed)) in structs.common.h, so GCC
 * cannot assume its fields are aligned and decomposes every access into bytes.
 * A plain "regs->macintstatus = MI_RXOV" compiles to four strb.w's:
 *
 *   ldrb.w r6,[r0,#296] / strb.w r6,[r0,#296]   <- byte 0
 *   strb.w ip,[r0,#297]                         <- byte 1
 *   strb.w r6,[r0,#298] / strb.w r6,[r0,#299]   <- bytes 2,3
 *
 * Four byte transactions against a memory-mapped, write-1-to-clear hardware
 * register, where the vendor's own ISR uses a single 32-bit str. Reads come
 * back coherent in practice, but writes are not safe and must not be done this
 * way.
 *
 * These force a single aligned word access instead. Offsets are from
 * struct d11regs: maccontrol 0x120, maccommand 0x124, macintstatus 0x128,
 * macintmask 0x12c.
 */
#define D11REG(regs, off) \
    (*(volatile unsigned int *) ((unsigned int) (regs) + (off)))
#define D11_MACCONTROL    0x120
#define D11_MACCOMMAND    0x124
#define D11_MACINTSTATUS  0x128
#define D11_MACINTMASK    0x12c

#define DEMCR           (*(volatile unsigned int *) 0xE000EDFC)
#define DWT_CTRL        (*(volatile unsigned int *) 0xE0001000)
#define DWT_CYCCNT      (*(volatile unsigned int *) 0xE0001004)
#define DEMCR_TRCENA    (1u << 24)
#define DWT_CYCCNTENA   (1u << 0)

unsigned int nex_hb_lastcyc = 0;

/* When nonzero, the heartbeat clears MI_RXOV out of macintstatus every time it
 * sees it set, and counts how often it had to.
 *
 * MI_RXOV (bit 8, "rxfifo overflow", per Broadcom's d11.h) is never in this
 * firmware's interrupt mask - measured im=m5c=0xbae7a864 with bit 8 clear -
 * and the ISR at ROM 0x8563ec only ever acknowledges bits that ARE in the
 * mask, so once the PSM sets it the bit latches forever. The question this
 * answers is whether that latched bit is what holds the receive path down, or
 * merely a marker left behind by an overflow that has already done the damage.
 *
 * macintstatus is write-1-to-clear: the ISR acknowledges by writing the bits
 * back (str r6,[r7,#0x128]), so writing 0x100 clears just this bit and leaves
 * every other pending bit untouched. Nothing is unmasked, so no interrupt that
 * has no handler can start firing.
 */
unsigned int nex_rxov_clear = 0;
unsigned int nex_rxov_seen = 0;
unsigned int nex_rxov_fill = 0;

/* Periodic heartbeat, printed to the firmware console.
 *
 * The point of printing rather than answering an ioctl: once the chip wedges,
 * the whole dongle-side command path is dead, so nothing can be read out by
 * ioctl - that is precisely the state we want to observe. The console ring is
 * different. The host reads it over SDIO as a plain memory read, without the
 * firmware servicing anything, so a heartbeat that keeps appearing in dmesg
 * after the ioctls stop answering proves the CPU is still executing and only
 * the wl thread is stuck. A heartbeat that stops at the same moment means the
 * core itself has stalled, and then no firmware-side instrument will ever
 * report anything - only host-side SDIO reads can.
 *
 * rx= distinguishes the third case: CPU alive but reception stopped.
 *
 * The D11 registers are the payload that matters. Both RX processing and
 * ioctls live in the wl thread and stop together at the wedge, so the question
 * is why that thread never runs again. macintstatus/macintmask separate the
 * two candidate answers: if the MAC is still posting interrupts and they are
 * still unmasked, the thread is spinning or blocked on something else; if
 * macintmask has been cleared, or macintstatus is stuck, the thread is waiting
 * for an interrupt that can no longer arrive.
 *
 * Everything here is a plain register or memory read, so the heartbeat cannot
 * itself perturb what it is measuring - which matters, given case 612 did
 * exactly that.
 */
static void
nex_heartbeat(struct hndrte_timer *t)
{
    unsigned int mis = 0;
    unsigned int cyc, dcyc;

    /* Unsigned subtraction, so a 32-bit wrap of CYCCNT still yields the right
     * delta - at ~80MHz it wraps roughly every 53 seconds. */
    cyc = DWT_CYCCNT;
    dcyc = cyc - nex_hb_lastcyc;
    nex_hb_lastcyc = cyc;

    if (nex_hb_wlc) {
        /* The ISR at ROM 0x8563ec decides what to acknowledge like this:
         *
         *   r8 = macintstatus                    ldr r8,[r7,#0x128]
         *   r6 = hw[0x5c] | hw[0x64]             the software mask
         *   r6 &= r8                             ands r6,r6,r8
         *   if (r6 == 0) return;                 beq  - ACKS NOTHING
         *   macintstatus = r6                    str r6,[r7,#0x128]
         *
         * So it only ever clears bits that are in the software mask. A status
         * bit outside that mask can never be acknowledged and stays latched
         * forever - which is exactly the behaviour of is=0x100.
         *
         * Printing the three mask words tests that directly: if bit 8 is set
         * in macintstatus and clear in (m5c | m64) during the wedge, the ISR
         * provably cannot clear it.
         *
         * (The suspend slots hw+0xf0/+0xf4/+0x144 were checked here first and
         * read 0 throughout, including mid-wedge, with the hw pointer verified
         * via hw+0x88 == 0x18001000 - so the MAC-suspend busy-wait is not
         * where the CPU goes. Dropped from the line rather than kept.)
         */
        volatile unsigned int *hw = (volatile unsigned int *) nex_hb_wlc->hw;

        /* The mask words are no longer reported: measured across a full
         * healthy-to-wedged run they never moved (m5c=bae7a864, m60=bae7a864,
         * m64=0, macintmask=bae7a864), so they are established rather than
         * observed. The line's budget is better spent on the recovery counters. */
        mis = D11REG(nex_hb_wlc->regs, D11_MACINTSTATUS);

        if (mis & MI_RXOV) {
            nex_rxov_seen++;
            if (nex_rxov_clear) {
                /* Clearing the status bit alone was measured to do nothing:
                 * 52 clears, macintstatus still 0x100 on every beat, wedge on
                 * schedule. The bit is a symptom - the PSM re-raises it while
                 * the receive FIFO is still overflowed - so recovery has to
                 * act on the FIFO.
                 *
                 * dma_rxfill() reposts receive descriptors, which is what an
                 * overflow leaves short. hw->di[] is the per-FIFO dma_info
                 * array at wlc_hw_info+0x14 and RX is di[0].
                 *
                 * Both dma_rx and dma_rxfill had no FW_VER_7_45_98 address
                 * until now - they would have compiled to silent no-op stubs.
                 * Relocated from 7.45.41.46 by byte signature (unique to 96B,
                 * prologues identical) and added to wrapper.c.
                 */
                if (hw) {
                    void *rxdi = (void *) hw[0x14 / 4];
                    if (rxdi) {
                        dma_rxfill(rxdi);
                        nex_rxov_fill++;
                    }
                }
                D11REG(nex_hb_wlc->regs, D11_MACINTSTATUS) = MI_RXOV;
            }
        }
    }

    printf("NEXHB %u rx=%u dcyc=%u is=%x ov=%u fill=%u clr=%u\n",
           ++nex_hb_seq, nex_rx_frames, dcyc, mis, nex_rxov_seen,
           nex_rxov_fill, nex_rxov_clear);
}

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

        case 607: // size-dependent buffer probe: how many buffers of each size are left
            /* Reports four counts - for 128, 512, 1024 and 1800 byte requests -
             * each measured by allocating until failure and then freeing every
             * one again. A fixed lbuf pool gives the same count for all four
             * sizes; a heap-backed allocator gives counts that fall as the size
             * rises, and a leak of large buffers then shows up as a decay in the
             * 1800-byte count long before the 128-byte count moves at all. That
             * distinction matters here: channel hopping leaves the 128-byte
             * count pinned at its healthy value for several hops and then drops
             * it straight to zero, which is what a large-buffer leak against a
             * shared heap looks like from a small-buffer probe. */
            if (len >= 16) {
                static const int sizes[4] = { 128, 512, 1024, 1800 };
                void *bufs[48];
                int s, n;
                for (s = 0; s < 4; s++) {
                    for (n = 0; n < 48; n++) {
                        bufs[n] = pkt_buf_get_skb(wlc->osh, sizes[s]);
                        if (bufs[n] == 0)
                            break;
                    }
                    ((unsigned int *) arg)[s] = n;
                    while (n-- > 0)
                        pkt_buf_free_skb(wlc->osh, bufs[n], 0);
                }
                ret = IOCTL_SUCCESS;
            }
            break;

        case 608: // read the firmware's own outstanding-packet counter
            /* The ROM allocator at 0x808744 increments osh[0] on every
             * successful packet allocation and pkt_buf_free_skb decrements it
             * once per non-cloned lbuf it releases, so osh[0] is the number of
             * packet buffers the firmware currently holds. Reading it directly
             * beats probing the pool: the probe only reports how many more
             * buffers of one size can still be handed out, which stays flat
             * until the underlying memory is gone and then collapses, whereas
             * this counter rises the moment something stops freeing.
             *
             * Also returns the two counters that pkt_buf_get_skb keeps for its
             * fallback reserve pool (0x41bf0 = served from reserve, 0x41bf4 =
             * reserve empty too), which tell us whether the main pool is
             * already failing before the probe notices. */
            if (len >= 16) {
                unsigned int *out = (unsigned int *) arg;
                /* Launder the two literal addresses through volatile ints:
                 * dereferencing an integer constant directly trips gcc's
                 * -Warray-bounds, which this tree builds with -Werror. */
                volatile unsigned int ctr_addr = 0x41bf0;
                volatile unsigned int rsv_addr = 0x774;
                volatile unsigned int *ctr = (volatile unsigned int *) ctr_addr;
                volatile unsigned int *rsv = (volatile unsigned int *) rsv_addr;
                out[0] = ((unsigned int *) wlc->osh)[0];
                out[1] = ctr[0];
                out[2] = ctr[1];
                out[3] = rsv[0];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 609: // validate osh[0] as an outstanding-packet gauge
            /* Reads the counter before, during and after holding 16 packet
             * buffers, and returns the osh pointer itself. If osh[0] really is
             * the allocator's live count it must read 16 higher in the middle
             * sample; if it does not move, wlc->osh is not the structure the
             * ROM allocator increments and the gauge has to be found elsewhere. */
            if (len >= 16) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int *osh0 = (volatile unsigned int *) wlc->osh;
                void *bufs[16];
                int n;
                out[0] = osh0[0];
                for (n = 0; n < 16; n++)
                    bufs[n] = pkt_buf_get_skb(wlc->osh, 128);
                out[1] = osh0[0];
                while (n-- > 0)
                    if (bufs[n])
                        pkt_buf_free_skb(wlc->osh, bufs[n], 0);
                out[2] = osh0[0];
                out[3] = (unsigned int) wlc->osh;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 610: // walk the firmware heap free list
            /* The packet allocator is heap-backed, not a fixed pool: RAM 0x2cf0
             * (reached from pkt_buf_get_skb via the ROM thunk at 0x880ba0,
             * which jumps through the RAM pointer table entry at 0x488) sizes
             * the request, rejects anything over 0x838 bytes, and then calls
             * malloc at 0x25e4. A NULL from that malloc is the only reason
             * pkt_buf_get_skb fails while osh[0] - the count of packets the
             * firmware currently holds - reads zero, which is exactly what
             * channel hopping produces. So the thing to watch is the heap, not
             * the packet count.
             *
             * malloc's free list is a singly linked list of {size, next} blocks
             * whose head node lives at 0x41a4c (the constant returned by the
             * accessor at 0x25b4); the head itself carries no block, so the
             * walk starts at head->next. Also returns malloc's own failure
             * counter (0x728) and lb_alloc's (0x41af0), which distinguish "heap
             * had nothing left" from "the request was rejected before malloc".
             */
            if (len >= 20) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int head_addr = 0x41a4c;
                volatile unsigned int mfail_addr = 0x728;
                volatile unsigned int lbfail_addr = 0x41af0;
                unsigned int *node = ((unsigned int **) head_addr)[1];
                unsigned int total = 0, count = 0, biggest = 0, guard = 0;

                while (node != 0 && guard++ < 4096) {
                    unsigned int sz = node[0];
                    total += sz;
                    count++;
                    if (sz > biggest)
                        biggest = sz;
                    node = (unsigned int *) node[1];
                }
                out[0] = total;
                out[1] = count;
                out[2] = biggest;
                out[3] = ((volatile unsigned int *) mfail_addr)[0];
                out[4] = ((volatile unsigned int *) lbfail_addr)[0];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 611: // dump the raw heap free list
            /* Companion to 610, for telling heap exhaustion apart from free
             * list corruption. Channel hopping takes the list from ~74 kB in
             * 12 blocks straight to 0 bytes in 0 blocks in a single step, and
             * malloc's failure counter never moves - which is what losing the
             * head's next pointer looks like, not what running out of memory
             * looks like. Exhaustion would leave the head intact with a few
             * small blocks still on the list.
             *
             * Returns the head node's first four words followed by up to ten
             * {block address, block size} pairs, so the list can be compared
             * before and after: if the blocks still hold sane sizes but the
             * head no longer points at them, something overwrote the head. */
            if (len >= 96) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int head_addr = 0x41a4c;
                unsigned int *head = (unsigned int *) head_addr;
                unsigned int *node;
                int i;

                out[0] = head[0];
                out[1] = head[1];
                out[2] = head[2];
                out[3] = head[3];
                for (i = 0; i < 20; i++)
                    out[4 + i] = 0;
                node = (unsigned int *) head[1];
                for (i = 0; i < 10 && node != 0; i++) {
                    out[4 + 2 * i] = (unsigned int) node;
                    out[5 + 2 * i] = node[0];
                    node = (unsigned int *) node[1];
                }
                ret = IOCTL_SUCCESS;
            }
            break;

        case 612: // one consolidated, self-validating firmware health sample
            /* Every earlier probe here reported "0" for two very different
             * conditions: the firmware answered and the value really was zero,
             * or the firmware never answered at all and nexutil printed back
             * the buffer it had zeroed before the call. Once the chip stops
             * responding those look identical from the host, which is exactly
             * when the interesting measurements happen - so a run of zeros was
             * being read as "the pool is exhausted" or "the heap is gone" when
             * it only ever meant "no reply". out[0] is a magic word written
             * before anything else is measured: if the host does not read it
             * back, the whole sample must be thrown away rather than believed.
             *
             * Everything is gathered in one call so a single reply gives a
             * consistent picture:
             *   [1..3] heap free bytes, free block count, largest free block
             *   [4]    osh[0], packet buffers the firmware currently holds
             *   [5]    how many 128-byte buffers can still be allocated (<=64)
             *   [6]    malloc's own failure counter (0x728)
             *   [7]    lb_alloc's failure counter (0x41af0)
             */
            if (len >= 32) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int head_addr = 0x41a4c;
                volatile unsigned int mfail_addr = 0x728;
                volatile unsigned int lbfail_addr = 0x41af0;
                unsigned int *node = ((unsigned int **) head_addr)[1];
                unsigned int total = 0, count = 0, biggest = 0, guard = 0;
                void *bufs[64];
                int n;

                out[0] = 0x4E455831; /* "NEX1" - sample is valid */

                while (node != 0 && guard++ < 4096) {
                    unsigned int sz = node[0];
                    total += sz;
                    count++;
                    if (sz > biggest)
                        biggest = sz;
                    node = (unsigned int *) node[1];
                }
                out[1] = total;
                out[2] = count;
                out[3] = biggest;
                out[4] = ((volatile unsigned int *) wlc->osh)[0];

                for (n = 0; n < 64; n++) {
                    bufs[n] = pkt_buf_get_skb(wlc->osh, 128);
                    if (bufs[n] == 0)
                        break;
                }
                out[5] = n;
                while (n-- > 0)
                    pkt_buf_free_skb(wlc->osh, bufs[n], 0);

                out[6] = ((volatile unsigned int *) mfail_addr)[0];
                out[7] = ((volatile unsigned int *) lbfail_addr)[0];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 613: // passive health sample - measures without perturbing
            /* case 612 allocates and frees 64 packet buffers on every call
             * (that is its bufs= field), which disturbs exactly the subsystem
             * under investigation: sampling it every 10s wedged the chip on a
             * fixed channel with no hopping at all, in 140 seconds. Every
             * field here is a read, so repeated sampling costs nothing.
             *
             * Same magic-word contract as 612 - out[0] is written before
             * anything is measured, and a reply without it must be discarded
             * rather than read as a row of zeros.
             *   [1..3] heap free bytes, free block count, largest free block
             *   [4]    osh[0], packet buffers the firmware currently holds
             *   [5]    frames seen by the monitor hook since boot
             *   [6]    malloc's failure counter (0x728)
             *   [7]    lb_alloc's failure counter (0x41af0)
             */
            if (len >= 32) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int head_addr = 0x41a4c;
                volatile unsigned int mfail_addr = 0x728;
                volatile unsigned int lbfail_addr = 0x41af0;
                unsigned int *node = ((unsigned int **) head_addr)[1];
                unsigned int total = 0, count = 0, biggest = 0, guard = 0;

                out[0] = 0x4E455831; /* "NEX1" - sample is valid */

                while (node != 0 && guard++ < 4096) {
                    unsigned int sz = node[0];
                    total += sz;
                    count++;
                    if (sz > biggest)
                        biggest = sz;
                    node = (unsigned int *) node[1];
                }
                out[1] = total;
                out[2] = count;
                out[3] = biggest;
                out[4] = ((volatile unsigned int *) wlc->osh)[0];
                out[5] = nex_rx_frames;
                out[6] = ((volatile unsigned int *) mfail_addr)[0];
                out[7] = ((volatile unsigned int *) lbfail_addr)[0];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 614: // arm the periodic console heartbeat (arg[0] = period in ms)
            /* Armed on demand rather than at boot so that a run can be done
             * with and without it, and so an unused build behaves exactly as
             * before. Arming twice would leak a timer, hence the guard.
             */
            if (len >= 4) {
                unsigned int *out = (unsigned int *) arg;
                unsigned int ms = out[0] ? out[0] : 1000;

                nex_hb_wlc = wlc;

                /* TRCENA first - the DWT block does not respond until it is
                 * set. Zeroing CYCCNT before enabling makes the first delta
                 * meaningful rather than a count since reset. */
                DEMCR |= DEMCR_TRCENA;
                DWT_CYCCNT = 0;
                nex_hb_lastcyc = 0;
                DWT_CTRL |= DWT_CYCCNTENA;

                if (!nex_hb_armed) {
                    if (schedule_work(0, 0, nex_heartbeat, ms, 1)) {
                        nex_hb_armed = ms;
                        printf("NEXHB armed %u ms\n", ms);
                    }
                }
                out[0] = nex_hb_armed;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 615: // arg[0]=1 -> heartbeat clears MI_RXOV; 0 -> only observes it
            /* Both arms in one build, so the comparison is between two runs of
             * the same firmware rather than between two images. */
            if (len >= 4) {
                unsigned int *out = (unsigned int *) arg;
                nex_rxov_clear = out[0] ? 1 : 0;
                nex_rxov_seen = 0;
                nex_rxov_fill = 0;
                printf("NEXHB rxov_clear=%u\n", nex_rxov_clear);
                out[0] = nex_rxov_clear;
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

