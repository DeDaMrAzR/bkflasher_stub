.syntax unified
.cpu cortex-m3
.thumb

.section .text.start, "ax", %progbits
.global _start
.type _start, %function

_start:
    cpsid i
    ldr sp, =0x20038000
    bl loader_main
1:
    b 1b

.size _start, . - _start

