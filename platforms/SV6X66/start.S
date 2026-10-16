.section .entry, "ax"
.align 2
.global _boot_start
.type _boot_start, @function

_boot_start:
	setgie.d
	dsb
	sethi $sp, hi20(_stack)
	ori $sp, $sp, lo12(_stack)
	bal sv6x66_boot_main

1:
	j 1b

.size _boot_start, . - _boot_start
