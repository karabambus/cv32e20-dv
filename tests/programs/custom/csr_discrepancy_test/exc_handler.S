/* exc_handler.S -custom u_sw_irq_handler for csr_discrepancy_test
 * Overrides the weak BSP handler.
 *
 * Protocol: before a CSR access expected to fault, the test sets
 * glb_expect_illegal_insn = 1.  The handler decrements it and advances
 * mepc by 4 (CSR instructions are always 32-bit).  Any unexpected
 * exception calls exit(1).
 */

#define EXCEPTION_ILLEGAL_INSN 2

.section .text
.global u_sw_irq_handler
.global glb_expect_illegal_insn
.global glb_last_mcause

u_sw_irq_handler:
    addi sp, sp, -64
    sw   ra,  0(sp)
    sw   a0,  4(sp)
    sw   a1,  8(sp)
    sw   a2, 12(sp)
    sw   a3, 16(sp)
    sw   a4, 20(sp)
    sw   a5, 24(sp)
    sw   a6, 28(sp)
    sw   a7, 32(sp)
    sw   t0, 36(sp)
    sw   t1, 40(sp)
    sw   t2, 44(sp)
    sw   t3, 48(sp)
    sw   t4, 52(sp)
    sw   t5, 56(sp)
    sw   t6, 60(sp)

    csrr t0, mcause
    la   t1, glb_last_mcause
    sw   t0, 0(t1)

    li   t1, EXCEPTION_ILLEGAL_INSN
    bne  t0, t1, unexpected_exc

    la   t1, glb_expect_illegal_insn
    lw   t0, 0(t1)
    beqz t0, unexpected_too_many
    addi t0, t0, -1
    sw   t0, 0(t1)

    /* CSR instructions are never compressed -always 4 bytes */
    csrr t0, mepc
    addi t0, t0, 4
    csrw mepc, t0

handler_ret:
    lw   ra,  0(sp)
    lw   a0,  4(sp)
    lw   a1,  8(sp)
    lw   a2, 12(sp)
    lw   a3, 16(sp)
    lw   a4, 20(sp)
    lw   a5, 24(sp)
    lw   a6, 28(sp)
    lw   a7, 32(sp)
    lw   t0, 36(sp)
    lw   t1, 40(sp)
    lw   t2, 44(sp)
    lw   t3, 48(sp)
    lw   t4, 52(sp)
    lw   t5, 56(sp)
    lw   t6, 60(sp)
    addi sp, sp, 64
    mret

unexpected_exc:
    la   a0, msg_unexpected_exc
    jal  ra, puts
    li   a0, 1
    jal  ra, exit

unexpected_too_many:
    la   a0, msg_too_many
    jal  ra, puts
    li   a0, 1
    jal  ra, exit

.section .rodata
msg_unexpected_exc:
    .string "HANDLER: unexpected exception -aborting\n"
msg_too_many:
    .string "HANDLER: more illegal-insn traps than expected -aborting\n"
