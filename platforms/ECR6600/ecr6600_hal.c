#include "../hal_generic.h"
#include <nds32_intrinsic.h>

unsigned int g_efuse_ctrl = 0;
E_SPI_MODE spi_mode = SPI_MODE_STANDARD;

static int spi_cmd_none(uint8_t cmd)
{
	if(!spi_bus_ready(SOC_SPI0_BASE)) return -1;
	if(spi_fifo_reset(SOC_SPI0_BASE)) return -2;

	WRITE_REG32(SOC_SPI0_BASE + 0x20, SPI_TRANSCTRL_CMD_EN | SPI_TRANSCTRL_TRAMODE_NONE);
	WRITE_REG32(SOC_SPI0_BASE + 0x24, cmd);

	return 0;
}

static uint32_t spi_read_cmd(uint8_t cmd, uint32_t len)
{
	uint32_t enc = READ_REG32(SOC_PD_APB_ENCRYPT_EN);
	WRITE_REG32(SOC_PD_APB_ENCRYPT_EN, APB_WRIT_NOT_ENCRYPT);
	if(!spi_bus_ready(SOC_SPI0_BASE)) goto fail;
	if(spi_fifo_reset(SOC_SPI0_BASE)) goto fail;
	WRITE_REG32(SOC_SPI0_BASE + 0x20, SPI_TRANSCTRL_CMD_EN | SPI_TRANSCTRL_TRAMODE_RO | SPI_TRANSCTRL_RCNT(len - 1));
	WRITE_REG32(SOC_SPI0_BASE + 0x24, cmd);
	if(!spi_rx_ready(SOC_SPI0_BASE))  goto fail;
	WRITE_REG32(SOC_PD_APB_ENCRYPT_EN, enc);
	return READ_REG32(SOC_SPI0_BASE + 0x2c);

fail:
	WRITE_REG32(SOC_PD_APB_ENCRYPT_EN, enc);
	return -1;
}

static void flash_write_enable(void)
{
	while(1)
	{
		spi_cmd_none(SPIFLASH_CMD_WREN);
		if(spi_read_cmd(SPIFLASH_CMD_RDSR_1, 1) & SPIFLASH_STATUS_WEL) break;
	}
}

static uint8_t flash_status(void)
{
	return (uint8_t)spi_read_cmd(SPIFLASH_CMD_RDSR_1, 1);
}

static void flash_wait_busy(void)
{
	while(flash_status() & SPIFLASH_STATUS_WIP);
}

static void flash_write_status(uint32_t cmd, uint32_t value, int len)
{
	unsigned int enc = READ_REG32(SOC_PD_APB_ENCRYPT_EN);
	WRITE_REG32(SOC_PD_APB_ENCRYPT_EN, APB_WRIT_NOT_ENCRYPT);
	flash_write_enable();
	WRITE_REG32(SOC_SPI0_BASE + 0x20, SPI_TRANSCTRL_CMD_EN | SPI_TRANSCTRL_TRAMODE_WO | SPI_TRANSCTRL_WCNT(len - 1));
	WRITE_REG32(SOC_SPI0_BASE + 0x24, cmd);
	WRITE_REG32(SOC_SPI0_BASE + 0x2c, value);
	flash_wait_busy();
	WRITE_REG32(SOC_PD_APB_ENCRYPT_EN, enc);
}

static int flash_erase_cmd(uint32_t addr, uint8_t cmd)
{
	flash_write_enable();
	if(!spi_bus_ready(SOC_SPI0_BASE)) return -1;
	if(spi_fifo_reset(SOC_SPI0_BASE)) return -2;
	WRITE_REG32(SOC_SPI0_BASE + 0x28, addr);
	WRITE_REG32(SOC_SPI0_BASE + 0x20, SPI_TRANSCTRL_CMD_EN | SPI_TRANSCTRL_ADDR_EN | SPI_TRANSCTRL_TRAMODE_NONE);
	WRITE_REG32(SOC_SPI0_BASE + 0x24, cmd);
	flash_wait_busy();
	return 0;
}

static int spi_otp_read_region(uint32_t cmd, uint32_t addr, uint32_t len, uint8_t* pdata)
{
	if(!spi_bus_ready(SOC_SPI0_BASE))
		return -1;

	if(spi_fifo_reset(SOC_SPI0_BASE))
		return -2;

	WRITE_REG32(SOC_SPI0_BASE + 0x20,
		SPI_TRANSCTRL_CMD_EN |
		SPI_TRANSCTRL_ADDR_EN |
		SPI_TRANSCTRL_TRAMODE_RO |
		SPI_TRANSCTRL_DUALQUAD_REGULAR |
		SPI_TRANSCTRL_RCNT(len - 1));

	WRITE_REG32(SOC_SPI0_BASE + 0x28, addr);
	WRITE_REG32(SOC_SPI0_BASE + 0x24, cmd);

	uint32_t words = len / 4;
	uint32_t* dst = (uint32_t*)pdata;

	for(uint32_t i = 0; i < words; i++)
	{
		while(READ_REG32(SOC_SPI0_BASE + 0x34) & SPI_STATUS_RXENPTY);

		dst[i] = READ_REG32(SOC_SPI0_BASE + 0x2c);
	}

	return 0;
}

void uart_putc(uint8_t b)
{
	WRITE_REG32(UARTC_THR_REG, b);
	while(!(REG32(UARTC_LSR_REG) & UARTC_LSR_TEMT));
}

int8_t uart_getc_timeout(uint8_t* out, uint32_t loops)
{
	while(loops--)
	{
		if(uart_data_tstc(SOC_UART0_BASE) != 0)
		{
			uart_getc(SOC_UART0_BASE, out, 1);
			return 1;
		}
	}
	return 0;
}

void uart_set_baud(uint32_t baud)
{
	uint32_t uclk = 80000000;
	uint32_t value = REG32(UARTC_LCR_REG);

	uint32_t bestOsc = 0;
	uint32_t bestDiv = 0;

	if(baud == 115200)
	{
		bestOsc = 8;
		bestDiv = 87;
		goto done;
	}

	for(uint32_t osc = 32; osc > 0; osc--)
	{
		uint32_t div = uclk / (osc * baud);

		if(div == 0) continue;

		for(int32_t d = -1; d <= 1; d++)
		{
			uint32_t testDiv = div + d;
			if(testDiv == 0) continue;

			uint32_t actual = uclk / (osc * testDiv);
			uint32_t err = (actual > baud) ? actual - baud : baud - actual;

			if(err * 100 < baud * 5)
			{
				bestOsc = osc;
				bestDiv = testDiv;
				goto done;
			}
		}
	}

done:
	WRITE_REG32(UARTC_OSCR_REG, bestOsc);
	WRITE_REG32(UARTC_LCR_REG, value | 0x80);
	WRITE_REG32(UARTC_DLL_REG, bestDiv & 0xff);
	WRITE_REG32(UARTC_DLM_REG, (bestDiv >> 8) & 0xff);
	WRITE_REG32(UARTC_LCR_REG, value & ~0x80);
}

void nds32_dcache_invalidate(void)
{
	__nds32__cctl_l1d_invalall();
	__nds32__msync_store();
	__nds32__dsb();
}

void flash_program_page(uint32_t off, const uint8_t* data)
{
	flash_write_enable();

	if(!spi_bus_ready(SOC_SPI0_BASE)) return;
	if(spi_fifo_reset(SOC_SPI0_BASE)) return;

	if(spi_mode == SPI_MODE_QUAD)
	{
		WRITE_REG32(SOC_SPI0_BASE + 0x20, SPI_TRANSCTRL_CMD_EN | SPI_TRANSCTRL_ADDR_EN | SPI_TRANSCTRL_TRAMODE_WO | SPI_TRANSCTRL_DUALQUAD_QUAD | SPI_TRANSCTRL_WCNT(FLASH_PAGE_SIZE - 1));
		WRITE_REG32(SOC_SPI0_BASE + 0x24, SPIFLASH_CMD_QUAD_INPUT_PP);
	}
	else
	{
		WRITE_REG32(SOC_SPI0_BASE + 0x20, SPI_TRANSCTRL_CMD_EN | SPI_TRANSCTRL_ADDR_EN | SPI_TRANSCTRL_TRAMODE_WO | SPI_TRANSCTRL_WCNT(FLASH_PAGE_SIZE - 1));
		WRITE_REG32(SOC_SPI0_BASE + 0x24, SPIFLASH_CMD_PP);
	}

	uint32_t words = (FLASH_PAGE_SIZE + 3) / 4;

	for(uint32_t i = 0; i < words; i++)
	{
		while(READ_REG32(SOC_SPI0_BASE + 0x34) & SPI_STATUS_TXFULL);
		WRITE_REG32(SOC_SPI0_BASE + 0x28, off + i * 4);
		WRITE_REG32(SOC_SPI0_BASE + 0x2c, ((uint32_t*)data)[i]);
	}

	flash_wait_busy();
	nds32_dcache_invalidate();
}

int flash_erase_range(uint32_t src, uint32_t len)
{
	if((src & 0x3) != 0)
		return 0;
	if(len == 0)
		return 0;

	uint32_t cur = src;
	uint32_t end = src + len;

	while(cur < end)
	{
		if((cur % 0x10000U == 0) && (end - cur >= 0x10000U))
		{
			flash_erase_cmd(cur, SPIFLASH_CMD_BE);
			cur += 0x10000U;
		}
		else
		{
			flash_erase_cmd(cur, SPIFLASH_CMD_SE);
			cur += 0x1000U;
		}
	}

	nds32_dcache_invalidate();

	return 1;
}

int flash_erase_chip()
{
	flash_write_enable();
	spi_cmd_none(SPIFLASH_CMD_CE);
	flash_wait_busy();
	nds32_dcache_invalidate();
	return 1;
}

void flash_init(void)
{
	WRITE_REG32(SOC_PD_SPI_CONFIG, REG32(SOC_PD_SPI_CONFIG) | 0x80);
	OR_REG32(SOC_PD_CLK_EN_BASE0, SPI0_APB_CLK_BIT | FLASH_AHB_CLK_BIT);
	WRITE_REG32(SOC_PD_SMU_BASE + 0x18, 0x80);

	PIN_FUNC_SET(IO_MUX0_GPIO7, FUNC_GPIO7_MSPI_MOSI);
	PIN_FUNC_SET(IO_MUX1_GPIO8, FUNC_GPIO8_MSPI_HOLD);
	PIN_FUNC_SET(IO_MUX1_GPIO9, FUNC_GPIO9_MSPI_CLK);
	PIN_FUNC_SET(IO_MUX1_GPIO10, FUNC_GPIO10_MSPI_CS0);
	PIN_FUNC_SET(IO_MUX1_GPIO11, FUNC_GPIO11_MSPI_MISO);
	PIN_FUNC_SET(IO_MUX1_GPIO12, FUNC_GPIO12_MSPI_WP);

	spi_cmd_none(SPIFLASH_STATUS_RSTEN);
	spi_cmd_none(SPIFLASH_STATUS_RST);
	udelay(4 * 1000);
	WRITE_REG32(SOC_SPI0_BASE + 0x40, (READ_REG32(SOC_SPI0_BASE + 0x40) & 0xffffff00) | 0xFF);
	WRITE_REG32(SOC_SPI0_BASE + 0x50, (READ_REG32(SOC_SPI0_BASE + 0x50) & 0xf) | SPIFLASH_READ_CMD_EB);

	flash_id = spi_read_cmd(SPIFLASH_CMD_RDID, 3) & 0xFFFFFF;

	if(flash_id != 0xFFFFFF)
	{
		switch(flash_id)
		{
			case 0x16701c:
			case 0x16301c:
			case 0x15701c:
				break;
			case 0x146085:
			case 0x1440c8:
			case 0x1540c8:
			case 0x15400b:
			case 0x14400b:
				flash_write_status(SPIFLASH_CMD_WRSR, SPIFLASH_STATUS_QE, 2);
				break;
			case 0x1560eb:
				flash_write_status(SPIFLASH_CMD_WRSR, 0x0218, 2);
				break;
			case 0x156085:
				flash_write_status(SPIFLASH_CMD_WRSR, SPIFLASH_STATUS_QE, 2);
				flash_write_status(SPIFLASH_CMD_WRSR_2, SPIFLASH_STATUS_2_QE, 1);
				break;
			case 0x1820c2:
			case 0x1720c2:
				flash_write_status(SPIFLASH_CMD_WRSR, 0x40, 2);
				break;
			default:
				flash_write_status(SPIFLASH_CMD_WRSR_2, SPIFLASH_STATUS_2_QE, 1);
				break;
		}
		spi_mode = SPI_MODE_QUAD;
	}
	else
	{
		spi_mode = SPI_MODE_STANDARD;
	}
}

int crc32_memory(uint32_t addr, uint32_t len, uint32_t* result)
{
	*result = crc32(0, (void*)addr, len);
	return 1;
}

int read_efuse(void)
{
	efuse_read_series(0, (unsigned int*)cmd_buf, 0x80 / 4);
	return 0x80;
}

int sha256_memory_hardware(uint32_t addr, uint32_t len)
{
	sha256((void*)addr, len, cmd_buf);
	return 1;
}

void boot_soc_update_cpu_clk(void)
{
	unsigned int val;
	g_efuse_ctrl = efuse_read(EFUSE_CTRL_OFFSET);
	OR_REG32(SOC_PD_CLK_EN_BASE0, RF_REGF_APB_CLK_BIT | FREF_CLK_EN_BIT);

	if((g_efuse_ctrl & EFUSE_CRYSTAL_OFFSET) == EFUSE_CRYSTAL_40M)
	{
		val = READ_REG32(SOC_RFC_DIG_PLL3);
		val = (val & 0xFFFFFF0F) | 0x60;
		WRITE_REG32(SOC_RFC_DIG_PLL3, val);
	}
	else
	{
		val = READ_REG32(SOC_RFC_DIG_PLL3);
		val = (val & 0xFFFF7F0F) | 0x60;
		WRITE_REG32(SOC_RFC_DIG_PLL3, val);
		val = READ_REG32(SOC_RFC_DIG_PLL2);
		val = (val & (~(0x7F << 14))) | (0x25 << 14);
		WRITE_REG32(SOC_RFC_DIG_PLL2, val);
		WRITE_REG32(SOC_DIG_PLL_REG13, 0x49);
		WRITE_REG32(SOC_DIG_PLL_REG14, 0x6C4EC5);
		WRITE_REG32(SOC_DIG_PLL_REG20, 0x49E);
	}

	val = READ_REG32(SOC_RFC_DIG_PLL1);
	val |= 1 << 13;
	WRITE_REG32(SOC_RFC_DIG_PLL1, val);

	udelay(10);

	WRITE_REG32(SOC_DIG_PLL_REG18, 1);

	udelay(10);

	val = READ_REG32(SOC_RFC_DIG_PLL3);
	val &= ~((1 << 14) | 0xF);
	WRITE_REG32(SOC_RFC_DIG_PLL3, val);

	WRITE_REG32(SOC_DIG_PLL_REG18, 0);

	udelay(5);

	val = READ_REG32(SOC_RFC_DIG_PLL2);
	val |= 1 << 27;
	WRITE_REG32(SOC_RFC_DIG_PLL2, val);

	val = READ_REG32(SOC_DIG_PLL_REG12);
	val |= 1;
	WRITE_REG32(SOC_DIG_PLL_REG12, val);

	udelay(20);

	val = READ_REG32(SOC_RFC_DIG_PLL3);
	val |= 1 << 23;
	WRITE_REG32(SOC_RFC_DIG_PLL3, val);

	WRITE_REG32(0x00202074, 0x6);

	udelay(1);

	efuse_cfg_160m();
	WRITE_REG32(SOC_PD_CLK_MUX_BASE, (READ_REG32(SOC_PD_CLK_MUX_BASE) & 0xFFFFFFF0) | CPU_CLK_FREQ_DIV0 | CPU_CLK_SEC_160M);
	WRITE_REG32(SOC_PD_UART_CLK_SEL, UART_CLK_SEL_160M_DIV2);
	udelay(1);
}

uint8_t get_chip_data(void)
{
	WRITE_REG32(cmd_buf, 0x4C7959C9); // ECR6600
	return 4;
}

uint32_t hal_read_otp(uint32_t otp_block_size, uint32_t otp_block_count, uint32_t otp_interval, uint32_t otp_start_addr, uint32_t otp_mode)
{
	uint32_t out_offset = 0;
	for(uint32_t blk = 0; blk < otp_block_count; ++blk)
	{
		uint32_t addr = otp_start_addr + blk * otp_interval;
		uint32_t remain = otp_block_size;

		while(remain > 0)
		{
			uint32_t len = remain > 256 ? 256 : remain;
			if(otp_mode)
				spi_otp_read_region(0xEB, addr, len, cmd_buf + out_offset);
			else
				spi_otp_read_region(SPIFLASH_CMD_OTP_RD, addr, len, cmd_buf + out_offset);
			addr += len;
			out_offset += len;
			remain -= len;
		}
	}
	return out_offset;
}

void hal_spi_cmd(uint8_t cmd)
{
	spi_cmd_none(cmd);
}

void boot_from_flash()
{
#define CHIP_SMU_PD_CLK_UART_TRNG_80M_EN		(1<<2)
#define CHIP_SMU_PD_CLK_DIV3_EN					(1<<1)
#define CHIP_SMU_PD_CLK_DIV2_EN					(1<<0)
	extern int flash_boot(int uart_boot_check);
	WRITE_REG32(SOC_PD_CLK_DIV_EN, (READ_REG32(SOC_PD_CLK_DIV_EN) | CHIP_SMU_PD_CLK_DIV2_EN | CHIP_SMU_PD_CLK_DIV3_EN) & ~CHIP_SMU_PD_CLK_UART_TRNG_80M_EN);
	WRITE_REG32(SOC_PD_CLK_MUX_BASE, (READ_REG32(SOC_PD_CLK_MUX_BASE) & 0xFFFFFFF0) | CPU_CLK_FREQ_DIV0 | CPU_CLK_SEC_40M_26M);
	flash_boot(0);
}

extern int main(void);

int boot_main(void)
{
	extern uint8_t __bss_start__[];
	extern uint8_t __bss_end__[];
	memset((void*)__bss_start__, 0, (__bss_end__ - __bss_start__));
	boot_soc_update_cpu_clk();
	//__nds32__enable_unaligned();
	main();
}
