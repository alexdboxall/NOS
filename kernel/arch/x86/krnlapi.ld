ENTRY(_krnlapi_start)
OUTPUT_FORMAT("elf32-i386")

SECTIONS
{
	. = 0;	
	
	.text ALIGN(4096) : AT (ADDR (.text))
	{
		*(.text)
		*(.rodata)
		*(.symtab)
		*(.strtab)
	}
	
	.data ALIGN(4096) : AT (ADDR (.data))
	{
		*(.data)
	}
	
	.bss ALIGN(4096) : AT (ADDR (.bss))
	{
		*(COMMON)
		*(.bss)
		*(.bootstrap_stack)
	}
		
	/DISCARD/ :
    {
        *(.comment)
    }
}