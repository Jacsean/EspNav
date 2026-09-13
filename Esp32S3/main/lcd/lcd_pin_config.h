#pragma once
/* ===== 集成板 LCD 引脚配置（唯一集中点，按厂商资料/丝印修正这里即可） =====
 * 配置 A（当前候选）：FT6336G 常见搭档 = ILI9341V 类集成板
 *   参考同类板（XPSTEM / BruceDevices 支持型号）：CS=10 DC=46 SCLK=12 MOSI=11 MISO=13 BL=45
 *   RST 未接 GPIO（板上接 EN），使用软复位序列。
 * ⚠ 本文件未按实物确认：若点亮失败，优先核对/替换这里。
 */
#include "driver/spi_master.h"

#define LCD_SPI_HOST        SPI2_HOST
#define LCD_PIN_SCLK        12
#define LCD_PIN_MOSI        11
#define LCD_PIN_MISO        13
#define LCD_PIN_CS          10
#define LCD_PIN_DC          46
#define LCD_PIN_RST         (-1)      /* -1 = 无 GPIO 复位（板上接 EN），走软复位 */
#define LCD_PIN_BL          45
#define LCD_SPI_HZ          (20 * 1000 * 1000)
#define LCD_BL_ACTIVE_HIGH  1         /* 背光高有效；若反了改 0 */
#define LCD_INVERT          1         /* 1=发送 INVON(0x21)；若某板不需反相改 0(发 INVOFF) */
#define LCD_W               320
#define LCD_H               240
