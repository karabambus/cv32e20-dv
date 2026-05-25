# umode_interrupt_mie.S
#
# Checks if an M-mode interrupt is taken in U-mode with mstatus.MIE=0 (required
# by Priv ISA v1.12/20211203 sec 3.1.6.1 / 3.1.9a). Triggers an MSI from U-mode twice: Phase A
# (MIE=0, must be taken) and Phase B (MIE=1, control). Exit value at 0x20000000:
# 123456789=PASS (A taken), 1=FAIL (A not taken, B taken: MIE-gated), 2=trigger
# problem. Addresses target the cv32e20-dv mm_ram testbench.

.include "user_define.h"

    .equ INT_GEN,    0x15000024     # simple interrupt generator (set/clear reg)
    .equ MSI_SET,    0x80000008     # bit31=set | bit3=MSI
    .equ MSI_CLR,    0x00000008     # bit31=clear | bit3=MSI
    .equ RESULT,     0x20000000     # mm_ram test-status register
    .equ MPP_MASK,   0x1800         # mstatus.MPP[12:11]
    .equ MPIE_MASK,  0x80           # mstatus.MPIE[7]
    .equ MIE_BIT,    0x8            # mstatus.MIE[3] / mie.MSIE[3]

    .section .text.start
    .globl _start
    .type _start, @function
_start:
    j _start_main

    .section .text
_start_main:
    li   t1, INT_GEN                # clear any stale MSI
    li   t0, MSI_CLR
    sw   t0, 0(t1)

    la   t0, vector_table           # vectored trap base (cv32e20 is vectored-only)
    ori  t0, t0, 1
    csrw mtvec, t0

    li   s0, 0                      # s0 = Phase A taken flag
    li   s1, 0                      # s1 = Phase B taken flag
    li   s2, 0                      # s2 = current phase (0=A, 1=B)

    # ---- Phase A: enter U-mode with MIE = 0 ----
    li   t0, MIE_BIT
    csrw mie, t0                    # mie.MSIE = 1
    csrci mstatus, MIE_BIT          # mstatus.MIE = 0
    li   t0, MPIE_MASK
    csrc mstatus, t0                # MPIE = 0 -> U-mode MIE = 0 after mret
    li   t0, MPP_MASK
    csrc mstatus, t0                # MPP = U
    la   t0, umode_spin
    csrw mepc, t0
    mret

# Shared U-mode body: assert MSI, wait, then leave via ecall.
umode_spin:
    li   t1, INT_GEN
    li   t0, MSI_SET
    sw   t0, 0(t1)
    li   t2, 4000
1:  addi t2, t2, -1
    bnez t2, 1b
    ecall                           # synchronous -> vector base (offset 0)

    .balign 256                     # cv32e20 MTVEC_BASE_ALIGNMENT_VECTORED = 256
vector_table:
    jal  x0, exc_handler            # +0   exceptions (incl. U-mode ecall)
    jal  x0, fail_vec               # +4   cause 1
    jal  x0, fail_vec               # +8   cause 2
    jal  x0, msi_handler            # +12  cause 3 = machine software interrupt
    jal  x0, fail_vec               # +16  cause 4
    jal  x0, fail_vec               # +20  cause 5
    jal  x0, fail_vec               # +24  cause 6
    jal  x0, fail_vec               # +28  cause 7 = machine timer
    jal  x0, fail_vec               # +32  cause 8
    jal  x0, fail_vec               # +36  cause 9
    jal  x0, fail_vec               # +40  cause 10
    jal  x0, fail_vec               # +44  cause 11 = machine external

# MSI taken: record for the current phase, clear it, return to U-mode.
msi_handler:
    bnez s2, 1f
    li   s0, 1
    j    2f
1:  li   s1, 1
2:  li   t1, INT_GEN
    li   t0, MSI_CLR
    sw   t0, 0(t1)
    mret

# Reached when the U-mode spin finishes (via ecall) without the MSI being taken.
exc_handler:
    li   t1, INT_GEN                # drop any still-pending MSI
    li   t0, MSI_CLR
    sw   t0, 0(t1)
    bnez s2, report

    # ---- Phase B: enter U-mode with MIE = 1 ----
    li   s2, 1
    li   t0, MIE_BIT
    csrw mie, t0
    csrci mstatus, MIE_BIT
    li   t0, MPIE_MASK
    csrs mstatus, t0                # MPIE = 1 -> U-mode MIE = 1 after mret
    li   t0, MPP_MASK
    csrc mstatus, t0                # MPP = U
    la   t0, umode_spin
    csrw mepc, t0
    mret

report:
    bnez s0, pass                   # Phase A taken -> spec-compliant
    bnez s1, fail                   # A not taken, B taken -> MIE-gated bug
    li   t0, 2                      # neither taken -> trigger problem
    j    write_exit
fail:
    li   t0, 1
    j    write_exit
pass:
    li   t0, 123456789
write_exit:
    li   t1, RESULT
    sw   t0, 0(t1)
    wfi
3:  j 3b

fail_vec:                           # unexpected vector
    li   t0, 1
    li   t1, RESULT
    sw   t0, 0(t1)
    wfi
4:  j 4b
