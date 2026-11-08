ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x20030000, LENGTH = 0x00008000
}

PHDRS
{
    code PT_LOAD FLAGS(5); /* read + execute */
    data PT_LOAD FLAGS(6); /* read + write */
}

SECTIONS
{
    .text :
    {
        KEEP(*(.text.start))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > RAM :code

    .data :
    {
        *(.data*)
        . = ALIGN(4);
    } > RAM :data

    .bss (NOLOAD) :
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM :data

    __loader_end__ = .;
    ASSERT(__loader_end__ <= 0x20037000, "loader leaves less than 4 KiB for stack")
}
