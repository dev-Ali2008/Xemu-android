/*
 * tlb-prefetch.h  —  TLB-aware hardware prefetch helpers for TCG
 *
 * Copyright (c) 2025 QEMU contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * ─────────────────────────────────────────────────────────────────────────
 * THE PROBLEM
 * -----------
 * Every guest load/store goes through:
 *
 *   mmu_lookup1()
 *     → tlb_index()      (shift + mask, free)
 *     → tlb_entry()      (pointer arithmetic, free)
 *     → tlb_read_idx()   (atomic load)  ← DRAM LATENCY HERE if cold
 *     → fulltlb[index]   (second cache-line)  ← DRAM LATENCY again
 *
 * For a guest running a 3-D game:
 *   • Vertex buffers → thousands of sequential stores
 *   • Texture sampling → scattered 4-byte loads
 *   • Index buffers → sequential 2/4-byte loads
 *
 * All of these hit the *same* small set of TLB entries repeatedly.
 * But on first access to each page, both the CPUTLBEntry and the
 * CPUTLBEntryFull sit in DRAM (~100 ns).  Prefetching them hides that
 * latency completely.
 *
 * WHAT THIS HEADER PROVIDES
 * --------------------------
 *  tlb_prefetch_entry()     — prefetch CPUTLBEntry + CPUTLBEntryFull
 *                             given a (cpu, mmu_idx, addr) triple.
 *                             Call this *before* the tlb_read_idx() that
 *                             will consume the result.
 *
 *  tlb_prefetch_next_page() — speculative prefetch for the TLB entry
 *                             of (addr + TARGET_PAGE_SIZE).  Useful in
 *                             loops that walk guest memory linearly
 *                             (memcpy, vertex processing, etc.).
 *
 *  TLB_PREFETCH_WRITE       — prefetch variant for store paths (rw=1).
 *
 * USAGE PATTERN (in cputlb.c or a future fast-path helper)
 * ---------------------------------------------------------
 *   // Issue the prefetch *before* other bookkeeping so the CPU can
 *   // overlap DRAM fetch with the oi decode, memop checks, etc.
 *   tlb_prefetch_entry(cpu, mmu_idx, addr);
 *   ...
 *   // By the time we call tlb_read_idx() the data is in L1d.
 *   uint64_t tlb_addr = tlb_read_idx(entry, access_type);
 *
 * SAFETY
 * ------
 * __builtin_prefetch is a pure hint.  If the address is unmapped or
 * crosses a protection boundary the CPU simply ignores the hint.
 * There are NO correctness consequences; only performance ones.
 * ─────────────────────────────────────────────────────────────────────────
 */
 

#ifndef ACCEL_TCG_TLB_PREFETCH_H
#define ACCEL_TCG_TLB_PREFETCH_H

#include "qemu/compiler.h"
#include "exec/cpu-common.h"          /* CPUState                    */
#include "exec/tlb-common.h"          /* CPUTLBEntry, CPUTLBDescFast */
#include "exec/vaddr.h"               /* vaddr                       */
#include "cpu-exec-opt.h"             /* EXEC_LIKELY / EXEC_UNLIKELY */

/* =========================================================================
 * Locality knobs
 * =========================================================================
 *  3 = temporal   — keep in L1/L2/L3 (best for working-set that fits)
 *  2 = L2/L3 only (streaming; avoids L1 pollution for scatter-gather)
 *  0 = non-temporal (hardware streaming prefetcher handles it)
 *
 * TLB entries are tiny (CPUTLBEntry = 3 × uintptr_t ≈ 24 bytes on 64-bit)
 * and are accessed with high temporal locality during execution of any
 * single TB → keep at 3.
 */
#ifndef TLB_ENTRY_PREFETCH_LOCALITY
#  define TLB_ENTRY_PREFETCH_LOCALITY   3
#endif

#ifndef TLB_FULL_PREFETCH_LOCALITY
#  define TLB_FULL_PREFETCH_LOCALITY    3
#endif

/* Write-intent hint (rw=1) for store-path prefetches */
#define TLB_PREFETCH_WRITE  1
#define TLB_PREFETCH_READ   0

/* =========================================================================
 * Core helper — low-level raw pointer prefetch
 *
 * NOTE: __builtin_prefetch requires constant arguments for rw and locality.
 * We ignore the rw parameter and always use 0 (read) because the performance
 * difference between read and write prefetch is negligible for TLB entries.
 * ========================================================================= */

#if defined(__GNUC__) || defined(__clang__)

#define tlb_prefetch_ptr(ptr, rw, locality) \
    __builtin_prefetch((ptr), 0, (locality))

#else

#define tlb_prefetch_ptr(ptr, rw, locality)  ((void)0)

#endif

/* =========================================================================
 * tlb_prefetch_entry()
 *
 * Prefetch both the fast CPUTLBEntry *and* the CPUTLBEntryFull metadata
 * for the TLB slot that will serve @addr in @mmu_idx.
 *
 * These are two separate allocations (fast->table[] and desc->fulltlb[])
 * and therefore potentially on two different cache lines.  Issuing both
 * prefetches together gives the CPU maximum time to fetch them in parallel
 * before mmu_lookup1() reads them sequentially.
 *
 * @cpu:     CPUState of the executing vCPU
 * @mmu_idx: MMU index for the access
 * @addr:    guest virtual address of the access
 * @rw:      TLB_PREFETCH_READ or TLB_PREFETCH_WRITE (ignored)
 * ========================================================================= */
static QEMU_ALWAYS_INLINE void
tlb_prefetch_entry(CPUState *cpu, int mmu_idx, vaddr addr, int rw)
{
    CPUTLBDescFast *fast = cpu_tlb_fast(cpu, mmu_idx);
    uintptr_t index = (addr >> TARGET_PAGE_BITS) &
                      (fast->mask >> CPU_TLB_ENTRY_BITS);

    /* Prefetch the fast TLB entry (addr_read/write/code + addend) */
    tlb_prefetch_ptr(&fast->table[index], rw, TLB_ENTRY_PREFETCH_LOCALITY);

    /* Prefetch the full TLB entry (xlat_section, attrs, slow_flags …) */
    tlb_prefetch_ptr(&cpu->neg.tlb.d[mmu_idx].fulltlb[index],
                     rw, TLB_FULL_PREFETCH_LOCALITY);
}

/* =========================================================================
 * tlb_prefetch_next_page()
 *
 * Speculatively prefetch the TLB entry for the *next* guest page after
 * @addr.  Useful in tight inner loops that walk guest memory linearly
 * (e.g. memset/memcpy helpers, vertex buffer processing, streaming I/O).
 *
 * The address arithmetic wraps correctly because vaddr is unsigned and
 * the prefetch is just a hint — there are no exceptions on bad addresses.
 *
 * @cpu:     CPUState of the executing vCPU
 * @mmu_idx: MMU index for the access
 * @addr:    guest virtual address of the *current* access
 * @rw:      TLB_PREFETCH_READ or TLB_PREFETCH_WRITE (ignored)
 * ========================================================================= */
static QEMU_ALWAYS_INLINE void
tlb_prefetch_next_page(CPUState *cpu, int mmu_idx, vaddr addr, int rw)
{
    tlb_prefetch_entry(cpu, mmu_idx, addr + TARGET_PAGE_SIZE, rw);
}

/* =========================================================================
 * tlb_prefetch_for_memop()
 *
 * Combined helper: prefetch both the current page's TLB entry and,
 * for accesses that may cross a page boundary (size > 1 and addr is
 * within @size bytes of a page boundary), the next page's entry too.
 *
 * This covers the crosspage case in mmu_lookup() without requiring
 * knowledge of whether a crossing will actually happen.
 *
 * @cpu:     CPUState
 * @mmu_idx: MMU index
 * @addr:    guest virtual address
 * @size:    access size in bytes (1, 2, 4, 8, 16 …)
 * @rw:      TLB_PREFETCH_READ or TLB_PREFETCH_WRITE (ignored)
 * ========================================================================= */
static QEMU_ALWAYS_INLINE void
tlb_prefetch_for_memop(CPUState *cpu, int mmu_idx,
                       vaddr addr, unsigned size, int rw)
{
    tlb_prefetch_entry(cpu, mmu_idx, addr, rw);

    /*
     * Issue the next-page prefetch only when the access might actually
     * cross a page boundary — i.e. the last byte is on a different page.
     * The check is a cheap bitwise test; no branch misprediction risk.
     */
    if (EXEC_UNLIKELY(size > 1)) {
        vaddr last = addr + size - 1;
        if (EXEC_UNLIKELY((addr ^ last) & TARGET_PAGE_MASK)) {
            tlb_prefetch_next_page(cpu, mmu_idx, last, rw);
        }
    }
}

/* =========================================================================
 * tlb_prefetch_range()
 *
 * Prefetch TLB entries for every page in [addr, addr+len).
 * Intended for bulk helpers (guest memset/memcpy) where we know the
 * range upfront.  Capped at TLB_PREFETCH_RANGE_MAX_PAGES to avoid
 * prefetch storms that can hurt performance.
 *
 * @cpu:     CPUState
 * @mmu_idx: MMU index
 * @addr:    start of the guest address range
 * @len:     length in bytes
 * @rw:      TLB_PREFETCH_READ or TLB_PREFETCH_WRITE (ignored)
 * ========================================================================= */
#ifndef TLB_PREFETCH_RANGE_MAX_PAGES
#  define TLB_PREFETCH_RANGE_MAX_PAGES  8
#endif

static QEMU_ALWAYS_INLINE void
tlb_prefetch_range(CPUState *cpu, int mmu_idx,
                   vaddr addr, vaddr len, int rw)
{
    vaddr page = addr & TARGET_PAGE_MASK;
    vaddr end  = addr + len;
    int   n    = 0;

    for (; page < end && n < TLB_PREFETCH_RANGE_MAX_PAGES;
         page += TARGET_PAGE_SIZE, n++) {
        tlb_prefetch_entry(cpu, mmu_idx, page, rw);
    }
}

#endif /* ACCEL_TCG_TLB_PREFETCH_H */