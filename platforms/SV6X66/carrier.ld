ENTRY(_carrier_header)

MEMORY
{
	BANK_B (rx) : ORIGIN = 0x300A6000, LENGTH = 0x0001A000
	ILM (rwx)   : ORIGIN = 0x00007000, LENGTH = 0x00001000
}

SECTIONS
{
	.carrier_header ORIGIN(BANK_B) : ALIGN(4)
	{
		KEEP(*(.carrier_header))
	} > BANK_B

	.carrier_entry : ALIGN(4)
	{
		KEEP(*(.carrier_entry))
	} > BANK_B

	.stub_payload : ALIGN(4)
	{
		KEEP(*(.stub_payload))
	} > BANK_B

	.rollback ORIGIN(ILM) :
		AT(LOADADDR(.stub_payload) + SIZEOF(.stub_payload))
	{
		__rollback_start__ = .;
		KEEP(*(.rollback.entry))
		*(.rollback.text*)
		__rollback_end__ = .;
	} > ILM
	__rollback_load_start__ = LOADADDR(.rollback);
	__rollback_load_end__ = LOADADDR(.rollback) + SIZEOF(.rollback);
	__carrier_image_end__ = __rollback_load_end__;

	/DISCARD/ :
	{
		*(.text*)
		*(.rodata*)
		*(.data*)
		*(.bss*)
		*(.sdata*)
		*(.sbss*)
		*(COMMON)
		*(.comment*)
		*(.note*)
	}
}

PROVIDE(_stack = 0x00120000);

ASSERT(ADDR(.carrier_header) == 0x300A6000,
	"carrier must enter at the SV6x66 Bank-B base")
ASSERT(SIZEOF(.carrier_header) == 4,
	"carrier header must be one branch instruction")
ASSERT(SIZEOF(.stub_payload) > 0,
	"carrier must embed the current restore stub")
ASSERT(ADDR(.rollback) == 0x00007000,
	"rollback routine must execute from reserved ILM")
ASSERT(__rollback_end__ <= 0x00008000,
	"rollback routine overlaps SPI command SRAM")
ASSERT(__carrier_image_end__ - ORIGIN(BANK_B) <= 0x00002000,
	"carrier exceeds the 8 KiB first-validation ceiling")
ASSERT(0x00004000 + SIZEOF(.stub_payload) <= 0x00007000,
	"restore stub copy overlaps rollback routine")
