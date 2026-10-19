/* CKW04 inactive-bank installation, separate from the legacy V3 full restore.
 * Source-only until both carriers and host have attributable local HW proof.
 * All wire integers are little endian; declared SHA binds the host session,
 * while the target verifies actual programmed bytes with full-bank CRC32. */
#define SV6X66_CMD_APPLICATION 0x9EU
#define SV6X66_APP_BYTES 0x0009B000U
#define SV6X66_APP_REPLY_BYTES 84U

static uint8_t sv6x66_app_state;
static uint8_t sv6x66_app_nonce[8];
static uint8_t sv6x66_app_sha[32];
static uint32_t sv6x66_app_raw_length;

static uint32_t sv6x66_app_base(void)
{
	return sv6x66_carrier_bank == 0U ? 0x0000B000U : 0x000A6000U;
}

static uint32_t sv6x66_app_selector(uint32_t bank)
{
	return bank == 1U ? 0U : 0xFFFFFFFFU;
}

static int sv6x66_read_selector(uint32_t* value)
{
	uint8_t page[FLASH_PAGE_SIZE];
	for(uint32_t offset = 0U; offset < FLASH_SECTOR_SIZE; offset += sizeof(page)) {
		if(!stub_flash_read_checked(page, 0x4000U + offset, sizeof(page))) return 0;
		if(offset == 0U) *value = load_le32(page);
		for(uint32_t i = offset == 0U ? 4U : 0U; i < sizeof(page); i++)
			if(page[i] != 0xFFU) return 0;
	}
	return 1;
}

static void sv6x66_application_reply(void)
{
	uint8_t reply[SV6X66_APP_REPLY_BYTES] = { 0U };
	uint32_t selector = 0xFFFFFFFFU;
	int profile_ok = sv6x66_read_selector(&selector);
	reply[0] = 1U;
	reply[1] = sv6x66_app_state;
	reply[2] = sv6x66_restore_status;
	reply[3] = (uint8_t)sv6x66_carrier_bank;
	reply[4] = (uint8_t)(sv6x66_carrier_bank ^ 1U);
	reply[5] = profile_ok ? 1U : 0U;
	memcpy(reply + 8, sv6x66_app_nonce, 8U);
	memcpy(reply + 16, sv6x66_app_sha, 32U);
	sv6x66_store32(reply + 48, SV6X66_APP_BYTES);
	sv6x66_store32(reply + 52, sv6x66_expected_crc);
	sv6x66_store32(reply + 56, sv6x66_range_crc);
	sv6x66_store32(reply + 60, selector);
	sv6x66_store32(reply + 64, sv6x66_programmed);
	sv6x66_store32(reply + 68, sv6x66_acked);
	sv6x66_store32(reply + 72, sv6x66_erased);
	sv6x66_store32(reply + 76, sv6x66_flags);
	sv6x66_store32(reply + 80, sv6x66_app_raw_length);
	obk_data_reply(SV6X66_CMD_APPLICATION, reply, sizeof(reply), OBK_STATUS_SUCCESS);
}

static int sv6x66_application_image_valid(void)
{
	uint8_t page[FLASH_PAGE_SIZE];
	if(sv6x66_app_raw_length < 68U || sv6x66_app_raw_length > SV6X66_APP_BYTES ||
		!stub_flash_read_checked(page, sv6x66_app_base(), 4U) ||
		page[0] != 0x48U || page[1] != 0U || page[2] != 0U || page[3] != 2U ||
		!stub_flash_read_checked(page, sv6x66_app_base() + sv6x66_app_raw_length - 64U, 64U) ||
		!sv6x66_equal(page, (const uint8_t*)"OBKSV6X1", 8U) ||
		load_le32(page + 8) != sv6x66_app_base() || load_le32(page + 12) != SV6X66_APP_BYTES ||
		load_le32(page + 16) != sv6x66_app_raw_length || load_le32(page + 20) != 1U) return 0;
	for(uint32_t i = 24U; i < 64U; i++) if(page[i] != 0U) return 0;
	for(uint32_t offset = sv6x66_app_raw_length; offset < SV6X66_APP_BYTES;) {
		uint32_t chunk = SV6X66_APP_BYTES - offset;
		if(chunk > sizeof(page)) chunk = sizeof(page);
		if(!stub_flash_read_checked(page, sv6x66_app_base() + offset, chunk)) return 0;
		for(uint32_t i = 0U; i < chunk; i++) if(page[i] != 0xFFU) return 0;
		offset += chunk;
	}
	return 1;
}

static int sv6x66_application_commit(void)
{
	uint32_t selector, completed, crc;
	uint8_t page[FLASH_PAGE_SIZE];
	if(sv6x66_app_state != 3U || sv6x66_restore_status != OBK_STATUS_SUCCESS ||
		sv6x66_erased != SV6X66_APP_BYTES || sv6x66_acked != SV6X66_APP_BYTES ||
		sv6x66_programmed != SV6X66_APP_BYTES || !(sv6x66_flags & SV6X66_RANGE_VERIFIED) ||
		!crc32_memory(sv6x66_app_base(), SV6X66_APP_BYTES, &crc) || crc != sv6x66_expected_crc ||
		!sv6x66_application_image_valid() ||
		!sv6x66_read_selector(&selector) ||
		selector != sv6x66_app_selector(sv6x66_carrier_bank ^ 1U)) return 0;

	/* Selector is the LAST destructive commit. State 2 remains indeterminate
	 * after any error: the host must query, never automatically replay it.
	 * Erase's intermediate FF boots A: either the old A or verified new A. */
	sv6x66_app_state = 2U;
	sv6x66_restore_status = OBK_STATUS_ERROR;
	if(!stub_flash_erase_range_progress(0x4000U, FLASH_SECTOR_SIZE, &completed)) return 0;
	if(sv6x66_carrier_bank == 1U) {
		memset(page, 0xFF, sizeof(page));
		memset(page, 0, 4U);
		if(!stub_flash_program_page_completed(0x4000U, page)) return 0;
	}
	if(!sv6x66_read_selector(&selector) ||
		selector != sv6x66_app_selector(sv6x66_carrier_bank)) return 0;
	sv6x66_app_state = 4U;
	sv6x66_restore_status = OBK_STATUS_SUCCESS;
	return 1;
}

static void sv6x66_application_control(uint16_t length)
{
	uint32_t selector;
	uint8_t operation = length ? cmd_buf[0] : 0xFFU;
	uint8_t error = SV6X66_STATUS_BAD_STATE;
	if(sv6x66_session_set) goto reject;
	if(sv6x66_carrier_bank > 1U || flash_id != SV6X66_FLASH_ID ||
		flash_size != STUB_READ_ONLY_FLASH_BYTES) goto reject;
	if(operation == 0U) {
		if(length != 1U) goto bad_length;
		goto reply;
	}
	if(operation == 1U) {
		uint8_t nonce_nonzero = 0U, sha_nonzero = 0U;
		if(length != 53U) goto bad_length;
		if(sv6x66_app_state != 0U) {
			if(!sv6x66_equal(cmd_buf + 1, sv6x66_app_nonce, 8U) ||
				!sv6x66_equal(cmd_buf + 9, sv6x66_app_sha, 32U) ||
				load_le32(cmd_buf + 41) != sv6x66_expected_crc ||
				load_le32(cmd_buf + 45) != SV6X66_APP_BYTES ||
				load_le32(cmd_buf + 49) != sv6x66_app_raw_length) goto reject;
			goto reply;
		}
		for(uint32_t i = 0U; i < 8U; i++) nonce_nonzero |= cmd_buf[1U + i];
		for(uint32_t i = 0U; i < 32U; i++) sha_nonzero |= cmd_buf[9U + i];
		if(!nonce_nonzero || !sha_nonzero || load_le32(cmd_buf + 45) != SV6X66_APP_BYTES ||
			load_le32(cmd_buf + 49) < 68U || load_le32(cmd_buf + 49) > SV6X66_APP_BYTES ||
			!sv6x66_read_selector(&selector) ||
			selector != sv6x66_app_selector(sv6x66_carrier_bank ^ 1U)) goto reject;
		memcpy(sv6x66_app_nonce, cmd_buf + 1, 8U);
		memcpy(sv6x66_app_sha, cmd_buf + 9, 32U);
		sv6x66_offset = sv6x66_app_base();
		sv6x66_length = SV6X66_APP_BYTES;
		sv6x66_expected_crc = load_le32(cmd_buf + 41);
		sv6x66_app_raw_length = load_le32(cmd_buf + 49);
		sv6x66_application_mode = 1U;
		sv6x66_app_state = 1U;
		goto reply;
	}
	if(length != 9U) goto bad_length;
	if(!sv6x66_app_state || !sv6x66_equal(cmd_buf + 1, sv6x66_app_nonce, 8U)) goto reject;
	if(operation == 2U) {
		if(sv6x66_app_state != 1U) goto reject;
		if(!sv6x66_read_selector(&selector) ||
			selector != sv6x66_app_selector(sv6x66_carrier_bank ^ 1U)) goto reject;
		obk_ack(SV6X66_CMD_APPLICATION, OBK_STATUS_SUCCESS);
		sv6x66_receive_region();
		sv6x66_app_state = 3U;
		return;
	}
	if(operation == 3U) goto reply;
	if(operation == 4U) {
		if(sv6x66_app_state != 4U && !sv6x66_application_commit()) goto reject;
		goto reply;
	}
	goto reject;
reply:
	sv6x66_application_reply();
	return;
bad_length:
	error = OBK_STATUS_LEN_ERROR;
reject:
	obk_ack(SV6X66_CMD_APPLICATION, error);
}
