ENTRY(_boot_start)

MEMORY
{
	ILM (rwx) : ORIGIN = 0x00004000, LENGTH = 0x0000B400
}

SECTIONS
{
	.entry ORIGIN(ILM) : ALIGN(4)
	{
		KEEP(*(.entry))
	} > ILM

	.text : ALIGN(4)
	{
		*(.text.startup*)
		*(.text*)
		*(.rodata*)
	} > ILM

	.data : ALIGN(4)
	{
		*(.data*)
		*(.sdata*)
	} > ILM

	.bss (NOLOAD) : ALIGN(4)
	{
		__bss_start__ = .;
		*(.bss*)
		*(.sbss*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end__ = .;
	} > ILM

	__ilm_alloc_end__ = .;

	/DISCARD/ :
	{
		*(.comment*)
		*(.note*)
	}
}

/* Source-derived SV6x66 ASICv2 + NO_ROM DLM top; the stub never returns. */
PROVIDE(_stack = 0x00120000);

ASSERT(ADDR(.entry) == 0x00004000, "SV6X66 entry must be 0x00004000")
ASSERT(__ilm_alloc_end__ <= 0x0000F400, "SV6X66 stub exceeds staging ILM")
ASSERT(__ilm_alloc_end__ <= 0x00008000, "SV6X66 stub overlaps SPI command SRAM")
