.section .carrier_header, "ax", @progbits
.align 2
.global _carrier_header
.type _carrier_header, @function

_carrier_header:
	j carrier_entry
.size _carrier_header, . - _carrier_header

.section .carrier_entry, "ax", @progbits
.align 2
.global carrier_entry
.type carrier_entry, @function

carrier_entry:
	/* No fallible validation occurs in XIP. Prepare ILM/DLM and move the
	 * fixed SRAM rollback routine immediately. Stock OTA authenticates the
	 * complete carrier before this entry is selected. */
	setgie.d
	dsb
	sethi $sp, hi20(_stack)
	ori $sp, $sp, lo12(_stack)
	movi $r0, 0xf
	mtsr $r0, $ilmb
	sethi $r0, 0x100
	ori $r0, $r0, 0x9
	mtsr $r0, $dlmb
	mfsr $r0, $mmu_ctl
	ori $r0, $r0, 0x4
	mtsr $r0, $mmu_ctl

	sethi $r0, hi20(__rollback_load_start__)
	ori $r0, $r0, lo12(__rollback_load_start__)
	sethi $r1, hi20(__rollback_start__)
	ori $r1, $r1, lo12(__rollback_start__)
	sethi $r2, hi20(__rollback_end__)
	ori $r2, $r2, lo12(__rollback_end__)
1:
	lbi $r3, [$r0]
	sbi $r3, [$r1]
	addi $r0, $r0, 1
	addi $r1, $r1, 1
	bne $r1, $r2, 1b
	dsb
	isb
	sethi $r15, hi20(sv6x66_rollback_entry)
	ori $r15, $r15, lo12(sv6x66_rollback_entry)
	jr $r15

.size carrier_entry, . - carrier_entry

