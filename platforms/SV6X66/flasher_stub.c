/* Framing and raw XMODEM read sender adapted from src/flasher_stub.c.
 * SV6X66 owns command policy and the retained V3 restore lifecycle here. */
#include "../hal_generic.h"

__attribute__((aligned(4)))
uint8_t cmd_buf[BUF_SIZE];
__attribute__((aligned(4)))
static uint8_t xmodem_packet[1 + 3 + 1024 + 2];
uint32_t flash_id;
uint32_t flash_size;

static uint8_t obk_crc8_sum(const uint8_t* data, uint32_t len)
{
	uint32_t sum = 0U;
	while(len--) sum += *data++;
	return (uint8_t)sum;
}

uint32_t flash_size_from_jedec(uint32_t id)
{
	uint8_t density = (uint8_t)((id >> 16) & 0xFFU);
	if(density > 0x30U && density < 0x3CU) density -= 0x30;
	if(density < 0x13U || density > 0x1CU)
	{
		return 0U;
	}
	return 1UL << density;
}

static void obk_ack(uint8_t type, uint8_t status)
{
	uint8_t a[6];
	a[0] = OBK_STUB_ACK_MAGIC;
	a[1] = type;
	a[2] = 0;
	a[3] = 0;
	a[4] = status;
	a[5] = obk_crc8_sum(a, 5);
	uart_write(a, 6);
}

static void obk_data_reply(uint8_t type, const uint8_t* data, uint16_t len, uint8_t status)
{
	uint8_t h[4];
	h[0] = OBK_STUB_ACK_MAGIC;
	h[1] = type;
	h[2] = (uint8_t)(len & 0xFFU);
	h[3] = (uint8_t)((len >> 8) & 0xFFU);
	uart_write(h, 4);
	if(len && data) uart_write(data, len);
	uart_putc(status);
	uint8_t sum = obk_crc8_sum(h, 4) + obk_crc8_sum(data, len) + status;
	uart_putc(sum);
}

static uint8_t sv6x66_application_mode;
#include "sv6x66_restore_transaction.h"
#include "sv6x66_app_transaction.h"

static void sv6x66_send_flash_crc32(uint8_t type, uint32_t off, uint32_t len)
{
	if(!range_does_not_wrap(off, len) || !flash_size ||
		off > flash_size || len > (flash_size - off))
	{
		obk_ack(type, OBK_STATUS_ADDR_ERROR);
		return;
	}
	uint32_t crc;
	if(!crc32_memory(FLASH_BASE + off, len, &crc))
	{
		obk_ack(type, OBK_STATUS_ERROR);
		return;
	}
	uint8_t payload[4];
	payload[0] = (uint8_t)(crc & 0xFFU);
	payload[1] = (uint8_t)((crc >> 8) & 0xFFU);
	payload[2] = (uint8_t)((crc >> 16) & 0xFFU);
	payload[3] = (uint8_t)((crc >> 24) & 0xFFU);
	obk_data_reply(type, payload, sizeof(payload), OBK_STATUS_SUCCESS);
}

static void xmodem_send_memory(uint32_t addr, uint32_t len)
{
	uint8_t resp;
	int use_crc = 1;
	int use_1k = 1;
	uint8_t block = 1;

	/* Offset the TX frame so its payload is 32-bit aligned; RX data uses index 0. */
	uint8_t* packet = &xmodem_packet[1];
	/* Wait for receiver's C or NAK. */
	int receiver_ready = 0;
	for(uint32_t tries = 0U; tries < 100U; tries++)
	{
		if(uart_getc_timeout(&resp, XMODEM_WAIT_LOOPS / 50U))
		{
			if(resp == CRC_MODE) { use_crc = 1; use_1k = 1; receiver_ready = 1; break; }
			if(resp == NAK) { use_crc = 0; use_1k = 0; receiver_ready = 1; break; }
			if(resp == CAN) return;
		}
	}
	if(!receiver_ready) { uart_putc(CAN); uart_putc(CAN); return; }

	uint32_t off = 0U;
	while(off < len)
	{
		WATCHDOG_REFRESH();
		uint32_t block_size = use_1k ? 1024U : 128U;
		uint32_t chunk = (len - off > block_size) ? block_size : (len - off);
		packet[0] = use_1k ? STX : SOH;
		packet[1] = block;
		packet[2] = (uint8_t)~block;
		if(addr + off < FLASH_BASE || !stub_flash_read_checked(&packet[3], addr + off - FLASH_BASE, chunk))
		{
			uart_putc(CAN);
			uart_putc(CAN);
			return;
		}
		memset(&packet[3 + chunk], 0xFF, block_size - chunk);
		uint32_t pkt_len = 3U + block_size;
		if(use_crc)
		{
			uint16_t crc = crc16_xmodem(&packet[3], block_size);
			packet[pkt_len++] = (uint8_t)(crc >> 8);
			packet[pkt_len++] = (uint8_t)(crc & 0xFFU);
		}
		else
		{
			packet[pkt_len++] = obk_crc8_sum(&packet[3], block_size);
		}

		int sent = 0;
		for(uint32_t retry = 0U; retry < XMODEM_MAX_RETRIES; retry++)
		{
			uart_write(packet, pkt_len);
			if(uart_getc_timeout(&resp, XMODEM_RESPONSE_WAIT_LOOPS))
			{
				if(resp == ACK) { sent = 1; break; }
				if(resp == CAN) return;
			}
		}
		if(!sent) { uart_putc(CAN); uart_putc(CAN); return; }
		off += chunk;
		block++;
	}

	for(int retry = 0; retry < 10; retry++)
	{
		uart_putc(EOT);
		if(uart_getc_timeout(&resp, XMODEM_RESPONSE_WAIT_LOOPS))
		{
			if(resp == ACK) return;
		}
	}
	uart_putc(CAN); uart_putc(CAN);
}

static void sv6x66_handle_command(uint8_t type, uint16_t data_len)
{
	switch(type)
	{
		case OBK_CMD_SYNC:
		{
			if(data_len != 0U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			obk_ack(type, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_FLASH_XMODEM_UL:
		{
			if(data_len != 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(flash_size != STUB_READ_ONLY_FLASH_BYTES ||
				off > flash_size || len == 0U || len > flash_size - off)
			{
				obk_ack(type, OBK_STATUS_ADDR_ERROR);
				return;
			}
			obk_ack(type, OBK_STATUS_SUCCESS);
			xmodem_send_memory(FLASH_BASE + off, len);
			break;
		}
		case OBK_CMD_FLASH_XMODEM_DL:
			sv6x66_transaction_begin(data_len);
			break;
		case SV6X66_CMD_TRANSACTION:
			/* Unified bank-aware loaders retain the exact V3 restore schedule.
			 * Failure at any point requires SPI recovery; no UART rollback
			 * or preservation of the currently selected bank is promised. */
#if SV6X66_BANK_AWARE
			if(sv6x66_carrier_bank >= 2U) obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
			else sv6x66_transaction_control(data_len);
#else
			/* Legacy V3 restore belongs to the legacy carrier only. */
			if(sv6x66_carrier_bank < 2U) obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
			else sv6x66_transaction_control(data_len);
#endif
			break;
		case SV6X66_CMD_APPLICATION:
			sv6x66_application_control(data_len);
			break;
		case OBK_CMD_BAUD_CHANGE:
		{
			if(data_len != 4U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t baud = load_le32(cmd_buf);
			if(!stub_uart_prepare_baud(baud, cmd_buf))
			{
				obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
				return;
			}
			/* Reply at the old rate, then switch after the complete frame drains. */
			obk_data_reply(type, cmd_buf, SV6X66_BAUD_DIAGNOSTIC_SIZE,
				OBK_STATUS_SUCCESS);
			stub_uart_commit_prepared_baud();
			break;
		}
		case OBK_CMD_GET_CHIP_PRODUCT:
		{
			if(data_len != 0U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint8_t len = get_chip_data();
			obk_data_reply(type, cmd_buf, len, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_FLASH_CRC32:
		{
			if(data_len != 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(flash_size != STUB_READ_ONLY_FLASH_BYTES || off > flash_size ||
				len == 0U || len > flash_size - off)
			{
				obk_ack(type, OBK_STATUS_ADDR_ERROR);
				return;
			}
			sv6x66_send_flash_crc32(type, off, len);
			break;
		}
		case OBK_CMD_FLASH_ID:
		{
			if(data_len != 0U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			if(flash_size != STUB_READ_ONLY_FLASH_BYTES)
			{
				obk_ack(type, OBK_STATUS_ERROR);
				return;
			}
			const uint8_t jedec[] = { 0xC8U, 0x40U, 0x15U, 0x00U };
			obk_data_reply(type, jedec, sizeof(jedec), OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_AVAILABLE_CMDS:
		{
			if(data_len != 0U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint8_t cmds[] =
			{
				OBK_CMD_SYNC,
				OBK_CMD_BAUD_CHANGE,
				OBK_CMD_GET_CHIP_PRODUCT,
				OBK_CMD_FLASH_CRC32,
				OBK_CMD_FLASH_ID,
				OBK_CMD_FLASH_XMODEM_DL,
				SV6X66_CMD_TRANSACTION,
#if SV6X66_BANK_AWARE
				SV6X66_CMD_APPLICATION,
#endif
				OBK_CMD_FLASH_XMODEM_UL,
				OBK_CMD_AVAILABLE_CMDS,
			};
			/* Bank-aware loaders advertise both application and full restore. */
			obk_data_reply(type, cmds, sizeof(cmds), OBK_STATUS_SUCCESS);
			break;
		}
		default:
		{
			obk_ack(type, OBK_STATUS_TYPE_ERROR);
			break;
		}
	}
	return;
}

static void handle_obk_frame(void)
{
	uint32_t frame_budget = RX_WAIT_LOOPS;
	uint8_t type, l0, l1, crc;
	if(!uart_getc_budget(&type, &frame_budget)) return;
	if(!uart_getc_budget(&l0, &frame_budget)) return;
	if(!uart_getc_budget(&l1, &frame_budget)) return;
	uint16_t data_len = (uint16_t)l0 | ((uint16_t)l1 << 8);
	if(data_len > sizeof(cmd_buf))
	{
		obk_ack(type, OBK_STATUS_LEN_ERROR);
		return;
	}
	for(uint32_t i = 0; i < data_len; i++)
	{
		if(!uart_getc_budget(&cmd_buf[i], &frame_budget)) return;
	}
	if(!uart_getc_budget(&crc, &frame_budget)) return;

	uint8_t sum = OBK_STUB_MAGIC + type + l0 + l1 + obk_crc8_sum(cmd_buf, data_len);
	if(sum != crc)
	{
		obk_ack(type, OBK_STATUS_CRC_ERROR);
		return;
	}

	sv6x66_handle_command(type, data_len);
}

int main(void)
{
	uart_init();
	flash_init();
	flash_size = flash_size_from_jedec(flash_id);
	crc32_init_table();

	while(1)
	{
		WATCHDOG_REFRESH();
		uint8_t b;
		if(!uart_getc_timeout(&b, PROMPT_IDLE_LOOPS))
		{
			continue;
		}
		if(b == OBK_STUB_MAGIC)
		{
			handle_obk_frame();
		}
	}
	return 0;
}
