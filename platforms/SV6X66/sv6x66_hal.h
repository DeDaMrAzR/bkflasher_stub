#ifndef SV6X66_HAL_H
#define SV6X66_HAL_H

#include <stdint.h>

/* Holds the largest framed request; XMODEM owns a separate 1030-byte packet. */
#undef BUF_SIZE
#define BUF_SIZE 1024U

#define STUB_READ_ONLY_FLASH_BYTES 0x00200000U
#define FLASH_BASE 0U

#define HSUART_DATA 0xC0000D00U
#define HSUART_LCR  0xC0000D0CU
#define HSUART_LSR  0xC0000D14U
#define HSUART_BAUD 0xC0000D28U
#define HSUART_CLOCK_SELECT 0xC0000018U
#define HSUART_LSR_DR   0x01U
#define HSUART_LSR_THRE 0x20U
#define HSUART_LSR_TEMT 0x40U

#define SV6X66_BAUD_DIAGNOSTIC_SIZE 16U
#define SV6X66_FLASH_ID 0x001540C8U
#define SV6X66_STATUS_BAD_STATE 0x12U
#define SV6X66_STATUS_TRANSFER_ABORTED_RETRYABLE 0x13U

#define SV6X66_TRANSFER_DETAIL_START_HANDSHAKE 0x00000001U
#define SV6X66_TRANSFER_DETAIL_PEER_ABORT 0x00000002U
#define SV6X66_TRANSFER_DETAIL_UNEXPECTED_MARKER 0x00000003U
#define SV6X66_TRANSFER_DETAIL_PACKET_TIMEOUT 0x00000004U
#define SV6X66_TRANSFER_DETAIL_PACKET_INTEGRITY 0x00000005U
#define SV6X66_TRANSFER_DETAIL_BLOCK_SEQUENCE 0x00000006U
#define SV6X66_TRANSFER_DETAIL_NEXT_MARKER_TIMEOUT 0x00000007U
#define SV6X66_TRANSFER_DETAIL_EARLY_EOT 0x00000008U
#define SV6X66_TARGET_DETAIL_ERASE 0x00000101U
#define SV6X66_TARGET_DETAIL_PAGE_PROGRAM 0x00000102U
#define SV6X66_TARGET_DETAIL_FINAL_CRC 0x00000103U

int stub_uart_set_baud_checked(uint32_t baud);
int8_t uart_getc_budget(uint8_t* out, uint32_t* loops);
int stub_flash_read_checked(void* dest, uint32_t off, uint32_t len);
int stub_uart_prepare_baud(uint32_t baud, uint8_t* diagnostic);
void stub_uart_commit_prepared_baud(void);
int stub_flash_erase_range_progress(uint32_t off, uint32_t len, uint32_t* completed);
int stub_flash_program_page_completed(uint32_t off, const uint8_t* data);
extern uint32_t sv6x66_carrier_bank;
#define SV6X66_BANK_HANDOFF_MAGIC 0x53423641U

#define SPI_CMD_SRAM       0xC0001000U
#define SPI_DATAIN_SRAM    0xC0001004U
#define SPI_CONTROL        0xC000100CU
#define SPI_TLEN           0xC0001010U
#define SPI_RLEN           0xC0001014U
#define SPI_BUSY_MASK      0x08U
#define SPI_MODE_MASK      0x06U

#define SPI_CMD_BUFFER     0x00008000U
#define SPI_PAYLOAD_BUFFER 0x00008004U
#define SPI_READ_BUFFER    0x00008400U

#endif
