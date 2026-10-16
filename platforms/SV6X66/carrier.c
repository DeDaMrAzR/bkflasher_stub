#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define MCU_WDT_CONTROL 0xC0000280U
#define SYS_WDT_CONTROL 0xC0000284U
#define WDT_ENABLE_MASK 0xC0000000U

#define SPI_CMD_SRAM       0xC0001000U
#define SPI_DATAIN_SRAM    0xC0001004U
#define SPI_CONTROL        0xC000100CU
#define SPI_TLEN           0xC0001010U
#define SPI_RLEN           0xC0001014U
#define SPI_BUSY_MASK      0x09U
#define SPI_MODE_MASK      0x06U
#define SPI_CMD_BUFFER     0x00008000U
#define SPI_PAYLOAD_BUFFER 0x00008004U
#define SPI_READ_BUFFER    0x00008400U

#define CMD_READ_STATUS 0x05U
#define CMD_WRITE_ENABLE 0x06U
#define CMD_FAST_READ 0x0BU
#define CMD_SECTOR_ERASE 0x20U
#define CMD_READ_JEDEC_ID 0x9FU

#define BOOT_FLAG_OFFSET 0x00004000U
#define BOOT_FLAG_XIP    0x30004000U
#define SV6X66_JEDEC_ID   0x001540C8U
#define STUB_DESTINATION 0x00004000U

#define ROLLBACK_TEXT __attribute__((section(".rollback.text"), always_inline)) inline

extern const uint8_t __stub_payload_start[];
extern const uint8_t __stub_payload_end[];

static ROLLBACK_TEXT void spi_wait_idle(void)
{
	while((REG32(SPI_CONTROL) & SPI_BUSY_MASK) != 0U) { }
}

static ROLLBACK_TEXT void spi_issue(uint32_t command, uint32_t transmit,
	uint32_t receive)
{
	spi_wait_idle();
	REG32(SPI_CMD_BUFFER) = command;
	REG32(SPI_RLEN) = receive;
	REG32(SPI_TLEN) = transmit;
	spi_wait_idle();
}

static ROLLBACK_TEXT uint8_t spi_read_byte(uint32_t index)
{
	uint32_t word = REG32(SPI_READ_BUFFER + (index & ~3U));
	return (uint8_t)(word >> ((index & 3U) * 8U));
}

static ROLLBACK_TEXT uint32_t command_word(uint8_t opcode, uint32_t address)
{
	return (uint32_t)opcode | ((address & 0x0000FFU) << 24) |
		((address & 0x00FF00U) << 8) | ((address & 0xFF0000U) >> 8);
}

static ROLLBACK_TEXT void flash_wait_ready(void)
{
	do {
		spi_issue(CMD_READ_STATUS, 1U, 3U);
	} while((spi_read_byte(0U) & 1U) != 0U);
}

static ROLLBACK_TEXT void flash_write_enable(void)
{
	spi_issue(CMD_WRITE_ENABLE, 1U, 0U);
}

static ROLLBACK_TEXT void flash_sector_erase(uint32_t address)
{
	flash_write_enable();
	spi_issue(command_word(CMD_SECTOR_ERASE, address), 4U, 0U);
	flash_wait_ready();
}

static ROLLBACK_TEXT void flash_fast_read(uint32_t address, uint32_t length)
{
	REG32(SPI_PAYLOAD_BUFFER) = 0U;
	spi_issue(command_word(CMD_FAST_READ, address), 5U, length);
}

static ROLLBACK_TEXT int boot_flag_sector_is_erased(void)
{
	uint32_t offset;
	for(offset = 0U; offset < 0x1000U; offset += 256U) {
		flash_fast_read(BOOT_FLAG_OFFSET + offset, 256U);
		for(uint32_t i = 0U; i < 256U; i++) {
			if(spi_read_byte(i) != 0xFFU) return 0;
		}
	}
	return 1;
}

static ROLLBACK_TEXT uint32_t read_jedec(void)
{
	spi_issue(CMD_READ_JEDEC_ID, 1U, 3U);
	return (uint32_t)spi_read_byte(0U) |
		((uint32_t)spi_read_byte(1U) << 8) |
		((uint32_t)spi_read_byte(2U) << 16);
}

static ROLLBACK_TEXT uint32_t crc32_bytes(const uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xFFFFFFFFU;
	while(length--) {
		crc ^= *data++;
		for(uint32_t bit = 0U; bit < 8U; bit++)
			crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
	}
	return crc ^ 0xFFFFFFFFU;
}

__attribute__((section(".rollback.entry"), used, noreturn))
void sv6x66_rollback_entry(void)
{
	const uint32_t inherited_mode = REG32(SPI_CONTROL) & SPI_MODE_MASK;
	const uint32_t payload_length =
		(uint32_t)(__stub_payload_end - __stub_payload_start);
	volatile uint8_t *const destination =
		(volatile uint8_t *)(uintptr_t)STUB_DESTINATION;

	/* Exact addresses and disable mask are confirmed from the active SV6x66
	 * stage-one's linked hal_wdt_disable implementation and HW_WDT_REG table. */
	REG32(MCU_WDT_CONTROL) &= ~WDT_ENABLE_MASK;
	REG32(SYS_WDT_CONTROL) &= ~WDT_ENABLE_MASK;

	REG32(SPI_CONTROL) &= ~SPI_MODE_MASK;
	__asm volatile("isb" ::: "memory");
	REG32(SPI_CMD_SRAM) = SPI_CMD_BUFFER;
	REG32(SPI_DATAIN_SRAM) = SPI_READ_BUFFER;

	/* This retry loop has no refusal/failure exit. The only transition out
	 * of SRAM occurs after the complete boot-flag sector verifies erased. */
	do {
		flash_sector_erase(BOOT_FLAG_OFFSET);
	} while(!boot_flag_sector_is_erased());

	REG32(SPI_CONTROL) = (REG32(SPI_CONTROL) & ~SPI_MODE_MASK) | inherited_mode;
	spi_wait_idle();
	__asm volatile("isb" ::: "memory");
	if(REG32(BOOT_FLAG_XIP) != 0xFFFFFFFFU) for(;;) { }

	/* The exact-profile gate is deliberately after rollback is armed. */
	REG32(SPI_CONTROL) &= ~SPI_MODE_MASK;
	__asm volatile("isb" ::: "memory");
	if(read_jedec() != SV6X66_JEDEC_ID) for(;;) { }
	REG32(SPI_CONTROL) = (REG32(SPI_CONTROL) & ~SPI_MODE_MASK) | inherited_mode;
	spi_wait_idle();
	__asm volatile("isb" ::: "memory");

	for(uint32_t i = 0U; i < payload_length; i++)
		destination[i] = __stub_payload_start[i];
	__asm volatile("dsb\n\tisb" ::: "memory");

	if(crc32_bytes(__stub_payload_start, payload_length) !=
		crc32_bytes((const uint8_t *)(uintptr_t)STUB_DESTINATION, payload_length))
		for(;;) { }

	((void (*)(void))(uintptr_t)STUB_DESTINATION)();
	for(;;) { }
}

