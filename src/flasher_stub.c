#include "../platforms/hal_generic.h"
#include "stub_miniz.h"

#if PLATFORM_RDA5981
__attribute__((section(".dram2")))
#endif
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

static uint32_t crc32_calc(uint32_t addr, uint32_t len)
{
	uint32_t crc = 0xFFFFFFFFU;
	if(crc32_memory(addr, len, &crc)) return crc;
	return 0;
}

static void obk_send_flash_crc32(uint8_t type, uint32_t off, uint32_t len)
{
	if(!range_does_not_wrap(off, len) || !flash_size ||
		off > flash_size || len > (flash_size - off))
	{
		obk_ack(type, OBK_STATUS_ADDR_ERROR);
		return;
	}
	uint32_t crc = crc32_calc(FLASH_BASE + off, len);
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
		stub_flash_read(&packet[3], addr + off, chunk);
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
			uint8_t sum = 0;
			for(uint32_t i = 0; i < block_size; i++) sum = (uint8_t)(sum + packet[3 + i]);
			packet[pkt_len++] = sum;
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

static int xmodem_tx_send_packet(xmodem_tx_stream_t* stream)
{
	uint8_t resp;
	uint8_t* packet = &xmodem_packet[1];
	uint32_t block_size = stream->use_1k ? 1024U : 128U;
	memset(&packet[3U + stream->data_len], 0xFF, block_size - stream->data_len);
	packet[0] = stream->use_1k ? STX : SOH;
	packet[1] = stream->block;
	packet[2] = (uint8_t)~stream->block;
	uint32_t packet_len = 3U + block_size;
	if(stream->use_crc)
	{
		uint16_t crc = crc16_xmodem(&packet[3], block_size);
		packet[packet_len++] = (uint8_t)(crc >> 8);
		packet[packet_len++] = (uint8_t)crc;
	}
	else
	{
		uint8_t sum = 0U;
		for(uint32_t i = 0U; i < block_size; i++) sum = (uint8_t)(sum + packet[3U + i]);
		packet[packet_len++] = sum;
	}
	for(uint32_t retry = 0U; retry < XMODEM_MAX_RETRIES; retry++)
	{
		WATCHDOG_REFRESH();
		uart_write(packet, packet_len);
		if(uart_getc_timeout(&resp, XMODEM_RESPONSE_WAIT_LOOPS))
		{
			if(resp == ACK)
			{
				stream->block++;
				stream->data_len = 0U;
				return 1;
			}
			if(resp == CAN) break;
		}
	}
	stream->failed = 1U;
	uart_putc(CAN);
	uart_putc(CAN);
	return 0;
}

static int xmodem_tx_start(xmodem_tx_stream_t* stream)
{
	uint8_t resp;
	stream->data_len = 0U;
	stream->block = 1U;
	stream->use_crc = 1U;
	stream->use_1k = 1U;
	stream->failed = 0U;
	for(uint32_t tries = 0U; tries < 100U; tries++)
	{
		if(uart_getc_timeout(&resp, XMODEM_WAIT_LOOPS / 50U))
		{
			if(resp == CRC_MODE) return 1;
			if(resp == NAK)
			{
				stream->use_crc = 0U;
				stream->use_1k = 0U;
				return 1;
			}
			if(resp == CAN) break;
		}
	}
	stream->failed = 1U;
	uart_putc(CAN);
	uart_putc(CAN);
	return 0;
}

static int xmodem_tx_put_buffer(void* ctx, const uint8_t* buffer, uint32_t len)
{
	xmodem_tx_stream_t* stream = (xmodem_tx_stream_t*)ctx;
	if(stream->failed) return 0;

	while(len)
	{
		WATCHDOG_REFRESH();
		uint32_t block_size = stream->use_1k ? 1024U : 128U;
		uint32_t chunk = block_size - stream->data_len;
		if(chunk > len) chunk = len;
		memcpy(&xmodem_packet[4U + stream->data_len], buffer, chunk);
		stream->data_len += chunk;
		buffer += chunk;
		len -= chunk;
		if(stream->data_len == block_size && !xmodem_tx_send_packet(stream)) return 0;
	}
	return 1;
}

static int xmodem_tx_finish(xmodem_tx_stream_t* stream)
{
	uint8_t resp;
	if(stream->failed) return 0;
	if(stream->data_len && !xmodem_tx_send_packet(stream)) return 0;
	for(uint32_t retry = 0U; retry < 10U; retry++)
	{
		uart_putc(EOT);
		if(uart_getc_timeout(&resp, XMODEM_RESPONSE_WAIT_LOOPS) && resp == ACK) return 1;
	}
	uart_putc(CAN);
	uart_putc(CAN);
	return 0;
}

static int xmodem_send_compressed_memory(uint32_t addr, uint32_t len, uint8_t level)
{
	xmodem_tx_stream_t stream;
	if(!xmodem_tx_start(&stream)) return 0;
	if(!stub_miniz_deflate_raw((const volatile uint8_t*)(uintptr_t)addr, len, level, xmodem_tx_put_buffer, &stream))
	{
		if(!stream.failed)
		{
			uart_putc(CAN);
			uart_putc(CAN);
		}
		return 0;
	}
	return xmodem_tx_finish(&stream);
}

static int xmodem_rx_next_packet(xmodem_rx_stream_t* stream)
{
	uint8_t marker;
	if(stream->pending_ack)
	{
		uart_putc(ACK);
		stream->pending_ack = 0U;
	}
	for(;;)
	{
		if(!stream->started)
		{
			uint32_t tries;
			for(tries = 0U; tries < 100U; tries++)
			{
				uart_putc(CRC_MODE);
				if(uart_getc_timeout(&marker, XMODEM_WAIT_LOOPS / 50U)) break;
			}
			if(tries == 100U) goto fail;
			stream->started = 1U;
		}
		else if(!uart_getc_timeout(&marker, XMODEM_WAIT_LOOPS))
		{
			goto fail;
		}
		if(marker == CAN || marker == EOT) goto fail;
		if(marker != SOH && marker != STX)
		{
			uart_putc(NAK);
			continue;
		}

		uint32_t block_size = marker == STX ? 1024U : 128U;
		uint8_t block_no, block_inv, crc_hi, crc_lo;
		if(!uart_getc_timeout(&block_no, XMODEM_WAIT_LOOPS) ||
			!uart_getc_timeout(&block_inv, XMODEM_WAIT_LOOPS)) goto fail;
		for(uint32_t i = 0U; i < block_size; i++)
		{
			if(!uart_getc_timeout(&xmodem_packet[i], XMODEM_WAIT_LOOPS)) goto fail;
		}
		if(!uart_getc_timeout(&crc_hi, XMODEM_WAIT_LOOPS) ||
			!uart_getc_timeout(&crc_lo, XMODEM_WAIT_LOOPS)) goto fail;
		uint16_t received_crc = ((uint16_t)crc_hi << 8) | crc_lo;
		if(block_inv != (uint8_t)~block_no ||
			received_crc != crc16_xmodem(xmodem_packet, block_size))
		{
			uart_putc(NAK);
			continue;
		}
		if(block_no == (uint8_t)(stream->expected_block - 1U))
		{
			uart_putc(ACK);
			continue;
		}
		if(block_no != stream->expected_block) goto fail;
		stream->expected_block++;
		stream->data_pos = 0U;
		stream->data_len = block_size;
		stream->pending_ack = 1U;
		return 1;
	}

fail:
	stream->failed = 1U;
	uart_putc(CAN);
	uart_putc(CAN);
	return 0;
}

static uint32_t xmodem_rx_get_buffer(void* ctx, uint8_t* buffer, uint32_t capacity)
{
	xmodem_rx_stream_t* stream = (xmodem_rx_stream_t*)ctx;
	if(stream->failed) return 0;
	if(stream->data_pos == stream->data_len && !xmodem_rx_next_packet(stream)) return 0;
	uint32_t chunk = stream->data_len - stream->data_pos;
	if(chunk > capacity) chunk = capacity;
	memcpy(buffer, &xmodem_packet[stream->data_pos], chunk);
	stream->data_pos += chunk;
	return chunk;
}

static int xmodem_rx_finish(xmodem_rx_stream_t* stream)
{
	uint8_t marker;
	if(stream->failed) return 0;
	stream->data_pos = stream->data_len;
	if(stream->pending_ack)
	{
		uart_putc(ACK);
		stream->pending_ack = 0U;
	}
	if(uart_getc_timeout(&marker, XMODEM_WAIT_LOOPS) && marker == EOT)
	{
		uart_putc(ACK);
		return 1;
	}
	uart_putc(CAN);
	uart_putc(CAN);
	return 0;
}

static int inflate_flash_flush(inflate_flash_t* flash)
{
	if(!flash->page_len) return 1;
	if(buffer_is_erased(flash->page, flash->page_len))
	{
		flash->page_len = 0U;
		return 1;
	}
	memset(&flash->page[flash->page_len], 0xFF, FLASH_PAGE_SIZE - flash->page_len);
	flash_program_page(flash->page_start, flash->page);
	flash->page_len = 0U;
	return 1;
}

static int inflate_flash_put_buffer(void* ctx, const uint8_t* buffer, uint32_t len)
{
	inflate_flash_t* flash = (inflate_flash_t*)ctx;
	if(len > flash->expected_len - flash->written) return 0;

	while(len)
	{
		if(!flash->page_len)
		{
			flash->page_start = flash->off + flash->written;
			if((flash->page_start & (FLASH_SECTOR_SIZE - 1U)) == 0U)
			{
				flash_erase_range(flash->page_start, FLASH_SECTOR_SIZE);
			}
		}

		uint32_t chunk = FLASH_PAGE_SIZE - flash->page_len;
		if(chunk > len) chunk = len;
		memcpy(&flash->page[flash->page_len], buffer, chunk);
		flash->page_len += chunk;
		flash->written += chunk;
		buffer += chunk;
		len -= chunk;
		if(flash->page_len == FLASH_PAGE_SIZE && !inflate_flash_flush(flash)) return 0;
	}
	return 1;
}

static int xmodem_receive_compressed_flash(uint32_t off, uint32_t len)
{
	xmodem_rx_stream_t stream = { 0U, 0U, 1U, 0U, 0U, 0U };
	inflate_flash_t flash;
	flash.off = off;
	flash.expected_len = len;
	flash.written = 0U;
	flash.page_start = 0U;
	flash.page_len = 0U;
	if(!stub_miniz_inflate_raw(xmodem_rx_get_buffer, &stream, inflate_flash_put_buffer, &flash, len)
		|| !inflate_flash_flush(&flash) || flash.written != len)
	{
		if(!stream.failed)
		{
			uart_putc(CAN);
			uart_putc(CAN);
		}
		return 0;
	}
	return xmodem_rx_finish(&stream);
}

static int xmodem_receive_flash(uint32_t off, uint32_t len)
{
	uint8_t expected_block = 1U;
	uint32_t written = 0U;
	uint8_t marker = 0U;

	if(!flash_erase_range(off, len))
	{
		uart_putc(CAN);
		uart_putc(CAN);
		return 0;
	}

	for(uint32_t tries = 0U; tries < 100U; tries++)
	{
		uart_putc(CRC_MODE);
		if(uart_getc_timeout(&marker, XMODEM_WAIT_LOOPS / 50U)) break;
	}
	if(marker != SOH && marker != STX)
	{
		uart_putc(CAN);
		uart_putc(CAN);
		return 0;
	}

	while(1)
	{
		if(marker == EOT)
		{
			if(written == len)
			{
				uart_putc(ACK);
				return 1;
			}
			uart_putc(CAN);
			uart_putc(CAN);
			return 0;
		}
		if(marker == CAN) return 0;
		if(marker != SOH && marker != STX)
		{
			uart_putc(NAK);
		}
		else
		{
			uint32_t block_size = marker == STX ? 1024U : 128U;
			uint8_t block_no, block_inv, crc_hi, crc_lo;
			if(!uart_getc_timeout(&block_no, XMODEM_WAIT_LOOPS) ||
				!uart_getc_timeout(&block_inv, XMODEM_WAIT_LOOPS)) return 0;
			for(uint32_t i = 0U; i < block_size; i++)
			{
				if(!uart_getc_timeout(&xmodem_packet[i], XMODEM_WAIT_LOOPS)) return 0;
			}
			if(!uart_getc_timeout(&crc_hi, XMODEM_WAIT_LOOPS) ||
				!uart_getc_timeout(&crc_lo, XMODEM_WAIT_LOOPS)) return 0;
			uint16_t received_crc = ((uint16_t)crc_hi << 8) | crc_lo;
			uint16_t calculated_crc = crc16_xmodem(xmodem_packet, block_size);
			if(block_inv != (uint8_t)~block_no || received_crc != calculated_crc)
			{
				uart_putc(NAK);
			}
			else if(block_no == (uint8_t)(expected_block - 1U))
			{
				uart_putc(ACK);
			}
			else if(block_no != expected_block)
			{
				uart_putc(CAN);
				uart_putc(CAN);
				return 0;
			}
			else
			{
				if(written >= len)
				{
					uart_putc(CAN);
					uart_putc(CAN);
					return 0;
				}
				uint32_t chunk = (len - written > block_size) ? block_size : (len - written);
				uint32_t page_off = 0U;
				while(page_off < chunk)
				{
					uint32_t current_off = off + written + page_off;
					uint32_t page_len = chunk - page_off;
					if(page_len > FLASH_PAGE_SIZE) page_len = FLASH_PAGE_SIZE;
					memset(&xmodem_packet[page_off + page_len], 0xFF,
						FLASH_PAGE_SIZE - page_len);
					if(!buffer_is_erased(&xmodem_packet[page_off], page_len))
					{
						flash_program_page(current_off, &xmodem_packet[page_off]);
					}
					page_off += page_len;
				}
				written += chunk;
				expected_block++;
				uart_putc(ACK);
			}
		}
		if(!uart_getc_timeout(&marker, XMODEM_WAIT_LOOPS)) return 0;
	}
}

static void packet_receive_handle(uint8_t type, uint32_t off, uint32_t len)
{
	uint32_t written = 0;
	uint32_t remaining = len;
	uint32_t last_frame_len = 0;

	uint8_t page_buf[FLASH_PAGE_SIZE];
	uint32_t page_off = 0;

	while(remaining > 0)
	{
		uint8_t hdr[4];
		for(int i = 0; i < 4; ++i) uart_getc_inf(&hdr[i]);

		if(hdr[0] != OBK_STUB_MAGIC || hdr[1] != type)
		{
			obk_ack(type, OBK_STATUS_ERROR);
			return;
		}

		uint16_t frame_len = hdr[2] | (hdr[3] << 8);

		if(frame_len > BUF_SIZE)
		{
			obk_ack(type, OBK_STATUS_LEN_ERROR);
			return;
		}

		for(uint32_t i = 0; i < frame_len; ++i) uart_getc_inf(&cmd_buf[i]);

		uint8_t crc_recv;
		uart_getc_inf(&crc_recv);

		uint8_t sum = obk_crc8_sum((uint8_t*)&hdr, 4) + obk_crc8_sum((uint8_t*)&cmd_buf, frame_len);

		if(sum != crc_recv)
		{
			obk_ack(type, OBK_STATUS_CRC_ERROR);
			return;
		}

		uint32_t data_len = frame_len - 1;

		if(cmd_buf[0] == 0x01)
		{
			remaining += last_frame_len;
			written -= last_frame_len;
		}
		last_frame_len = data_len;

		const uint8_t* p = &cmd_buf[1];

		if(type == OBK_CMD_FLASH_DOWNLOAD)
		{
			while(data_len)
			{
				uint32_t chunk = FLASH_PAGE_SIZE - page_off;
				if(chunk > data_len) chunk = data_len;

				memcpy(&page_buf[page_off], p, chunk);
				page_off += chunk;
				p += chunk;
				data_len -= chunk;

				if(page_off == FLASH_PAGE_SIZE)
				{
					flash_program_page(off + written - page_off, page_buf);
					page_off = 0;
				}
			}
		}
		else
		{
			memcpy((void*)(off + written), p, data_len);
		}

		written += frame_len - 1;
		remaining -= frame_len - 1;

		obk_ack(type, OBK_STATUS_SUCCESS);
	}

	if(page_off && type == OBK_CMD_FLASH_DOWNLOAD)
	{
		memset(&page_buf[page_off], 0xFF, FLASH_PAGE_SIZE - page_off);
		flash_program_page(off + written - page_off, page_buf);
	}
}

static void handle_obk_frame(void)
{
	uint8_t type, l0, l1, crc;
	if(!uart_getc_timeout(&type, RX_WAIT_LOOPS)) return;
	if(!uart_getc_timeout(&l0, RX_WAIT_LOOPS)) return;
	if(!uart_getc_timeout(&l1, RX_WAIT_LOOPS)) return;
	uint16_t data_len = (uint16_t)l0 | ((uint16_t)l1 << 8);
	if(data_len > sizeof(cmd_buf))
	{
		obk_ack(type, OBK_STATUS_LEN_ERROR);
		return;
	}
	for(uint32_t i = 0; i < data_len; i++)
	{
		if(!uart_getc_timeout(&cmd_buf[i], RX_WAIT_LOOPS)) return;
	}
	if(!uart_getc_timeout(&crc, RX_WAIT_LOOPS)) return;

	uint8_t sum = OBK_STUB_MAGIC + type + l0 + l1;
	for(uint32_t i = 0; i < data_len; i++) sum = (uint8_t)(sum + cmd_buf[i]);
	if(sum != crc)
	{
		obk_ack(type, OBK_STATUS_CRC_ERROR);
		return;
	}

	switch(type)
	{
		case OBK_CMD_SYNC:
		{
			obk_ack(type, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_RAM_DOWNLOAD:
		{
			if(data_len != 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			// no bounds check to make it possible to write to periphs
			obk_ack(type, OBK_STATUS_SUCCESS);
			packet_receive_handle(type, off, len);
			break;
		}
		case OBK_CMD_FLASH_DOWNLOAD:
		{
			if(data_len != 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			if(!flash_erase_range(off, len))
			{
				obk_ack(type, OBK_STATUS_ERROR);
				return;
			}
			obk_ack(type, OBK_STATUS_SUCCESS);
			packet_receive_handle(type, off, len);
			break;
		}
		case OBK_CMD_FLASH_UPLOAD:
		{
			if(data_len != 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			uint32_t tempLen = 0;
			do
			{
				tempLen = MIN(MIN(BUF_SIZE, 1024), len);
				stub_flash_read(cmd_buf, off, tempLen);
				obk_data_reply(type, cmd_buf, tempLen, OBK_STATUS_SUCCESS);
				len -= tempLen;
				off += tempLen;
			} while(len);
			break;
		}
		case OBK_CMD_BAUD_CHANGE:
		{
			if(data_len != 4U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t baud = load_le32(cmd_buf);
			// keep it compatible with original stub
			uart_set_baud(baud);
			delay_loops(BAUD_DELAY);
			obk_ack(type, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_RUN:
		{
			obk_ack(type, OBK_STATUS_SUCCESS);
			delay_loops(300000);
			boot_from_flash();
			break;
		}
		case OBK_CMD_FLASH_ID:
		{
#ifndef PLATFORM_GD32VW553
			// LE will work fine, no BE devices yet
			obk_data_reply(type, (uint8_t*)&flash_id, sizeof(flash_id), OBK_STATUS_SUCCESS);
#else
			obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
#endif
			break;
		}
		case OBK_CMD_FLASH_CRC32:
		{
			if(data_len != 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			obk_send_flash_crc32(type, off, len);
			break;
		}
		case OBK_CMD_FLASH_SHA256:
		{
			if(data_len != 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			if(!sha256_memory_hardware(FLASH_BASE + off, len))
			{
				obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
				return;
			}
			obk_data_reply(type, cmd_buf, 32, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_GET_MAC:
		{
			uint8_t mac[6];
			if(!read_factory_mac(mac))
			{
				obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
				return;
			}
			obk_data_reply(type, mac, sizeof(mac), OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_FLASH_ERASE:
		{
			if(data_len < 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			if(!flash_erase_range(off, len))
			{
				obk_ack(type, OBK_STATUS_ERROR);
				return;
			}
			obk_ack(type, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_FLASH_XMODEM_DL:
		{
			if(data_len < 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			obk_ack(type, OBK_STATUS_SUCCESS);
			xmodem_receive_flash(off, len);
			break;
		}
		case OBK_CMD_FLASH_XMODEM_UL:
		{
			if(data_len < 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			obk_ack(type, OBK_STATUS_SUCCESS);
			xmodem_send_memory(FLASH_BASE + off, len);
			break;
		}
		case OBK_CMD_FLASH_XMODEM_UL_Z:
		{
#ifndef NO_MINIZ_COMPRESSION
			if(data_len < 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			uint8_t level = data_len >= 9U ? cmd_buf[8] : 5U;
			obk_ack(type, OBK_STATUS_SUCCESS);
			xmodem_send_compressed_memory(FLASH_BASE + off, len, level);
#else
			obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
#endif
			break;
		}
		case OBK_CMD_FLASH_XMODEM_DL_Z:
		{
#ifndef NO_MINIZ_DECOMPRESSION
			if(data_len < 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t off = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			if(off + len > flash_size) { obk_ack(type, OBK_STATUS_ADDR_ERROR); return; }
			obk_ack(type, OBK_STATUS_SUCCESS);
			xmodem_receive_compressed_flash(off, len);
#else
			obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
#endif
			break;
		}
		case OBK_CMD_RAW_XMODEM_UL:
		{
			if(data_len < 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t addr = load_le32(cmd_buf);
			uint32_t len = load_le32(cmd_buf + 4);
			obk_ack(type, OBK_STATUS_SUCCESS);
			xmodem_send_memory(addr, len);
			break;
		}
		case OBK_CMD_FLASH_CHIP_ERASE:
		{
			if(!flash_size)
			{
				obk_ack(type, OBK_STATUS_ERROR);
				return;
			}
			if(!flash_erase_chip())
			{
				obk_ack(type, OBK_STATUS_ERROR);
				return;
			}
			obk_ack(type, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_READ_EFUSE:
		{
			int len = read_efuse();
			if(!len)
			{
				obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
				return;
			}
			obk_data_reply(type, cmd_buf, len, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_READ_OTP:
		{
			int len = read_otp();
			if(len == 0)
			{
				obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
				return;
			}
			else if(len < 0)
			{
				obk_ack(type, OBK_STATUS_OTP_ERROR);
				return;
			}
			obk_data_reply(type, cmd_buf, len, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_GET_CHIP_PRODUCT:
		{
			uint8_t len = get_chip_data();
			obk_data_reply(type, cmd_buf, len, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_READ_REG:
		{
			if(data_len < 4U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t addr = load_le32(cmd_buf);
			uint32_t data = READ_REG32(addr);
			obk_data_reply(type, (uint8_t*)&data, 4, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_WRITE_REG:
		{
			if(data_len < 8U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			uint32_t addr = load_le32(cmd_buf);
			uint32_t data = load_le32(cmd_buf + 4);
			WRITE_REG32(addr, data);
			obk_ack(type, OBK_STATUS_SUCCESS);
			break;
		}
		case OBK_CMD_KV_GET:
		{
#if (PLATFORM_RTL8721DA || PLATFORM_RTL8720E) && !defined(DISABLE_KV)
			void stub_kv_init(void);
			stub_kv_init();
			char kvname[255] = { 0 };
			strncpy((char*)&kvname, (const char*)cmd_buf, data_len);
			int kvsize = rt_kv_size(kvname);
			if(kvsize <= 0)
			{
				obk_ack(type, OBK_STATUS_ERROR);
				return;
			}
			rt_kv_get(kvname, &cmd_buf, kvsize);
			obk_data_reply(type, cmd_buf, kvsize, OBK_STATUS_SUCCESS);
#else
			obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
#endif
			break;
		}
		case OBK_CMD_KV_SET:
		{
#if (PLATFORM_RTL8721DA || PLATFORM_RTL8720E) && !defined(DISABLE_KV)
			void stub_kv_init(void);
			stub_kv_init();
			unsigned char namelen = cmd_buf[0];
			char kvname[255] = { 0 };
			strncpy((char*)&kvname, (const char*)cmd_buf + 3, namelen);
			uint16_t datasize = load_le16(cmd_buf + 1);
			rt_kv_set(kvname, cmd_buf + 3 + namelen, datasize);
			obk_ack(type, OBK_STATUS_SUCCESS);
#else
			obk_ack(type, OBK_STATUS_NOT_SUPPORTED);
#endif
			break;
		}
		case OBK_CMD_JUMP:
		{
			if(data_len < 4U) { obk_ack(type, OBK_STATUS_LEN_ERROR); return; }
			obk_ack(type, OBK_STATUS_SUCCESS);
			void (*entry_point)(void) = (void (*)(void))(load_le32(cmd_buf)); // no check
			entry_point();
			break;
		}
#if 0
		case OBK_CMD_AVAILABLE_CMDS:
		{
			uint8_t cmds[] =
			{
				OBK_CMD_SYNC,
				OBK_CMD_RAM_DOWNLOAD,
				OBK_CMD_FLASH_DOWNLOAD,
				OBK_CMD_FLASH_UPLOAD,
				OBK_CMD_FLASH_ERASE,
				OBK_CMD_FLASH_CHIP_ERASE,
#ifdef PLATFORM_SUPPORTS_REBOOT
				OBK_CMD_RUN,
#endif
				OBK_CMD_BAUD_CHANGE,
#ifdef PLATFORM_SUPPORTS_SHA256
				OBK_CMD_FLASH_SHA256,
#endif
				OBK_CMD_GET_CHIP_PRODUCT,
				OBK_CMD_READ_REG,
				OBK_CMD_WRITE_REG,
				OBK_CMD_FLASH_CRC32,
#ifndef PLATFORM_GD32VW553
				OBK_CMD_FLASH_ID,
#endif
				OBK_CMD_FLASH_XMODEM_DL,
				OBK_CMD_FLASH_XMODEM_UL,
#if (PLATFORM_RTL8721DA || PLATFORM_RTL8720E) && !defined(DISABLE_KV)
				OBK_CMD_KV_GET,
				OBK_CMD_KV_SET,
#endif
#if PLATFORM_SUPPORTS_MAC_READ
				OBK_CMD_GET_MAC,
#endif
#ifndef NO_MINIZ_COMPRESSION
				OBK_CMD_FLASH_XMODEM_UL_Z,
#endif
#ifndef NO_MINIZ_DECOMPRESSION
				OBK_CMD_FLASH_XMODEM_DL_Z,
#endif
				OBK_CMD_RAW_XMODEM_UL,
#ifdef PLATFORM_SUPPORTS_EFUSE_READ
				OBK_CMD_READ_EFUSE,
#endif
#ifdef PLATFORM_SUPPORTS_OTP_READ
				OBK_CMD_READ_OTP,
#endif
				OBK_CMD_JUMP,
				OBK_CMD_AVAILABLE_CMDS,
			};
			obk_data_reply(type, cmds, sizeof(cmds), OBK_STATUS_SUCCESS);
			break;
		}
#endif
		default:
		{
			obk_ack(type, OBK_STATUS_TYPE_ERROR);
			break;
		}
	}
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
