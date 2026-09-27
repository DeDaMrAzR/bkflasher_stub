#ifndef HAL_GENERIC_H
#define HAL_GENERIC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define BUF_SIZE 0x6000
#if PLATFORM_W800
#include "W800/w800_hal.h"
#elif PLATFORM_RDA5981
#include "RDA5981/rda5981_hal.h"
#elif PLATFORM_RTL8710B
#include "RTL8710B/rtl8710b_hal.h"
#elif PLATFORM_RTL8720D
#include "RTL8720D/rtl8720d_hal.h"
#elif PLATFORM_ECR6600
#include "ECR6600/ecr6600_hal.h"
#elif PLATFORM_RTL8710C
#include "RTL8710C/rtl8710c_hal.h"
#elif PLATFORM_GD32VW553
#include "GD32VW553/gd32vw553_hal.h"
#elif PLATFORM_RTL8721DA
#include "RTL8721DA/rtl8721da_hal.h"
#elif PLATFORM_RTL8720E
#include "RTL8720E/rtl8720e_hal.h"
#elif PLATFORM_LN882H
#include "LN882H/ln882h_hal.h"
#elif PLATFORM_LN8825
#include "LN8825/ln8825_hal.h"
#elif PLATFORM_TR6260
#include "TR6260/tr6260_hal.h"
#elif PLATFORM_OPL1000A2
#include "OPL1000A2/opl1000a2_hal.h"
#endif

#ifndef REG32
#define REG32(a)				(*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define READ_REG32(addr)		(*((volatile unsigned int *)(addr)))
#define WRITE_REG32(REG, VAL)	((*(volatile unsigned int*)(REG)) = (unsigned int)(VAL))
#define OR_REG32(REG, VAL)		((*(volatile unsigned int*)(REG)) |= (unsigned int)(VAL))
#define WRITE_REG8(REG, VAL)	((*(volatile unsigned char*)(REG)) = (unsigned char)(VAL))
#ifndef BIT
#define BIT(x)					(1 << x)
#endif
#define MAX( a , b ) ( ( (a) > (b) ) ? (a) : (b) )
#define MIN( a , b ) ( ( (a) > (b) ) ? (b) : (a) )

/* Common OBK/Easy-Flasher custom-stub protocol.
 * These values are shared with the existing OBK custom stubs.
 * Handlers below map them onto W800-specific UART/flash/memory operations.
 */
#define OBK_STUB_MAGIC             0xA5U
#define OBK_STUB_ACK_MAGIC         0x5AU

#define OBK_CMD_SYNC               0x00U
#define OBK_CMD_RAM_DOWNLOAD       0x01U
#define OBK_CMD_FLASH_DOWNLOAD     0x02U
#define OBK_CMD_FLASH_UPLOAD       0x03U
#define OBK_CMD_FLASH_ERASE        0x04U
#define OBK_CMD_FLASH_CHIP_ERASE   0x05U
#define OBK_CMD_RUN                0x06U
#define OBK_CMD_BAUD_CHANGE        0x07U
#define OBK_CMD_FLASH_SHA256       0x09U
#define OBK_CMD_GET_CHIP_PRODUCT   0x20U
#define OBK_CMD_READ_REG           0x21U
#define OBK_CMD_WRITE_REG          0x22U
#define OBK_CMD_FLASH_CRC32        0x8FU
#define OBK_CMD_FLASH_ID           0x90U
#define OBK_CMD_FLASH_XMODEM_DL    0x91U  /* host -> target flash write */
#define OBK_CMD_FLASH_XMODEM_UL    0x92U  /* target -> host flash read */
#define OBK_CMD_KV_GET             0x93U
#define OBK_CMD_KV_SET             0x94U
#define OBK_CMD_GET_MAC            0x95U
#define OBK_CMD_FLASH_XMODEM_UL_Z  0x96U  /* compressed target -> host */
#define OBK_CMD_FLASH_XMODEM_DL_Z  0x97U  /* compressed host -> target */
#define OBK_CMD_RAW_XMODEM_UL      0x98U  /* target -> host absolute memory read */
#define OBK_CMD_READ_EFUSE         0x99U
#define OBK_CMD_READ_OTP           0x9AU
#define OBK_CMD_JUMP               0x9FU
#define OBK_CMD_AVAILABLE_CMDS     0xFFU

#define OBK_STATUS_SUCCESS         0x00U
#define OBK_STATUS_ERROR           0x01U
#define OBK_STATUS_ADDR_ERROR      0x02U
#define OBK_STATUS_TYPE_ERROR      0x03U
#define OBK_STATUS_LEN_ERROR       0x04U
#define OBK_STATUS_CRC_ERROR       0x05U
#define OBK_STATUS_NOT_SUPPORTED   0x10U
#define OBK_STATUS_OTP_ERROR       0x11U

#define SOH                        0x01U
#define STX                        0x02U
#define EOT                        0x04U
#define ACK                        0x06U
#define NAK                        0x15U
#define CAN                        0x18U
#define CRC_MODE                   0x43U

#define PROMPT_IDLE_LOOPS          900000U
#define RX_WAIT_LOOPS              20000000U
#define XMODEM_WAIT_LOOPS          50000000U
#define XMODEM_RESPONSE_WAIT_LOOPS 10000000U
#define XMODEM_MAX_RETRIES         30U

#define FLASH_SECTOR_SIZE          0x1000U
#define FLASH_BLOCK_SIZE           0x10000U
#define FLASH_PAGE_SIZE            0x100U

#ifndef WATCHDOG_REFRESH
#define WATCHDOG_REFRESH()
#endif

#define SPIFLASH_CMD_OTP_ENTRY			0x3A
#define SPIFLASH_CMD_OTP_EXIT			0x04
#define SPIFLASH_CMD_OTP_ENTRY_KH		0xB1
#define SPIFLASH_CMD_OTP_EXIT_KH		0xC1

typedef struct
{
	uint32_t data_len;
	uint8_t block;
	uint8_t use_crc;
	uint8_t use_1k;
	uint8_t failed;
} xmodem_tx_stream_t;

typedef struct
{
	uint32_t data_pos;
	uint32_t data_len;
	uint8_t expected_block;
	uint8_t started;
	uint8_t pending_ack;
	uint8_t failed;
} xmodem_rx_stream_t;

typedef struct
{
	uint32_t off;
	uint32_t expected_len;
	uint32_t written;
	uint32_t page_start;
	uint32_t page_len;
	uint8_t page[FLASH_PAGE_SIZE];
} inflate_flash_t;

typedef uint32_t alias_u32 __attribute__((may_alias));

extern uint32_t flash_id;
extern uint32_t flash_size;
extern uint8_t cmd_buf[BUF_SIZE];

uint32_t flash_size_from_jedec(uint32_t id);

void uart_set_baud(uint32_t baud);

void uart_init(void);

void uart_putc(uint8_t b);

int8_t uart_getc_timeout(uint8_t* out, uint32_t loops);

void flash_program_page(uint32_t off, const uint8_t* data);

int flash_erase_range(uint32_t off, uint32_t len);

int flash_erase_chip(void);

void flash_init(void);

void crc32_init_table(void);

uint32_t crc32_update_wire(uint32_t crc, uint8_t b);

int crc32_memory(uint32_t addr, uint32_t len, uint32_t* result);

int sha256_memory_hardware(uint32_t addr, uint32_t len);

int read_factory_mac(uint8_t mac[6]);

//int flash_range_is_erased(uint32_t off, uint32_t len);

//int flash_range_matches(uint32_t off, const uint8_t* data, uint32_t len);

int buffer_is_erased(const uint8_t* data, uint32_t len);

uint32_t reverse_u32(uint32_t value);

uint16_t crc16_xmodem(const uint8_t* data, uint32_t len);

int read_efuse(void);

uint8_t get_chip_data(void);

int read_otp(void);

void stub_flash_read(void* dest, uint32_t off, size_t len);

void boot_from_flash();

static inline uint32_t load_le32(const uint8_t* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static inline uint16_t load_le16(const uint8_t* p)
{
	return p[0] | (p[1] << 8);
}

static inline void delay_loops(volatile uint32_t loops)
{
	while(loops--) __asm volatile ("nop");
}

static inline void uart_write(const void* ptr, uint32_t len)
{
	const uint8_t* p = (const uint8_t*)ptr;
	while(len--) uart_putc(*p++);
}

static inline int range_does_not_wrap(uint32_t addr, uint32_t len)
{
	return len != 0U && addr <= (UINT32_MAX - (len - 1U));
}

static inline int range_is_within(uint32_t addr, uint32_t len, uint32_t base, uint32_t size)
{
	return len && addr >= base && addr - base < size && len <= size - (addr - base);
}

static inline int8_t uart_getc_inf(uint8_t* out)
{
	while(!uart_getc_timeout(out, 0xFFFFFFFF));
}

#endif
