#ifndef RTL8720D_HAL_H
#define RTL8720D_HAL_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

//#undef BUF_SIZE
//#define BUF_SIZE (512 * 3)
#undef BAUD_DELAY
#define BAUD_DELAY 200000
#define PLATFORM_SUPPORTS_OTP

#define __I     volatile const       /*!< Defines 'read only' permissions                 */
#define __O     volatile             /*!< Defines 'write only' permissions                */
#define __IO    volatile             /*!< Defines 'read / write' permissions              */

typedef struct
{
	void (*rdp_decrypt_func)(uint32_t addr, uint8_t* key, uint8_t cnt, uint8_t* buf);
	uint32_t psram_s_start_addr;
} BOOT_EXPORT_SYMB_TABLE;

typedef struct _RAM_FUNCTION_START_TABLE_
{
	void(*RamStartFun) (void);
	void(*RamWakeupFun) (void);
	void(*RamPatchFun0) (void);
	void(*RamPatchFun1) (void);
	void(*RamPatchFun2) (void);
	void(*FlashStartFun) (void);
	uint32_t Img1ValidCode;
	BOOT_EXPORT_SYMB_TABLE* ExportTable;
}RAM_FUNCTION_START_TABLE, * PRAM_FUNCTION_START_TABLE;

typedef struct
{
	uint32_t FLASH_Id;					/*!< Specifies the flash vendor ID.
										This parameter can be a value of @ref FLASH_VENDOR_ID_definitions */
	uint8_t FLASH_cur_bitmode;			/*!< Specifies the current bitmode of SPIC.
										This parameter can be a value of @ref FLASH_BIT_Mode_definitions */
	uint8_t FLASH_baud_rate;			/*!< Specifies the spi_sclk divider value. The frequency of spi_sclk is derived from:
										Frequency of spi_sclk = Frequency of oc_clk / (2 * FLASH_baud_rate) */
	uint8_t FLASH_baud_boot;			/*!< Specifies the spi_sclk divider value for rom boot. The frequency of spi_sclk is derived from:
										Frequency of spi_sclk = Frequency of oc_clk / (2 * FLASH_baud_rate) */
	uint32_t FLASH_cur_cmd; 			/*!< Specifies the current read cmd which is used to read data from flash
										in current bitmode. */

										/* status bits define */
	uint32_t FLASH_QuadEn_bit;			/*!< Specifies the QE bit in status register which is used to enable Quad I/O mode . */
	uint32_t FLASH_Busy_bit;			/*!< Specifies the WIP(Write in Progress) bit in status register which indicates whether
										the device is busy in program/erase/write status register progress. */
	uint32_t FLASH_WLE_bit; 			/*!< Specifies the WEL(Write Enable Latch) bit in status register which indicates whether
										the device will accepts program/erase/write status register instructions*/
	uint32_t FLASH_Status2_exist;		/*!< Specifies whether this flash chip has Status Register2 or not.
										This parameter can be 0/1. 0 means it doesn't have Status Register2, 1 means
										it has Status Register2.*/

										/* calibration data */
	uint8_t FLASH_rd_sample_phase_cal;	/*!< Specifies the read sample phase obtained from calibration. this is cal sample phase get from high speed cal */
	uint8_t FLASH_rd_sample_phase;		/*!< Specifies the read sample phase obtained from calibration. this is current sample phase */
	uint8_t FLASH_rd_dummy_cyle[3];		/*!< Specifies the read dummy cycle of different bitmode according to
										flash datasheet*/

										/* valid R/W command set */
	uint32_t FLASH_rd_dual_o;			/*!< Specifies dual data read cmd */
	uint32_t FLASH_rd_dual_io;			/*!< Specifies dual data/addr read cmd */
	uint32_t FLASH_rd_quad_o;			/*!< Specifies quad data read cmd */
	uint32_t FLASH_rd_quad_io;			/*!< Specifies quad data/addr read cmd */
	uint32_t FLASH_wr_dual_i;			/*!< Specifies dual data write cmd */
	uint32_t FLASH_wr_dual_ii;			/*!< Specifies dual data/addr write cmd */
	uint32_t FLASH_wr_quad_i;			/*!< Specifies quad data write cmd */
	uint32_t FLASH_wr_quad_ii;			/*!< Specifies quad data/addr write cmd */
	uint32_t FALSH_dual_valid_cmd;		/*!< Specifies valid cmd of dual bitmode to program/read flash in auto mode */
	uint32_t FALSH_quad_valid_cmd;		/*!< Specifies valid cmd of quad bitmode to program/read flash in auto mode */

	/* other command set */
	uint8_t FLASH_cmd_wr_en;			/*!< Specifies the Write Enable(WREN) instruction which is for setting WEL bit*/
	uint8_t FLASH_cmd_rd_id;			/*!< Specifies the Read ID instruction which is for getting the identity of the flash divice.*/
	uint8_t FLASH_cmd_rd_status;		/*!< Specifies the Read Status Register instruction which is for getting the status of flash */
	uint8_t FLASH_cmd_rd_status2;		/*!< Specifies the Read Status Register2 instruction which is for getting the status2 of flash */
	uint8_t FLASH_cmd_wr_status;		/*!< Specifies the Write Status Register instruction which is for setting the status register of flash */
	uint8_t FLASH_cmd_wr_status2;		/*!< Specifies the Write Status Register2 instruction which is for setting the status register2 of flash.
										In some flash chips, status2 write cmd != status1 write cmd,
										like: GD25Q32C, GD25Q64C,GD25Q128C etc.*/
	uint8_t FLASH_cmd_chip_e;			/*!< Specifies the Erase Chip instruction which is for erasing the whole chip*/
	uint8_t FLASH_cmd_block_e;			/*!< Specifies the Erase Block instruction which is for erasing 64kB*/
	uint8_t FLASH_cmd_sector_e;			/*!< Specifies the Erase Sector instruction which is for erasing 4kB*/
	uint8_t FLASH_cmd_pwdn_release;		/*!< Specifies the Release from Deep Power Down instruction which is for exiting power down mode.*/
	uint8_t FLASH_cmd_pwdn;				/*!< Specifies the Deep Power Down instruction which is for entering power down mode.*/

	/* debug log */
	uint8_t debug;						/*!< Specifies whether or not to print debug log.*/

	/* new calibration */
	uint8_t phase_shift_idx;			/*!< Specifies the phase shift idx in new calibration.*/

	uint8_t FLASH_addr_phase_len;		/*!< Specifies the number of bytes in address phase (between command phase and write/read phase).
										This parameter can be 0/1/2/3. 0 means 4-byte address mode in SPI Flash.*/
	uint8_t FLASH_pseudo_prm_en;		/*!< Specifies whether SPIC enables SPIC performance read mode or not.*/
	uint8_t FLASH_pinmux;				/*!< Specifies which pinmux is used. PINMUX_S0 or PINMUX_S1*/

	uint32_t FLASH_rd_fast_single;		/*!< Specifies fast read cmd in auto mode.*/
} FLASH_InitTypeDef;

typedef struct
{
	__IO uint32_t DLL;				/*!< UART Divisor Latch register(not used in Amebaz),	Address offset: 0x00*/
	__IO uint32_t DLH_INTCR;		/*!< UART interrupt enable register,              			Address offset: 0x04*/
	__IO uint32_t INTID;			/*!< UART interrupt identification register,              	Address offset: 0x08*/
	__IO uint32_t LCR;				/*!< UART line control register,              				Address offset: 0x0C*/
	__IO uint32_t MCR;				/*!< UART modem control register,              			Address offset: 0x10*/
	__I uint32_t LSR;				/*!< UART line status register,              				Address offset: 0x14*/
	__I uint32_t MDSR;				/*!< UART modem status register,              			Address offset: 0x18*/
	__IO uint32_t SPR;				/*!< UART scratch pad register,              				Address offset: 0x1C*/
	__IO uint32_t STSR;				/*!< UART STS register,              					Address offset: 0x20*/
	__IO uint32_t RB_THR;			/*!< UART receive buffer/transmitter holding register,	Address offset: 0x24*/
	__IO uint32_t MISCR;			/*!< UART MISC control register,              			Address offset: 0x28*/
	__IO uint32_t TXPLSR;			/*!< UART IrDA SIR TX Pulse Width Control register,	Address offset: 0x2C*/

	__IO uint32_t RXPLSR;			/*!< UART IrDA SIR RX Pulse Width Control register,	Address offset: 0x30*/
	__IO uint32_t BAUDMONR;			/*!< UART baud monitor register,              			Address offset: 0x34*/
	__IO uint32_t RSVD2;			/*!< UART reserved field,              					Address offset: 0x38*/
	__IO uint32_t DBG_UART;			/*!< UART debug register,              					Address offset: 0x3C*/

	/* AmebaZ add for power save */
	__IO uint32_t RX_PATH;			/*!< UART rx path control register,              			Address offset: 0x40*/
	__IO uint32_t MON_BAUD_CTRL;	/*!< UART monitor baud rate control register,              	Address offset: 0x44*/
	__IO uint32_t MON_BAUD_STS;		/*!< UART monitor baud rate status register,              	Address offset: 0x48*/
	__IO uint32_t MON_CYC_NUM;		/*!< UART monitor cycle number register,              		Address offset: 0x4c*/
	__IO uint32_t RX_BYTE_CNT;		/*!< UART rx byte counter register,              			Address offset: 0x50*/

	/* AmebaZ change */
	__IO uint32_t FCR;				/*!< UART FIFO Control register,              			Address offset: 0x54*/
} UART_TypeDef;

typedef struct
{
	__IO uint32_t ctrlr0;			/*!< SPIC control register0,		Address offset: 0x000 */
	__IO uint32_t ctrlr1;			/*!< SPIC control register1,		Address offset: 0x004 */
	__IO uint32_t ssienr;			/*!< SPIC enable register,		Address offset: 0x008 */
	__IO uint32_t mwcr;			/*!< N/A,					Address offset: 0x00C */
	__IO uint32_t ser;				/*!< SPIC slave enable register,	Address offset: 0x010 */
	__IO uint32_t baudr;			/*!< SPIC baudrate select register,	Address offset: 0x014 */
	__IO uint32_t txftlr;				/*!< SPIC transmit FIFO threshold level,	Address offset: 0x018 */
	__IO uint32_t rxftlr;				/*!< SPIC receive FIFO threshold level,	Address offset: 0x01C */
	__IO uint32_t txflr;				/*!< SPIC transmit FIFO level register,	Address offset: 0x020 */
	__IO uint32_t rxflr;				/*!< SPIC receive FIFO level register,	Address offset: 0x024 */
	__IO uint32_t sr;				/*!< SPIC status register,				Address offset: 0x028 */
	__IO uint32_t imr;				/*!< SPIC interrupt mask register,		Address offset: 0x02C */
	__IO uint32_t isr;				/*!< SPIC interrupt status register,		Address offset: 0x030 */
	__IO uint32_t risr;				/*!< SPIC raw interrupt status register,	Address offset: 0x034 */
	__IO uint32_t txoicr;			/*!< SPIC transmit FIFO overflow interrupt clear register,	Address offset: 0x038 */
	__IO uint32_t rxoicr;			/*!< SPIC receive FIFO overflow interrupt clear register,	Address offset: 0x03C */
	__IO uint32_t rxuicr;			/*!< SPIC receive FIFO underflow interrupt clear register,	Address offset: 0x040 */
	__IO uint32_t msticr;			/*!< SPIC master error interrupt clear register,	Address offset: 0x044 */
	__IO uint32_t icr;				/*!< SPIC interrupt clear register,	Address offset: 0x048 */
	__IO uint32_t dmacr;			/*!< N/A,					Address offset: 0x04C */
	__IO uint32_t dmatdlr;			/*!< N/A,					Address offset: 0x050 */
	__IO uint32_t dmardlr;			/*!< N/A,					Address offset: 0x054 */
	__IO uint32_t idr;				/*!< SPIC Identiation register,		Address offset: 0x058 */
	__IO uint32_t spi_flash_version;	/*!< SPIC version ID register,		Address offset: 0x05C */
	union
	{
		__IO uint8_t  byte;
		__IO uint16_t half;
		__IO uint32_t word;
	} dr[32];						/*!< SPIC data register,					Address offset: 0x060~0x0DC */
	__IO uint32_t rd_fast_single;		/*!< Fast read data command of SPI Flash,	Address offset: 0x0E0 */
	__IO uint32_t rd_dual_o;		/*!< Dual output read command of SPI Flash,	Address offset: 0x0E4 */
	__IO uint32_t rd_dual_io;		/*!< Dual I/O read command of SPI Flash,	Address offset: 0x0E8 */
	__IO uint32_t rd_quad_o; 		/*!< Quad output read command of SPI Flash,	Address offset: 0x0EC */
	__IO uint32_t rd_quad_io;		/*!< Quad I/O read command of SPI Flash,	Address offset: 0x0F0 */
	__IO uint32_t wr_single;			/*!< Page program command of SPI Flash,	Address offset: 0x0F4 */
	__IO uint32_t wr_dual_i;			/*!< Dual data input program command of SPI Flash,			Address offset: 0x0F8 */
	__IO uint32_t wr_dual_ii;		/*!< Dual address and data input program command of SPI Flash,	Address offset: 0x0FC */
	__IO uint32_t wr_quad_i;		/*!< Quad data input program command of SPI Flash,			Address offset: 0x100 */
	__IO uint32_t wr_quad_ii;		/*!< Quad address and data input program command of SPI Flash,	Address offset: 0x104 */
	__IO uint32_t wr_enable;		/*!< Write enabe command of SPI Flash,	Address offset: 0x108 */
	__IO uint32_t rd_status;			/*!< Read status command of SPI Flash,	Address offset: 0x10C */
	__IO uint32_t ctrlr2;			/*!< SPIC control register2,			Address offset: 0x110 */
	__IO uint32_t fbaudr;			/*!< SPIC fast baudrate select,			Address offset: 0x114 */
	__IO uint32_t addr_length;		/*!< SPIC address length register,		Address offset: 0x118 */
	__IO uint32_t auto_length;		/*!< SPIC auto address length register,	Address offset: 0x11C */
	__IO uint32_t valid_cmd;		/*!< SPIC valid command register,		Address offset: 0x120 */
	__IO uint32_t flash_size;			/*!< SPIC flash size register,			Address offset: 0x124 */
	__IO uint32_t flush_fifo;			/*!< SPIC flush FIFO register,			Address offset: 0x128 */
} SPIC_TypeDef;
#define BIT_CMD_CH(x)					(((x) & 0x00000003) << 20)
#define BIT_DATA_CH(x)					(((x) & 0x00000003) << 18)
#define BIT_ADDR_CH(x)					(((x) & 0x00000003) << 16)
#define BIT_TMOD(x)						(((x) & 0x00000003) << 8)
#define BIT_SCPOL						(0x00000001 << 7)
#define BIT_SCPH						(0x00000001 << 6)

#define _LONG_CALL_						__attribute__ ((long_call))
#define FLASH_BASE						0x08000000
#define LOG_UART_REG_BASE				0x48012000
#define SYSTEM_CTRL_BASE_LP				0x48000000
#define SPI_FLASH_CTRL_BASE				0x48080000
#define REG_LP_CLK_CTRL0				0x0210
#define BIT_SHIFT_FLASH_CLK_SEL			9
#define BIT_MASK_FLASH_CLK_SEL			0x03
#define BIT_SHIFT_FLASH_CLK_ANA4M		(0x0 << BIT_SHIFT_FLASH_CLK_SEL)
#define BIT_SHIFT_FLASH_CLK_XTAL		(0x1 << BIT_SHIFT_FLASH_CLK_SEL)
#define BIT_SHIFT_FLASH_CLK_PLL			(0x2 << BIT_SHIFT_FLASH_CLK_SEL)
#define UART2_DEV						((UART_TypeDef*)LOG_UART_REG_BASE)
#define SPIC							((SPIC_TypeDef*) SPI_FLASH_CTRL_BASE)
#define EraseChip						0
#define EraseBlock						1
#define EraseSector						2
#define FLASH_ID_OTHERS					0
#define FLASH_ID_MXIC					1
#define FLASH_ID_WINBOND				2
#define FLASH_ID_MICRON					3
#define FLASH_ID_EON					4
#define FLASH_ID_GD						5
#define FLASH_ID_BOHONG					6
#define ReadQuadIOMode					0
#define ReadQuadOMode					1
#define ReadDualIOMode					2
#define ReadDualOMode 					3
#define ReadOneMode						4
#define SpicOneBitMode					0
#define SpicDualBitMode					1
#define SpicQuadBitMode					2
#define BIT_CTRLR0_CH					(0x00000001 << 12)
#define BIT_PRM_EN						(0x00000001 << 11)
#define BIT_WR_BLOCKING					(0x00000001 << 9)
#define BIT_WR_QUAD_II					(0x00000001 << 8)
#define BIT_WR_QUAD_I					(0x00000001 << 7)
#define BIT_WR_DUAL_II					(0x00000001 << 6)
#define BIT_WR_DUAL_I					(0x00000001 << 5)
#define BIT_RD_QUAD_IO					(0x00000001 << 4)
#define BIT_RD_QUAD_O					(0x00000001 << 3)
#define BIT_RD_DUAL_IO					(0x00000001 << 2)
#define BIT_RD_DUAL_I					(0x00000001 << 1)
#define BIT_FRD_SINGEL					(0x00000001)
#define SPIC_VALID_CMD_MASK				(0x7fff)
#define DUAL_PRM_CYCLE_NUM				4
#define QUAD_PRM_CYCLE_NUM				2
#define FLASH_DM_CYCLE_2O				0x08
#define FLASH_DM_CYCLE_2IO				0x04
#define FLASH_DM_CYCLE_4O				0x08
#define FLASH_DM_CYCLE_4IO				0x06
#define FLASH_CMD_PP					0x02            //Page Program
#define FLASH_CMD_READ					0x03            //read data
#define FLASH_CMD_DREAD					0x3B            //Double Output Mode command
#define FLASH_CMD_FREAD					0x0B            //fast read data
#define FLASH_CMD_2READ					0xBB            // 2 x I/O read  command
#define FLASH_CMD_4READ					0xEB            // 4 x I/O read  command
#define FLASH_CMD_QREAD					0x6B            // 1I / 4O read command


#define SYS_CLK_CTRL1					0x03U //0x210
#define SYS_FUNC_EN1					0x03U //0x200
#define BIT_LSYS_GPIO0_FEN				BIT(20)
#define BIT_LSYS_GPIO0_CKE				BIT(20)	/* R/W	0	 */	
#define APBPeriph_GPIO_CLOCK			(SYS_CLK_CTRL1  << 30 | BIT_LSYS_GPIO0_CKE)
#define APBPeriph_GPIO					(SYS_FUNC_EN1  << 30 | BIT_LSYS_GPIO0_FEN)

#define SYSCFG_CUT_VERSION_A				0
#define SYSCFG_CUT_VERSION_B				1

#define SPIC_LOWSPEED_SAMPLE_PHASE	1

extern uint8_t __image1_bss_start__[];
extern uint8_t __image1_bss_end__[];
extern FLASH_InitTypeDef flash_init_para;
extern uint32_t SPIC_CALIB_PATTERN[2];
_LONG_CALL_ void RCC_PeriphClockCmd(uint32_t APBPeriph, uint32_t APBPeriph_Clock, uint8_t NewState);
_LONG_CALL_ void PINMUX_Ctrl(uint32_t  Function, uint32_t  PinLocation, bool Operation);
_LONG_CALL_ void CPU_ClkSet(uint8_t CPU_CLOCK_SEL_VALUE);
_LONG_CALL_ void DelayUs(uint32_t us);
_LONG_CALL_ uint8_t FLASH_Init(uint8_t SpicBitMode);
_LONG_CALL_ void FLASH_StructInit(FLASH_InitTypeDef* FLASH_InitStruct);
_LONG_CALL_ uint32_t FLASH_CalibrationNewCmd(uint32_t NewStatus);
_LONG_CALL_ void FLASH_TxCmd(uint8_t cmd, uint8_t DataPhaseLen, uint8_t* pData);
_LONG_CALL_ void FLASH_RxCmd(uint8_t cmd, uint32_t read_len, uint8_t* read_data);
_LONG_CALL_ void FLASH_RxData(uint8_t cmd, uint32_t StartAddr, uint32_t read_len, uint8_t* read_data);
_LONG_CALL_ void FLASH_SetSpiMode(FLASH_InitTypeDef* FLASH_InitStruct, uint8_t SpicBitMode);
_LONG_CALL_ void FLASH_Erase(uint32_t EraseType, uint32_t Address);
_LONG_CALL_ uint32_t FLASH_ClockDiv(uint8_t Div);
_LONG_CALL_ void FLASH_Erase(uint32_t EraseType, uint32_t Address);
_LONG_CALL_ void FLASH_SetStatus(uint8_t Cmd, uint32_t Len, uint8_t* Status);
_LONG_CALL_ void FLASH_TxData256B(uint32_t StartAddr, uint32_t DataPhaseLen, uint8_t* pData);
_LONG_CALL_ void FLASH_TxData12B(uint32_t StartAddr, uint8_t DataPhaseLen, uint8_t* pData);
_LONG_CALL_ void FLASH_WaitBusy(uint32_t WaitType);
_LONG_CALL_ void FLASH_SW_CS_Control(uint8_t cs);
_LONG_CALL_ void FLASH_WriteEn(void);
_LONG_CALL_ void Cache_Enable(uint32_t Enable);
_LONG_CALL_ void Cache_Flush(void);
_LONG_CALL_ uint32_t UART_Writable(UART_TypeDef* UARTx);
_LONG_CALL_ uint32_t UART_Readable(UART_TypeDef* UARTx);
_LONG_CALL_ void UART_CharPut(UART_TypeDef* UARTx, uint8_t TxData);
_LONG_CALL_ void UART_CharGet(UART_TypeDef* UARTx, uint8_t* pRxByte);
_LONG_CALL_ void UART_SetBaud(UART_TypeDef* UARTx, uint32_t BaudRate);
_LONG_CALL_ extern uint8_t EFUSE_LogicalMap_Read(uint8_t* pbuf);
_LONG_CALL_ uint32_t SYSCFG_GetChipInfo(void);
_LONG_CALL_ uint32_t SYSCFG_ROMINFO_Get(void);
_LONG_CALL_ void RCC_PeriphClockCmd(uint32_t APBPeriph, uint32_t APBPeriph_Clock, uint8_t NewState);

#define SCB_BASE						(0xE000ED00UL)

#define SCB_CCR							(*(volatile uint32_t *)(SCB_BASE + 0x14))
#define SCB_CSSELR						(*(volatile uint32_t *)(SCB_BASE + 0x84))
#define SCB_CCSIDR						(*(volatile uint32_t *)(SCB_BASE + 0x80))
#define SCB_DCIMVAC						(*(volatile uint32_t *)(SCB_BASE + 0x25C))
#define SCB_DCCISW						(*(volatile uint32_t *)(SCB_BASE + 0x274))

#define SCB_CCR_DC_Pos					16U
#define SCB_CCR_DC_Msk					(1UL << SCB_CCR_DC_Pos)

#define SCB_DCCISW_SET_Pos				5U
#define SCB_DCCISW_SET_Msk				(0x1FFUL << SCB_DCCISW_SET_Pos)

#define SCB_DCCISW_WAY_Pos				31U
#define SCB_DCCISW_WAY_Msk				(1UL << SCB_DCCISW_WAY_Pos)

#define SCB_CCSIDR_NUMSETS_Pos			13U
#define SCB_CCSIDR_NUMSETS_Msk			(0x7FFFUL << SCB_CCSIDR_NUMSETS_Pos)

#define SCB_CCSIDR_ASSOCIATIVITY_Pos	3U
#define SCB_CCSIDR_ASSOCIATIVITY_Msk	(0x3FFUL << SCB_CCSIDR_ASSOCIATIVITY_Pos)
#define CCSIDR_WAYS(x)					(((x) & SCB_CCSIDR_ASSOCIATIVITY_Msk) >> SCB_CCSIDR_ASSOCIATIVITY_Pos)
#define CCSIDR_SETS(x)					(((x) & SCB_CCSIDR_NUMSETS_Msk      ) >> SCB_CCSIDR_NUMSETS_Pos      )

__attribute__((always_inline)) static inline void DSB(void)
{
	__asm volatile ("dsb");
}

__attribute__((always_inline)) static inline void ISB(void)
{
	__asm volatile ("isb");
}

static inline void SCB_InvalidateDCache(void)
{
	uint32_t ccsidr;
	uint32_t sets;
	uint32_t ways;

	SCB_CSSELR = 0U;
	DSB();

	ccsidr = SCB_CCSIDR;

	sets = CCSIDR_SETS(ccsidr);
	do
	{
		ways = CCSIDR_WAYS(ccsidr);

		do
		{
			SCB_DCCISW = ((sets << SCB_DCCISW_SET_Pos) & SCB_DCCISW_SET_Msk) | ((ways << SCB_DCCISW_WAY_Pos) & SCB_DCCISW_WAY_Msk);

		} while(ways-- != 0U);

	} while(sets-- != 0U);

	DSB();
	ISB();
}

static inline void SCB_InvalidateDCache_by_Addr(uint32_t* addr, int32_t dsize)
{
	int32_t op_size = dsize;
	uint32_t op_addr = (uint32_t)addr;
	int32_t linesize = 32;

	DSB();

	while(op_size > 0)
	{
		SCB_DCIMVAC = op_addr;
		op_addr += (uint32_t)linesize;
		op_size -= linesize;
	}

	DSB();
	ISB();
}

static inline void DCache_Invalidate(uint32_t Address, uint32_t Bytes)
{
	uint32_t addr = Address, len = Bytes;

	if((Address == 0xFFFFFFFF) && (Bytes == 0xFFFFFFFF))
	{
		SCB_InvalidateDCache();
	}
	else
	{
		if((addr & 0x1F) != 0)
		{
			addr = (Address >> 5) << 5;   //32-byte aligned
			len = ((((Address + Bytes - 1) >> 5) + 1) << 5) - addr; //next 32-byte aligned
		}

		SCB_InvalidateDCache_by_Addr((uint32_t*)addr, len);
	}
}

#endif
