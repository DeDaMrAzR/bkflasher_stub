static uint8_t sv6x66_restore_status;

/* Included by the SV6X66 platform engine. Wire integers are little endian. */
#define SV6X66_CMD_TRANSACTION 0x9DU
#define SV6X66_TRANSACTION_VERSION 3U
#define SV6X66_TRANSACTION_REPLY_BYTES 84U
#define SV6X66_STATUS_CRC_MISMATCH 0x14U
#define SV6X66_STATUS_CANCELLED 0x15U
#define SV6X66_ERASE_STARTED 1U
#define SV6X66_RANGE_VERIFIED 2U

static uint8_t sv6x66_session[8];
static uint8_t sv6x66_source_sha[32];
static uint8_t sv6x66_session_set;
static uint8_t sv6x66_transaction_state;
static uint8_t sv6x66_region = 0xFFU;
static uint32_t sv6x66_attempt;
static uint32_t sv6x66_offset;
static uint32_t sv6x66_length;
static uint32_t sv6x66_expected_crc;
static uint32_t sv6x66_programmed;
static uint32_t sv6x66_acked;
static uint32_t sv6x66_detail;
static uint32_t sv6x66_range_crc;
static uint32_t sv6x66_erased;
static uint32_t sv6x66_flags;

static const uint32_t sv6x66_region_start[8] = {
	0x141000U, 0x0A6000U, 0x00C000U, 0x00B000U,
	0x005000U, 0x001000U, 0x000000U, 0x004000U
};
static const uint32_t sv6x66_region_length[8] = {
	0x0BF000U, 0x09B000U, 0x09A000U, 0x001000U,
	0x006000U, 0x003000U, 0x001000U, 0x001000U
};

static void sv6x66_store32(uint8_t* out, uint32_t value)
{
	for(uint32_t i = 0U; i < 4U; i++) out[i] = (uint8_t)(value >> (8U * i));
}

static int sv6x66_equal(const uint8_t* a, const uint8_t* b, uint32_t len)
{
	while(len--) if(*a++ != *b++) return 0;
	return 1;
}

static int sv6x66_session_matches(const uint8_t* nonce)
{
	return sv6x66_session_set && sv6x66_equal(nonce, sv6x66_session, 8U);
}

static void sv6x66_transaction_reply(uint8_t type)
{
	uint8_t reply[SV6X66_TRANSACTION_REPLY_BYTES] = { 0U };
	reply[0] = SV6X66_TRANSACTION_VERSION;
	reply[1] = sv6x66_transaction_state;
	reply[2] = sv6x66_restore_status;
	reply[3] = sv6x66_region;
	memcpy(reply + 4, sv6x66_session, 8U);
	memcpy(reply + 12, sv6x66_source_sha, 32U);
	sv6x66_store32(reply + 44, sv6x66_attempt);
	sv6x66_store32(reply + 48, sv6x66_offset);
	sv6x66_store32(reply + 52, sv6x66_length);
	sv6x66_store32(reply + 56, sv6x66_expected_crc);
	sv6x66_store32(reply + 60, sv6x66_programmed);
	sv6x66_store32(reply + 64, sv6x66_acked);
	sv6x66_store32(reply + 68, sv6x66_range_crc);
	sv6x66_store32(reply + 72, sv6x66_detail);
	sv6x66_store32(reply + 76, sv6x66_erased);
	sv6x66_store32(reply + 80, sv6x66_flags);
	obk_data_reply(type, reply, sizeof(reply), OBK_STATUS_SUCCESS);
}

static void sv6x66_receive_region(void)
{
	uint8_t marker = 0U;
	uint32_t retries = 0U;
	uint8_t status = OBK_STATUS_ERROR;
	uint32_t detail = SV6X66_TARGET_DETAIL_ERASE;
	/* ECR-style preparation erases only this semantic range before the first C.
	 * Any interruption leaves the current range unsafe until its final flash CRC. */
	sv6x66_flags = SV6X66_ERASE_STARTED;
	if(sv6x66_application_mode) {
		/* Application installation has exactly 155 separate sector commits;
		 * the legacy full-restore erase selection below remains unchanged. */
		while(sv6x66_erased < sv6x66_length) {
			uint32_t completed;
			if(!stub_flash_erase_range_progress(sv6x66_offset + sv6x66_erased,
				FLASH_SECTOR_SIZE, &completed)) goto failed;
			sv6x66_erased += completed;
		}
	}
	else if(!stub_flash_erase_range_progress(sv6x66_offset, sv6x66_length, &sv6x66_erased)) goto failed;
	status = SV6X66_STATUS_TRANSFER_ABORTED_RETRYABLE;
	detail = SV6X66_TRANSFER_DETAIL_START_HANDSHAKE;
	for(uint32_t tries = 0U; tries < 100U; tries++)
	{
		uart_putc(CRC_MODE);
		if(uart_getc_timeout(&marker, XMODEM_WAIT_LOOPS / 50U)) break;
	}
	if(marker == 0U) goto failed;
	for(;;)
	{
		if(marker == CAN)
		{
			/* A raw stream abort never claims a safely completed 4 KiB boundary. */
			status = SV6X66_STATUS_CANCELLED;
			detail = SV6X66_TRANSFER_DETAIL_PEER_ABORT;
			break;
		}
		if(marker == EOT)
		{
			if(sv6x66_acked != sv6x66_length || sv6x66_programmed != sv6x66_length)
			{
				detail = SV6X66_TRANSFER_DETAIL_EARLY_EOT;
				break;
			}
			if(!crc32_memory(sv6x66_offset, sv6x66_length, &sv6x66_range_crc))
			{
				status = OBK_STATUS_ERROR;
				detail = SV6X66_TARGET_DETAIL_FINAL_CRC;
				break;
			}
			if(sv6x66_range_crc != sv6x66_expected_crc)
			{
				status = SV6X66_STATUS_CRC_MISMATCH;
				detail = sv6x66_range_crc;
				break;
			}
			sv6x66_flags |= SV6X66_RANGE_VERIFIED;
			sv6x66_restore_status = OBK_STATUS_SUCCESS;
			sv6x66_transaction_state = 3U;
			uart_putc(ACK);
			return;
		}
		if(marker != STX)
		{
			detail = SV6X66_TRANSFER_DETAIL_UNEXPECTED_MARKER;
			uart_putc(NAK);
			if(++retries >= XMODEM_MAX_RETRIES) break;
		}
		else
		{
			uint8_t block, inverse, hi, lo;
			uint32_t budget = XMODEM_WAIT_LOOPS;
			detail = SV6X66_TRANSFER_DETAIL_PACKET_TIMEOUT;
			if(!uart_getc_budget(&block, &budget) || !uart_getc_budget(&inverse, &budget)) break;
			for(uint32_t i = 0U; i < 1024U; i++)
				if(!uart_getc_budget(xmodem_packet + i, &budget)) goto failed;
			if(!uart_getc_budget(&hi, &budget) || !uart_getc_budget(&lo, &budget)) break;
			if(inverse != (uint8_t)~block || (((uint16_t)hi << 8) | lo) != crc16_xmodem(xmodem_packet, 1024U))
			{
				detail = SV6X66_TRANSFER_DETAIL_PACKET_INTEGRITY;
				uart_putc(NAK);
				if(++retries >= XMODEM_MAX_RETRIES) break;
			}
			else if(sv6x66_acked && block == (uint8_t)(sv6x66_acked / 1024U))
			{
				/* As in ECR, a valid immediate duplicate ACK never programs twice. */
				uart_putc(ACK);
			}
			else
			{
				if(block != (uint8_t)(sv6x66_acked / 1024U + 1U) || sv6x66_acked >= sv6x66_length)
				{
					detail = SV6X66_TRANSFER_DETAIL_BLOCK_SEQUENCE;
					break;
				}
				for(uint32_t page = 0U; page < 1024U; page += FLASH_PAGE_SIZE)
					if(!stub_flash_program_page_completed(sv6x66_offset + sv6x66_acked + page, xmodem_packet + page))
					{
						status = OBK_STATUS_ERROR;
						detail = SV6X66_TARGET_DETAIL_PAGE_PROGRAM;
						goto failed;
					}
				sv6x66_programmed = sv6x66_acked + 1024U;
				/* ACK proves program completion; only EOT's actual flash CRC verifies content. */
				sv6x66_acked = sv6x66_programmed;
				retries = 0U;
				uart_putc(ACK);
			}
		}
		if(!uart_getc_timeout(&marker, XMODEM_WAIT_LOOPS))
		{
			detail = SV6X66_TRANSFER_DETAIL_NEXT_MARKER_TIMEOUT;
			break;
		}
	}
failed:
	sv6x66_detail = detail;
	sv6x66_restore_status = status;
	sv6x66_transaction_state = 3U;
	uart_putc(CAN);
	uart_putc(CAN);
}

static int sv6x66_descriptor_matches(void)
{
	return cmd_buf[13] == sv6x66_region && load_le32(cmd_buf + 14) == sv6x66_offset &&
		load_le32(cmd_buf + 18) == sv6x66_length && load_le32(cmd_buf + 22) == sv6x66_expected_crc;
}

static void sv6x66_transaction_control(uint16_t data_len)
{
	uint8_t operation = data_len ? cmd_buf[0] : 0U;
	uint32_t attempt = data_len >= 13U ? load_le32(cmd_buf + 9) : 0U;
	uint8_t error = SV6X66_STATUS_BAD_STATE;
	if(sv6x66_application_mode) goto reject;
	if(operation == 1U)
	{
		if(data_len != 41U) goto bad_length;
		uint8_t nonce_nonzero = 0U, sha_nonzero = 0U;
		for(uint32_t i = 0U; i < 8U; i++) nonce_nonzero |= cmd_buf[1U + i];
		for(uint32_t i = 0U; i < 32U; i++) sha_nonzero |= cmd_buf[9U + i];
		if(!nonce_nonzero || !sha_nonzero) goto reject;
		if(sv6x66_session_set)
		{
			if(!sv6x66_session_matches(cmd_buf + 1) || !sv6x66_equal(cmd_buf + 9, sv6x66_source_sha, 32U)) goto reject;
		}
		else
		{
			memcpy(sv6x66_session, cmd_buf + 1, 8U);
			memcpy(sv6x66_source_sha, cmd_buf + 9, 32U);
			sv6x66_session_set = 1U;
		}
	}
	else
	{
		if(data_len < 13U) goto bad_length;
		if(!sv6x66_session_matches(cmd_buf + 1) || !attempt) goto reject;
		if(operation == 2U)
		{
			if(data_len != 26U) goto bad_length;
			if(attempt == sv6x66_attempt && sv6x66_transaction_state != 0U)
			{
				if(!sv6x66_descriptor_matches()) goto reject;
			}
			else
			{
				uint8_t next = sv6x66_region == 0xFFU ? 0U : (uint8_t)(sv6x66_region + 1U);
				if(next >= 8U || cmd_buf[13] != next || attempt != (uint32_t)next + 1U ||
					(sv6x66_transaction_state != 0U && (sv6x66_transaction_state != 4U ||
						sv6x66_restore_status != OBK_STATUS_SUCCESS || !(sv6x66_flags & SV6X66_RANGE_VERIFIED)))) goto reject;
				if(load_le32(cmd_buf + 14) != sv6x66_region_start[next] ||
					load_le32(cmd_buf + 18) != sv6x66_region_length[next] ||
					flash_id != SV6X66_FLASH_ID || flash_size != STUB_READ_ONLY_FLASH_BYTES)
				{
					error = OBK_STATUS_ADDR_ERROR;
					goto reject;
				}
				sv6x66_region = next;
				sv6x66_attempt = attempt;
				sv6x66_offset = sv6x66_region_start[next];
				sv6x66_length = sv6x66_region_length[next];
				sv6x66_expected_crc = load_le32(cmd_buf + 22);
				sv6x66_programmed = sv6x66_acked = sv6x66_erased = sv6x66_flags = 0U;
				sv6x66_detail = sv6x66_range_crc = 0U;
				sv6x66_restore_status = 0U;
				sv6x66_transaction_state = 1U;
			}
		}
		else
		{
			if(data_len != 13U || operation < 5U || operation > 7U) goto bad_length;
			if(attempt != sv6x66_attempt || sv6x66_transaction_state == 0U) goto reject;
			if(operation == 5U)
			{
				if(sv6x66_transaction_state == 3U || sv6x66_transaction_state == 4U) goto reply;
				if(sv6x66_transaction_state != 1U) goto reject;
				obk_ack(SV6X66_CMD_TRANSACTION, OBK_STATUS_SUCCESS);
				sv6x66_transaction_state = 5U;
				sv6x66_receive_region();
				return;
			}
			if(operation == 7U)
			{
				if((sv6x66_transaction_state != 3U && sv6x66_transaction_state != 4U) ||
					sv6x66_restore_status != OBK_STATUS_SUCCESS || sv6x66_acked != sv6x66_length ||
					sv6x66_programmed != sv6x66_length || sv6x66_erased != sv6x66_length ||
					!(sv6x66_flags & SV6X66_RANGE_VERIFIED) || sv6x66_range_crc != sv6x66_expected_crc) goto reject;
				sv6x66_transaction_state = 4U;
			}
		}
	}
reply:
	sv6x66_transaction_reply(SV6X66_CMD_TRANSACTION);
	return;
bad_length:
	error = OBK_STATUS_LEN_ERROR;
reject:
	obk_ack(SV6X66_CMD_TRANSACTION, error);
}

static void sv6x66_transaction_begin(uint16_t data_len)
{
	(void)data_len;
	/* Legacy sector writes are unavailable in the v3 ECR-style range profile. */
	obk_ack(OBK_CMD_FLASH_XMODEM_DL, OBK_STATUS_NOT_SUPPORTED);
}

