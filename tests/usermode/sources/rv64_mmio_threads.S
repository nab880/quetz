/* Freestanding Linux clone test. Four guest CPUs access their own SST slot,
 * concurrently, and each mailbox reply identifies the requesting CPU. */
    .option norvc
    .option norelax
    .text
    .globl _start
_start:
    li s0, 0
    call exercise
    li s0, 1
spawn:
    la a1, stacks
    slli t0, s0, 12
    add a1, a1, t0
    li a0, 0x50f00       /* VM | FS | FILES | SIGHAND | THREAD | SYSVSEM */
    li a2, 0
    li a3, 0
    li a4, 0
    li a7, 220          /* clone */
    ecall
    beqz a0, child
    bltz a0, failure
    addi s0, s0, 1
    li t0, 4
    bne s0, t0, spawn
    call exercise      /* main overlaps with its three children */
wait:
    la t0, done
    lw t1, 0(t0)
    li t2, 3
    bne t1, t2, wait
    la t0, errors
    lw a0, 0(t0)
    li a7, 94          /* exit_group */
    ecall
child:
    call exercise
    la t0, done
    li t1, 1
    amoadd.w zero, t1, (t0)
    li a0, 0
    li a7, 93          /* exit */
    ecall
failure:
    li a0, 2
    li a7, 94
    ecall
exercise:
    /* Main leaves s0=4 after cloning but remains CPU 0. */
    andi t3, s0, 3
    li t4, 128
    li t5, 0x70000000
    addi t6, t3, 0x100
1:
    ld t0, 0(t5)
    bne t0, t6, bad
    sd t3, 16(t5)
    addi t4, t4, -1
    bnez t4, 1b
    ret
bad:
    la t0, errors
    li t1, 1
    amoor.w zero, t1, (t0)
    ret
    .bss
    .balign 4096
stacks:
    .space 16384
    .balign 8
done:
    .word 0
errors:
    .word 0
