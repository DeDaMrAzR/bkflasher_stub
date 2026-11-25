.syntax unified
.cpu cortex-m3
.thumb

.section .text
.global _start
.type _start, %function
_start:
	cpsid i
	ldr r4, clock_control
	ldr r3, [r4]
	ldr r2, divider_clear
	ands r3, r2
	str r3, [r4]
	ldr r3, [r4]
	ldr r2, divider_final
	orrs r3, r2
	str r3, [r4]

	ldr r0, source
	ldr r1, destination
	ldr r2, length
copy_loop:
	ldrb r3, [r0]
	strb r3, [r1]
	adds r0, #1
	adds r1, #1
	subs r2, #1
	bne copy_loop

	ldr r0, baud_register
	ldr r1, entry
	bx r1
	nop

	.balign 4
clock_control:
	.word 0x40000710
divider_clear:
	.word 0xFFFFF0F0
divider_final:
	.word 0x80000212
source:
	.word 0x080021A0
destination:
	.word 0x20030000
length:
	.word 0x00000000
baud_register:
	.word 0x00000000
entry:
	.word 0x20030001

.size _start, . - _start
