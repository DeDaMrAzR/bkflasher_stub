#include "../hal_generic.h"

#define CMD_READ_JEDEC_ID 0x9FU
#define CMD_READ_STATUS1  0x05U
#define CMD_WRITE_ENABLE  0x06U
#define CMD_SECTOR_ERASE  0x20U
#define CMD_BLOCK_ERASE   0xD8U
#define CMD_PAGE_PROGRAM  0x02U
#define CMD_FAST_READ     0x0BU
#define CONTROLLER_LIMIT  10000000U
#define STATUS_LIMIT      8000000U
#define FLASH_READ_CHUNK  256U
#define HSUART_CLOCK_40M  4U
#define HSUART_CLOCK_80M  8U

static uint32_t prepared_baud_register;

static void store_le32(uint8_t* output, uint32_t value)
{
	output[0] = (uint8_t)value;
	output[1] = (uint8_t)(value >> 8);
	output[2] = (uint8_t)(value >> 16);
	output[3] = (uint8_t)(value >> 24);
}

static int baud_is_supported(uint32_t baud)
{
	switch(baud)
	{
		case 115200U:
		case 230400U:
		case 460800U:
		case 921600U:
		case 1500000U:
		case 2000000U:
			return 1;
		default:
			return 0;
	}
}

static int calculate_baud(uint32_t baud, uint32_t* clock,
	uint32_t* baud_register, uint32_t* achieved_baud)
{
	uint32_t clock_select = READ_REG32(HSUART_CLOCK_SELECT) & 0x0FU;

	if(!baud_is_supported(baud)) return 0;
	if(clock_select == HSUART_CLOCK_40M) *clock = 40000000U;
	else if(clock_select == HSUART_CLOCK_80M) *clock = 80000000U;
	else return 0;
	/*
	 * The hardware divider is integer plus an eight-bit fractional field.
	 * Select the nearest representable divisor rather than truncating the
	 * fraction, which minimizes line-rate error at every supported baud.
	 * The direct sequence and register fields were recovered from the
	 * MIT-compatible source investigation; no vendor object is linked.
	 */
	uint32_t base = *clock / 8U;
	uint32_t scaled_divider = (base * 256U + baud / 2U) / baud;
	uint32_t divider = scaled_divider >> 8;
	uint32_t fraction = scaled_divider & 0xFFU;
	if(divider == 0U || divider > 0xFFFFU) return 0;
	*baud_register = divider | (fraction << 16);
	*achieved_baud = (base * 256U + scaled_divider / 2U) / scaled_divider;
	return 1;
}

int stub_uart_prepare_baud(uint32_t baud, uint8_t* diagnostic)
{
	uint32_t clock;
	uint32_t achieved_baud;
	if(!calculate_baud(baud, &clock, &prepared_baud_register, &achieved_baud)) return 0;
	store_le32(diagnostic, baud);
	store_le32(diagnostic + 4, achieved_baud);
	store_le32(diagnostic + 8, clock);
	store_le32(diagnostic + 12, prepared_baud_register);
	return 1;
}

void stub_uart_commit_prepared_baud(void)
{
	while((READ_REG32(HSUART_LSR) & HSUART_LSR_TEMT) == 0U) { }
	delay_loops(BAUD_DELAY);
	WRITE_REG32(HSUART_LCR, 0x03U); /* 8 data bits, one stop bit, no parity. */
	WRITE_REG32(HSUART_BAUD, prepared_baud_register);
}

int stub_uart_set_baud_checked(uint32_t baud)
{
	uint32_t clock;
	uint32_t baud_register;
	uint32_t achieved_baud;
	if(!calculate_baud(baud, &clock, &baud_register, &achieved_baud)) return 0;

	while((READ_REG32(HSUART_LSR) & HSUART_LSR_TEMT) == 0U) { }
	WRITE_REG32(HSUART_LCR, 0x03U); /* 8 data bits, one stop bit, no parity. */
	WRITE_REG32(HSUART_BAUD, baud_register);
	return 1;
}

void uart_set_baud(uint32_t baud)
{
	(void)stub_uart_set_baud_checked(baud);
}

void uart_init(void)
{
	/* Stage two always begins its framed protocol at 115200 baud. */
	(void)stub_uart_set_baud_checked(115200U);
}

void uart_putc(uint8_t value)
{
	while((READ_REG32(HSUART_LSR) & HSUART_LSR_THRE) == 0U) { }
	WRITE_REG32(HSUART_DATA, value);
}

int8_t uart_getc_timeout(uint8_t* out, uint32_t loops)
{
	return uart_getc_budget(out, &loops);
}

int8_t uart_getc_budget(uint8_t* out, uint32_t* loops)
{
	while(*loops != 0U)
	{
		--*loops;
		if(READ_REG32(HSUART_LSR) & HSUART_LSR_DR)
		{
			*out = (uint8_t)READ_REG32(HSUART_DATA);
			return 1;
		}
	}
	return 0;
}

uint8_t get_chip_data(void)
{
	/* Explicit bytes keep the on-wire identity independent of host endianness. */
	cmd_buf[0] = 'S';
	cmd_buf[1] = 'V';
	cmd_buf[2] = '6';
	cmd_buf[3] = '6';
	return 4U;
}

/*
 * Adapted from the direct SPI-controller sequence in the MIT-licensed SV6x66
 * loader at SV6x66_loader commit 7aa8a641836d6fcdeb0b3af5714a4cc634f306b4.
 * This implementation links no vendor object or static library.
 */
static int controller_idle(void)
{
	uint32_t limit = CONTROLLER_LIMIT;
	while((READ_REG32(SPI_CONTROL) & SPI_BUSY_MASK) != 0U)
	{
		if(--limit == 0U) return 0;
	}
	return 1;
}

static int issue(uint32_t command, uint32_t transmit, uint32_t receive)
{
	if(!controller_idle()) return 0;
	WRITE_REG32(SPI_CMD_BUFFER, command);
	WRITE_REG32(SPI_RLEN, receive);
	WRITE_REG32(SPI_TLEN, transmit);
	return controller_idle();
}

static uint8_t read_buffer_byte(uint32_t index)
{
	uint32_t word = READ_REG32(SPI_READ_BUFFER + (index & ~3U));
	return (uint8_t)(word >> ((index & 3U) * 8U));
}

static uint32_t command_word(uint8_t opcode, uint32_t address)
{
	return (uint32_t)opcode | ((address & 0x0000FFU) << 24) |
		((address & 0x00FF00U) << 8) | ((address & 0xFF0000U) >> 8);
}

static int fast_read(uint32_t address, uint32_t length)
{
	WRITE_REG32(SPI_PAYLOAD_BUFFER, 0U);
	return issue(command_word(CMD_FAST_READ, address), 5U, length);
}

static int flash_ready(void)
{
	uint32_t limit = STATUS_LIMIT;
	while(limit--)
	{
		if(!issue(CMD_READ_STATUS1, 1U, 1U)) return 0;
		if((READ_REG32(SPI_READ_BUFFER) & 1U) == 0U) return 1;
	}
	return 0;
}

static int write_enable(void)
{
	return issue(CMD_WRITE_ENABLE, 1U, 0U) && flash_ready() &&
		(READ_REG32(SPI_READ_BUFFER) & 2U) != 0U;
}

static int erase_unit(uint32_t address, uint32_t size)
{
	uint8_t command = size == FLASH_BLOCK_SIZE ? CMD_BLOCK_ERASE : CMD_SECTOR_ERASE;
	if(!write_enable()) return 0;
	if(!issue(command_word(command, address), 4U, 0U)) return 0;
	return flash_ready();
}

int stub_flash_erase_range_progress(uint32_t off, uint32_t len, uint32_t* completed)
{
	*completed = 0U;
	if(flash_id != SV6X66_FLASH_ID || flash_size != STUB_READ_ONLY_FLASH_BYTES ||
		len == 0U || off > flash_size || len > flash_size - off ||
		(off & (FLASH_SECTOR_SIZE - 1U)) != 0U ||
		(len & (FLASH_SECTOR_SIZE - 1U)) != 0U) return 0;
	while(len)
	{
		uint32_t size = FLASH_SECTOR_SIZE;
		if((off & (FLASH_BLOCK_SIZE - 1U)) == 0U && len >= FLASH_BLOCK_SIZE)
			size = FLASH_BLOCK_SIZE;
		if(!erase_unit(off, size)) return 0;
		*completed += size;
		off += size;
		len -= size;
	}
	return 1;
}

int stub_flash_program_page_completed(uint32_t off, const uint8_t* data)
{
	if(flash_id != SV6X66_FLASH_ID || flash_size != STUB_READ_ONLY_FLASH_BYTES ||
		(off & (FLASH_PAGE_SIZE - 1U)) != 0U ||
		off > flash_size || FLASH_PAGE_SIZE > flash_size - off) return 0;
	if(!buffer_is_erased(data, FLASH_PAGE_SIZE))
	{
		for(uint32_t i = 0U; i < FLASH_PAGE_SIZE; i += 4U)
		{
			uint32_t word = (uint32_t)data[i] | ((uint32_t)data[i + 1U] << 8) |
				((uint32_t)data[i + 2U] << 16) | ((uint32_t)data[i + 3U] << 24);
			WRITE_REG32(SPI_PAYLOAD_BUFFER + i, word);
		}
		if(!write_enable()) return 0;
		if(!issue(command_word(CMD_PAGE_PROGRAM, off), FLASH_PAGE_SIZE + 4U, 0U)) return 0;
		if(!flash_ready()) return 0;
	}
	return 1;
}

void flash_init(void)
{
	uint8_t jedec[3];
	flash_id = 0U;
	if(!controller_idle()) return;
	WRITE_REG32(SPI_CONTROL, READ_REG32(SPI_CONTROL) & ~SPI_MODE_MASK);
	__asm volatile("isb" ::: "memory");
	if((READ_REG32(SPI_CONTROL) & SPI_MODE_MASK) != 0U) return;
	WRITE_REG32(SPI_CMD_SRAM, SPI_CMD_BUFFER);
	WRITE_REG32(SPI_DATAIN_SRAM, SPI_READ_BUFFER);
	if(!issue(CMD_READ_JEDEC_ID, 1U, 3U)) return;
	for(uint32_t i = 0U; i < 3U; i++) jedec[i] = read_buffer_byte(i);
	uint32_t id = (uint32_t)jedec[0] | ((uint32_t)jedec[1] << 8) |
		((uint32_t)jedec[2] << 16);
	if(id == SV6X66_FLASH_ID) flash_id = id;
}

int stub_flash_read_checked(void* dest, uint32_t off, uint32_t len)
{
	uint8_t* out = (uint8_t*)dest;
	if(off > STUB_READ_ONLY_FLASH_BYTES || len == 0U ||
		len > STUB_READ_ONLY_FLASH_BYTES - off) return 0;
	while(len)
	{
		uint32_t chunk = MIN(len, FLASH_READ_CHUNK);
		if(!fast_read(off, chunk)) return 0;
		for(uint32_t i = 0U; i < chunk; i += 4U)
		{
			uint32_t word = READ_REG32(SPI_READ_BUFFER + i);
			uint32_t count = MIN(4U, chunk - i);
			for(uint32_t byte = 0U; byte < count; byte++)
				out[i + byte] = (uint8_t)(word >> (8U * byte));
		}
		off += chunk;
		out += chunk;
		len -= chunk;
	}
	return 1;
}

int crc32_memory(uint32_t addr, uint32_t len, uint32_t* result)
{
	uint32_t crc = 0xFFFFFFFFU;
	if(addr > STUB_READ_ONLY_FLASH_BYTES || len == 0U ||
		len > STUB_READ_ONLY_FLASH_BYTES - addr) return 0;
	while(len)
	{
		uint32_t chunk = MIN(len, FLASH_READ_CHUNK);
		if(!fast_read(addr, chunk)) return 0;
		for(uint32_t i = 0U; i < chunk; i += 4U)
		{
			uint32_t word = READ_REG32(SPI_READ_BUFFER + i);
			uint32_t count = MIN(4U, chunk - i);
			for(uint32_t byte = 0U; byte < count; byte++)
				crc = crc32_update_wire(crc, (uint8_t)(word >> (8U * byte)));
		}
		addr += chunk;
		len -= chunk;
	}
	*result = crc ^ 0xFFFFFFFFU;
	return 1;
}

extern int main(void);

void __attribute__((noreturn)) sv6x66_boot_main(void)
{
	extern uint32_t __bss_start__[];
	extern uint32_t __bss_end__[];
	uint32_t* word = __bss_start__;

	while(word < __bss_end__) *word++ = 0U;
	main();
	while(1) { }
}
