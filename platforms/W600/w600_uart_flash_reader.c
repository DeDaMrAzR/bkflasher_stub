#include <stdint.h>

#define REG8(address) (*(volatile uint8_t *)(uintptr_t)(address))
#define REG32(address) (*(volatile uint32_t *)(address))

#define UART0_BASE        0x40010800u
#define UART1_BASE        0x40010A00u
#define UART0_FIFO_STATUS 0x4001081Cu
#define UART0_TX_WINDOW   0x40010820u
#define UART0_RX_WINDOW   0x40010830u
#define UART_TX_COUNT     0x0000003Fu
#define UART_RX_COUNT     0x000003C0u
#define UART_BAUD_115200  0x000B0014u
#define UART_BAUD_1500000 0x000A0000u

#define CLOCK_RESET_CONTROL 0x4000070Cu

#define GPIOB_DATA         0x40011200u
#define GPIOB_DATA_ENABLE  0x40011204u
#define GPIOB_DIRECTION    0x40011208u
#define GPIOB_PULL_ENABLE  0x4001120Cu
#define GPIOB_AF_SELECT    0x40011210u
#define GPIOA_PULL_ENABLE 0x40010C0Cu
#define GPIOA_AF_SELECT   0x40010C10u
#define GPIOA_AF_SELECT_1 0x40010C14u
#define GPIOA_AF_SELECT_0 0x40010C18u
#define GPIOA_UART0_RX     (1u << 5)
#define SECBOOT_SPI_PINS   ((1u << 2) | (1u << 3) | (1u << 9) | (1u << 11))

#define SPI_CHANNEL_CONFIG 0x40010400u
#define SPI_CONFIG         0x40010404u
#define SPI_CLOCK_CONFIG   0x40010408u
#define SPI_INT_MASK       0x40010410u
#define SPI_INT_STATUS     0x40010414u

#define NVIC_CLEAR_PENDING_0 0xE000E280u
#define NVIC_CLEAR_PENDING_1 0xE000E284u
#define WATCHDOG_CONTROL  0x40011008u
#define WATCHDOG_INT_CLR  0x4001100Cu
#define WATCHDOG_LOAD     0x40011000u
#define WATCHDOG_LOCK     0x40011040u

#define FLASH_COMMAND     0x40002000u
#define FLASH_ADDRESS     0x40002004u
#define FLASH_DATA_WINDOW 0x40002200u
#define FLASH_READ_COMMAND 0xEC00C0EBu
#define FLASH_JEDEC_ID_COMMAND 0x0002C09Fu
#define FLASH_TRIGGER          0x10000000u

#define FLASH_BASE        0x08000000u
#define FLASH_SIZE        0x00100000u
#define FLASH_READ_WINDOW 1024u
#define MAX_READ_LENGTH   16384u
#define PROTOCOL_VERSION  2u
#define REQUEST_SIZE      16u
#define RESPONSE_SIZE     20u
#define OP_INFO           0u
#define OP_READ           1u
#define OP_RETURN_TO_ROM  2u
#define OP_FLASH_ID       4u

#define STATUS_OK           0u
#define STATUS_BAD_HEADER   1u
#define STATUS_BAD_VERSION  2u
#define STATUS_BAD_OPCODE   3u
#define STATUS_BAD_RANGE    4u
#define STATUS_SPI_FAILURE  5u
#define STATUS_SPI_CLEANUP_FAILED 6u

#define SPI_BASE 0x40010400u
#define SPI_STATUS (SPI_BASE + 0x18u)
#define SPI_RX_DATA (SPI_BASE + 0x30u)
#define SPI_TX_DATA (SPI_BASE + 0x20u)
#define SPI_BUSY (1u << 12)
#define SPI_FIFO_COUNTS 0xFFFu
#define SPI_POLL_LIMIT 100000u
#define SECONDARY_NO_RESPONSE 0u
#define SECONDARY_SUPPORTED 1u
#define SECONDARY_UNSUPPORTED 2u
#define SECONDARY_TIMEOUT 3u
#define SECONDARY_UNSTABLE 4u
#define SECONDARY_BUSY 5u
#define SECONDARY_CLEANUP_FAILED 6u

static uint32_t primary_id;
static uint32_t secondary_id;
static uint32_t secondary_state;
static uint32_t secondary_size;
static uint32_t secondary_initial_state[4];

static uint8_t request[REQUEST_SIZE];
static uint8_t payload[MAX_READ_LENGTH];

static void bitband_write(uint32_t address, uint32_t bit, uint32_t value)
{
	uint32_t alias = 0x42000000u + ((address - 0x40000000u) * 32u) + (bit * 4u);
	REG32(alias) = value;
}

static void secboot_gpio_output_low_floating(uint32_t bit)
{
	REG32(GPIOB_AF_SELECT) &= ~bit;
	REG32(GPIOB_DATA_ENABLE) |= bit;
	REG32(GPIOB_DIRECTION) |= bit;
	REG32(GPIOB_PULL_ENABLE) |= bit;
	REG32(GPIOB_DATA) &= ~bit;
}

static void secboot_uart_register_init(uint32_t base, uint32_t baud_register)
{
	REG32(base + 0x10u) = baud_register;
	REG32(base + 0x00u) = 0xC3u;
	REG32(base + 0x04u) = 0u;
	REG32(base + 0x08u) = 0u;
	REG32(base + 0x0Cu) = 0u;
	REG32(base + 0x14u) = 0u;
}

static void secboot_spi_reset_and_normalize_gpio(void)
{
	bitband_write(CLOCK_RESET_CONTROL, 12u, 0u);
	bitband_write(CLOCK_RESET_CONTROL, 12u, 1u);
	REG32(GPIOB_AF_SELECT) &= 0xF7FFFFF0u;
}

static void secboot_spi_open_close(void)
{
	secboot_spi_reset_and_normalize_gpio();

	REG32(GPIOA_AF_SELECT) |= SECBOOT_SPI_PINS;
	REG32(GPIOA_AF_SELECT_1) |= (1u << 11) | (1u << 9) | (1u << 3) | (1u << 2);
	REG32(GPIOA_AF_SELECT_0) |= (1u << 11) | (1u << 9);
	REG32(GPIOA_AF_SELECT_0) &= ~((1u << 3) | (1u << 2));

	REG32(SPI_CHANNEL_CONFIG) = 0x00580006u;
	REG32(SPI_CONFIG) = 4u;
	REG32(SPI_CLOCK_CONFIG) = 1u;
	REG32(SPI_INT_MASK) = 0xFFu;
	REG32(SPI_INT_STATUS) = 0xFFu;
	REG32(NVIC_CLEAR_PENDING_0) = 1u << 21;

	secboot_spi_reset_and_normalize_gpio();
	REG32(GPIOA_AF_SELECT) &= ~SECBOOT_SPI_PINS;
}

static void reproduce_secboot_pre_application_state(uint32_t baud_register)
{
	static const uint8_t gpio_bits[] = {8u, 9u, 13u, 14u, 15u, 16u, 17u, 18u};
	uint32_t i;

	for (i = 0u; i < sizeof(gpio_bits); ++i) {
		secboot_gpio_output_low_floating(1u << gpio_bits[i]);
	}
	REG32(GPIOB_PULL_ENABLE) &= ~(1u << 11);
	REG32(GPIOA_PULL_ENABLE) &= ~GPIOA_UART0_RX;

	secboot_uart_register_init(UART0_BASE, UART_BAUD_115200);
	secboot_uart_register_init(UART1_BASE, UART_BAUD_115200);
	secboot_spi_open_close();
	REG32(NVIC_CLEAR_PENDING_1) = 0x80u;

	secboot_uart_register_init(UART0_BASE, baud_register);
}

static uint16_t load16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t load32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void store32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t crc16_ccitt_false(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint32_t bit;

    for (i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint32_t crc32_ieee(const uint8_t *data, uint32_t length)
{
	static const uint32_t crc32_table[16] = {
		0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
		0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
		0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
		0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
	};
	uint32_t crc = 0xFFFFFFFFu;
	uint32_t i;

	for (i = 0u; i < length; ++i) {
		crc ^= data[i];
		crc = crc32_table[crc & 0xFu] ^ (crc >> 4);
		crc = crc32_table[crc & 0xFu] ^ (crc >> 4);
	}
	return ~crc;
}

static void uart_put(uint8_t value)
{
	while ((REG32(UART0_FIFO_STATUS) & UART_TX_COUNT) >= 32u) {
	}
	REG8(UART0_TX_WINDOW) = value;
}

static uint8_t uart_get(void)
{
    while ((REG32(UART0_FIFO_STATUS) & UART_RX_COUNT) == 0u) {
    }
	return REG8(UART0_RX_WINDOW);
}

static void uart_write(const uint8_t *data, uint32_t length)
{
    uint32_t i;
    for (i = 0; i < length; ++i) {
        uart_put(data[i]);
    }
}

static void uart_init(uint32_t baud_register)
{
	reproduce_secboot_pre_application_state(baud_register);
}

static void receive_request(void)
{
    uint32_t matched = 0u;
    uint8_t ch;
    static const uint8_t magic[4] = {'W', '6', 'R', 'Q'};

    while (matched < 4u) {
        ch = uart_get();
        if (ch == magic[matched]) {
            request[matched++] = ch;
        } else {
            matched = (ch == magic[0]) ? 1u : 0u;
            if (matched != 0u) {
                request[0] = ch;
            }
        }
    }
    for (matched = 4u; matched < REQUEST_SIZE; ++matched) {
        request[matched] = uart_get();
    }
}

/* Read-only transaction matching the vendor SDK's internal QFLASH path. */
static void flash_read(uint32_t cpu_address, uint8_t *destination, uint32_t length)
{
	uint32_t completed = 0u;

	while (completed < length) {
		uint32_t block_length = length - completed;
		uint32_t offset = cpu_address + completed - FLASH_BASE;
		uint32_t words;
		uint32_t i;
		uint32_t word;
		uint32_t produced = 0u;

		if (block_length > FLASH_READ_WINDOW) {
			block_length = FLASH_READ_WINDOW;
		}
		words = (block_length + 3u) / 4u;
		REG32(FLASH_COMMAND) = FLASH_READ_COMMAND | (((block_length - 1u) & 0x3FFu) << 16);
		REG32(FLASH_ADDRESS) = 0x10000000u | ((offset & 0xFFFFFu) << 8);

		for (i = 0u; i < words; ++i) {
			word = REG32(FLASH_DATA_WINDOW + (i * 4u));
			if (produced < block_length) destination[completed + produced++] = (uint8_t)word;
			if (produced < block_length) destination[completed + produced++] = (uint8_t)(word >> 8);
			if (produced < block_length) destination[completed + produced++] = (uint8_t)(word >> 16);
			if (produced < block_length) destination[completed + produced++] = (uint8_t)(word >> 24);
		}
		completed += block_length;
	}
}

static uint32_t flash_read_jedec_id(void)
{
	REG32(FLASH_COMMAND) = FLASH_JEDEC_ID_COMMAND;
	REG32(FLASH_ADDRESS) = FLASH_TRIGGER;
	return REG32(FLASH_DATA_WINDOW) & 0x00FFFFFFu;
}

static int spi_wait(uint32_t address, uint32_t mask, uint32_t value)
{
	uint32_t polls;
	for (polls = 0u; polls < SPI_POLL_LIMIT; ++polls) {
		if ((REG32(address) & mask) == value) {
			return 1;
		}
	}
	return 0;
}

static int secondary_read(uint8_t opcode, uint32_t offset, uint8_t *data, uint32_t length)
{
	static const uint32_t registers[] = {
		GPIOA_AF_SELECT, GPIOA_AF_SELECT_1, GPIOA_AF_SELECT_0,
		GPIOB_AF_SELECT, GPIOB_AF_SELECT + 4u, GPIOB_AF_SELECT + 8u,
		SPI_CHANNEL_CONFIG, SPI_CONFIG, SPI_CLOCK_CONFIG, SPI_BASE + 0xCu,
		SPI_INT_MASK, SPI_BASE + 0x1Cu
	};
	static const uint8_t alternatives[][3] = {
		{0,1,3}, {0,4,3}, {0,5,3}, {0,10,4}, {0,12,4},
		{1,0,1}, {1,1,1}, {1,2,1}, {1,7,4}, {1,15,3},
		{1,16,3}, {1,17,3}, {1,18,3}, {1,27,1}
	};
	uint32_t saved[sizeof(registers) / sizeof(registers[0])];
	uint32_t i, j, word, count, command_length, clocks, bit, base, option;
	uint32_t byte;
	uint32_t reset_bit;
	uint32_t initial_flags;
	uint32_t pending_bit;
	int ok = 0;
	initial_flags = REG32(SPI_INT_STATUS) & 0xFFu;
	pending_bit = REG32(0xE000E200u) & (1u << 21);
	if ((REG32(SPI_STATUS) & (SPI_BUSY | SPI_FIFO_COUNTS)) != 0u ||
		(initial_flags & ~3u) != 0u ||
		(REG32(CLOCK_RESET_CONTROL) & (1u << 12)) == 0u) {
		return SECONDARY_BUSY;
	}
	for (i = 0u; i < sizeof(registers) / sizeof(registers[0]); ++i) {
		saved[i] = REG32(registers[i]);
	}
	reset_bit = (REG32(CLOCK_RESET_CONTROL) >> 12) & 1u;
	REG32(SPI_CHANNEL_CONFIG) = 6u;
	REG32(SPI_CONFIG) = 12u;
	REG32(SPI_CLOCK_CONFIG) = 19u;
	REG32(SPI_BASE + 0xCu) = 0u;
	REG32(SPI_INT_MASK) = 0xFFu;
	REG32(SPI_BASE + 0x1Cu) = 0u;
	for (i = 0u; i < sizeof(alternatives) / sizeof(alternatives[0]); ++i) {
		base = alternatives[i][0] ? GPIOB_AF_SELECT : GPIOA_AF_SELECT;
		bit = 1u << alternatives[i][1];
		option = alternatives[i][2] - 1u;
		if ((REG32(base) & bit) != 0u &&
			((REG32(base + 4u) & bit) != 0u) == ((option & 2u) != 0u) &&
			((REG32(base + 8u) & bit) != 0u) == ((option & 1u) != 0u)) {
			REG32(base) &= ~bit;
		}
	}
	REG32(GPIOA_AF_SELECT_1) |= SECBOOT_SPI_PINS;
	REG32(GPIOA_AF_SELECT_0) = (REG32(GPIOA_AF_SELECT_0) | (1u << 9) | (1u << 11)) &
		~((1u << 2) | (1u << 3));
	REG32(GPIOA_AF_SELECT) |= SECBOOT_SPI_PINS;
	while (length != 0u) {
		command_length = opcode == 0x9Fu ? 1u : 4u;
		count = length > 28u ? 28u : length;
		clocks = command_length + count;
		REG32(SPI_CHANNEL_CONFIG) = 6u | (1u << 22);
		if (!spi_wait(SPI_CHANNEL_CONFIG, 1u << 22, 0u)) {
			goto cleanup;
		}
		for (i = 0u; i < clocks; i += 4u) {
			word = 0u;
			for (j = 0u; j < 4u && i + j < clocks; ++j) {
				bit = i + j;
				if (bit == 0u) {
					byte = opcode;
				} else if (bit < command_length) {
					byte = (offset >> ((3u - bit) * 8u)) & 0xFFu;
				} else {
					byte = 0xFFu;
				}
				word |= byte << ((3u - j) * 8u);
			}
			REG32(SPI_TX_DATA) = word;
		}
		REG32(SPI_CHANNEL_CONFIG) = (command_length * 8u << 23) | (1u << 21) | (1u << 20) |
			(1u << 19) | (clocks * 8u << 3) | 3u;
		if (!spi_wait(SPI_STATUS, SPI_BUSY | 0xFC0u, count << 6) ||
			(REG32(SPI_INT_STATUS) & 0xBCu) != 0u) {
			goto cleanup;
		}
		REG32(SPI_CHANNEL_CONFIG) = 6u;
		for (i = 0u; i < count; ++i) {
			if ((i & 3u) == 0u) {
				word = REG32(SPI_RX_DATA);
			}
			*data++ = (uint8_t)(word >> ((3u - (i & 3u)) * 8u));
		}
		offset += count;
		length -= count;
	}
	ok = 1;
cleanup:
	REG32(SPI_CHANNEL_CONFIG) = 6u;
	if (!spi_wait(SPI_STATUS, SPI_BUSY, 0u)) {
		bitband_write(CLOCK_RESET_CONTROL, 12u, 0u);
		bitband_write(CLOCK_RESET_CONTROL, 12u, 1u);
	}
	if (!spi_wait(SPI_STATUS, SPI_BUSY, 0u)) {
		return SECONDARY_CLEANUP_FAILED;
	}
	REG32(SPI_CHANNEL_CONFIG) = 6u | (1u << 22);
	if (!spi_wait(SPI_CHANNEL_CONFIG, 1u << 22, 0u)) {
		bitband_write(CLOCK_RESET_CONTROL, 12u, 0u);
		bitband_write(CLOCK_RESET_CONTROL, 12u, 1u);
		if (!spi_wait(SPI_STATUS, SPI_BUSY | SPI_FIFO_COUNTS, 0u)) {
			return SECONDARY_CLEANUP_FAILED;
		}
	}
	REG32(SPI_INT_STATUS) = REG32(SPI_INT_STATUS) & ~initial_flags & 0xFFu;
	if (pending_bit != 0u) {
		REG32(0xE000E200u) = 1u << 21;
	} else {
		REG32(NVIC_CLEAR_PENDING_0) = 1u << 21;
	}
	for (i = sizeof(registers) / sizeof(registers[0]); i != 0u; --i) {
		if (i == 6u) {
			REG32(GPIOA_AF_SELECT) &= ~SECBOOT_SPI_PINS;
		}
		REG32(registers[i - 1u]) = saved[i - 1u];
	}
	bitband_write(CLOCK_RESET_CONTROL, 12u, reset_bit);
	return ok ? SECONDARY_SUPPORTED : SECONDARY_TIMEOUT;
}

static void secondary_probe(void)
{
	uint8_t id[3], check[3];
	uint32_t i;
	secondary_size = 0u;
	secondary_id = 0u;
	secondary_initial_state[0] = REG32(SPI_STATUS);
	secondary_initial_state[1] = REG32(SPI_INT_STATUS);
	secondary_initial_state[2] = REG32(0xE000E200u) & (1u << 21);
	secondary_initial_state[3] = REG32(CLOCK_RESET_CONTROL);
	secondary_state = (uint32_t)secondary_read(0x9Fu, 0u, id, 3u);
	if (secondary_state != SECONDARY_SUPPORTED) {
		return;
	}
	secondary_state = (uint32_t)secondary_read(0x9Fu, 0u, check, 3u);
	if (secondary_state != SECONDARY_SUPPORTED) {
		return;
	}
	for (i = 0u; i < 3u; ++i) {
		secondary_id |= (uint32_t)id[i] << (i * 8u);
		if (id[i] != check[i]) {
			secondary_state = SECONDARY_UNSTABLE;
			return;
		}
	}
	if (secondary_id == 0u || secondary_id == 0xFFFFFFu) {
		secondary_state = SECONDARY_NO_RESPONSE;
	} else if (id[0] == 0u || id[0] == 0xFFu || id[2] != 0x14u) {
		secondary_state = SECONDARY_UNSUPPORTED;
	} else {
		secondary_size = FLASH_SIZE;
	}
}

static void send_response_with_crc(uint8_t status, uint16_t sequence, uint32_t address,
	const uint8_t *data, uint16_t length, uint32_t data_crc)
{
    uint8_t header[RESPONSE_SIZE];
    header[0] = 'W'; header[1] = '6'; header[2] = 'R'; header[3] = 'S';
    header[4] = PROTOCOL_VERSION;
    header[5] = status;
    store16(header + 6, sequence);
    store32(header + 8, address);
    store16(header + 12, length);
    store32(header + 14, data_crc);
    store16(header + 18, crc16_ccitt_false(header, 18u));

    uart_write(header, RESPONSE_SIZE);
    if (length != 0u) {
        uart_write(data, length);
    }
}

static void send_response(uint8_t status, uint16_t sequence, uint32_t address,
	const uint8_t *data, uint16_t length)
{
	send_response_with_crc(status, sequence, address, data, length,
		crc32_ieee(data, length));
}

static void return_to_rom(void)
{
    /* The reader executes entirely from SRAM. Arm a reset, erase only the
       temporary secboot sector, and leave the immutable Mask ROM in control. */
    while ((REG32(UART0_FIFO_STATUS) & UART_TX_COUNT) != 0u) {
    }

    REG32(WATCHDOG_LOCK) = 0x1ACCE551u;
    REG32(WATCHDOG_LOAD) = 40000000u;
    REG32(WATCHDOG_CONTROL) = 3u;
    REG32(WATCHDOG_LOCK) = 1u;

    REG32(FLASH_COMMAND) = 0x00000006u;
    REG32(FLASH_ADDRESS) = 0x10000000u;
    REG32(FLASH_COMMAND) = 0x80000820u;
    REG32(FLASH_ADDRESS) = 0x10200000u;

    for (;;) {
    }
}

void loader_main(uint32_t baud_register)
{
    uint32_t address;
    uint32_t end;
	uint16_t length;
	uint16_t sequence;
	uint8_t opcode;
	uint32_t data_crc;

	REG32(WATCHDOG_CONTROL) = 0u;
	REG32(WATCHDOG_INT_CLR) = 1u;
	uart_init(baud_register);
	primary_id = flash_read_jedec_id();
	secondary_probe();
    for (;;) {
        receive_request();
        sequence = load16(request + 6);
        address = load32(request + 8);
        length = load16(request + 12);

        if (crc16_ccitt_false(request, 14u) != load16(request + 14)) {
            send_response(STATUS_BAD_HEADER, sequence, address, payload, 0u);
            continue;
        }
        if (request[4] != PROTOCOL_VERSION) {
            send_response(STATUS_BAD_VERSION, sequence, address, payload, 0u);
            continue;
        }

        opcode = request[5];
		if (opcode == OP_INFO) {
            store32(payload + 0, FLASH_BASE);
            store32(payload + 4, FLASH_SIZE + secondary_size);
            store16(payload + 8, MAX_READ_LENGTH);
            store16(payload + 10, 2u);
            store32(payload + 12, 0x00020000u);
			store32(payload + 16, primary_id);
			store32(payload + 20, secondary_id);
			store32(payload + 24, FLASH_SIZE);
			store32(payload + 28, secondary_size);
			store32(payload + 32, secondary_state);
			store32(payload + 36, secondary_initial_state[0]);
			store32(payload + 40, secondary_initial_state[1]);
			store32(payload + 44, secondary_initial_state[2]);
			store32(payload + 48, secondary_initial_state[3]);
			send_response(STATUS_OK, sequence, FLASH_BASE, payload, 52u);
			continue;
		}
		if (opcode == OP_FLASH_ID) {
			if (address != 0u || length != 0u) {
				send_response(STATUS_BAD_RANGE, sequence, address, payload, 0u);
				continue;
			}
			store32(payload, flash_read_jedec_id());
			send_response(STATUS_OK, sequence, 0u, payload, 4u);
			continue;
		}
        if (opcode == OP_RETURN_TO_ROM) {
			if (secondary_state == SECONDARY_CLEANUP_FAILED) {
				send_response(STATUS_SPI_CLEANUP_FAILED, sequence, address, payload, 0u);
				continue;
			}
            if (address != (FLASH_BASE + 0x2000u) || length != 0u) {
                send_response(STATUS_BAD_RANGE, sequence, address, payload, 0u);
                continue;
            }
            send_response(STATUS_OK, sequence, address, payload, 0u);
            return_to_rom();
        }
        if (opcode != OP_READ) {
            send_response(STATUS_BAD_OPCODE, sequence, address, payload, 0u);
            continue;
        }

        end = FLASH_BASE + FLASH_SIZE + secondary_size;
        if (length == 0u || length > MAX_READ_LENGTH ||
            address < FLASH_BASE || address >= end ||
            (uint32_t)length > (end - address)) {
            send_response(STATUS_BAD_RANGE, sequence, address, payload, 0u);
            continue;
        }

		if (secondary_state == SECONDARY_CLEANUP_FAILED) {
			send_response(STATUS_SPI_CLEANUP_FAILED, sequence, address, payload, 0u);
			continue;
		}
		{
			uint32_t first_length = address < FLASH_BASE + FLASH_SIZE
				? FLASH_BASE + FLASH_SIZE - address : 0u;
			if (first_length > length) {
				first_length = length;
			}
			if (first_length != 0u) {
				flash_read(address, payload, first_length);
			}
			if (first_length < length) {
				uint32_t status = (uint32_t)secondary_read(0x03u,
					address + first_length - FLASH_BASE - FLASH_SIZE,
					payload + first_length, length - first_length);
				if (status != SECONDARY_SUPPORTED) {
					if (status == SECONDARY_CLEANUP_FAILED) {
						secondary_state = status;
					}
					send_response(status == SECONDARY_CLEANUP_FAILED
						? STATUS_SPI_CLEANUP_FAILED : STATUS_SPI_FAILURE,
						sequence, address, payload, 0u);
					continue;
				}
			}
		}
		data_crc = crc32_ieee(payload, length);
		send_response_with_crc(STATUS_OK, sequence, address, payload, length, data_crc);
    }
}
