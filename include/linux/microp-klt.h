/**
 * These are combined into a bitmask to turn individual LEDs on
 */

 #ifndef _MICROP_KLT_H
#define _MICROP_KLT_H

#include <linux/leds.h>
#include <linux/earlysuspend.h>
enum microp_led_t {
	MICROP_KLT_LED_HOME,	// 0x01
	MICROP_KLT_LED_BACK,	// 0x02
	MICROP_KLT_LED_END,	// 0x04
	MICROP_KLT_LED_SEND,	// 0x08
	MICROP_KLT_LED_ACTION,	// 0x10
	MICROP_KLT_BKL_LCD = 13,// 0x2000
	MICROP_KLT_BKL_KBD = 14,// 0x4000
	MICROP_KLT_LED_CNT = 7
};
#define MICROP_KLT_LEDS_OFF   0x00

struct microp_spi_table {
	uint16_t value1;
	uint16_t value2;
	uint16_t value3;
	uint16_t delay;
};

#define BMA150_WROP_BUF	30

static struct microp_klt {
	struct i2c_client *client;
	struct mutex lock;
	u16 led_states;
	u8 misc_states;
	unsigned short version;
	struct led_classdev leds[MICROP_KLT_LED_CNT];
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
	uint8_t enable_early_suspend;
#endif
}*micropklt_t = 0;


/**
 * These behaviors are repeated approximately every 5 seconds
 * These cannot be combined
 */
#define MICROP_KLT_SYSLED_NONE        0x0 // No predefined behavior
#define MICROP_KLT_SYSLED_RING      0x100 // Action and Send LEDs flash quickly
#define MICROP_KLT_SYSLED_BLINK     0x200 // Single flash of all 4 action LEDs
#define MICROP_KLT_SYSLED_BREATHE   0x400 // Bottom-to-top fade on action LEDs
#define MICROP_KLT_SYSLED_FADE      0x500 // Slow fade on/off of action LEDs
#define MICROP_KLT_SYSLED_ROTATE    0x800 // Counter-clockwise rotate of action LEDs
#define MICROP_KLT_SYSLED_VERTICAL 0x1000 // Top and bottom action LEDs flash twice

// Default state is LCD backlight on, LEDs off
#define MICROP_KLT_DEFAULT_LED_STATES ( (1U << MICROP_KLT_BKL_LCD) | MICROP_KLT_LEDS_OFF)
#define MICROP_KLT_ALL_LEDS	0xffff

/** micropklt misc register bit flags
 */
#define MISC_CAP_SEN_RES_CTRL1	0x02
#define MISC_CAP_SEN_RES_CTRL2	0x40
#define MISC_MICP_ISP			0x80

/**
 * I2C data address IDs
 */
#define MICROP_KLT_ID_VERSION           0x30 // Chip revision
#define MICROP_KLT_ID_LED_STATE         0x40 // Set LED behavior using above bitmasks
#define MICROP_KLT_ID_LCD_BRIGHTNESS    0x22 // Set brightness of LCD backlight
#define MICROP_KLT_ID_LCD_BRIGHTNESS2   0x24 // Set brightness of LCD backlight (for rhod)
#define MICROP_KLT_ID_GET_LCD_BRHTNS 	0x32 // Get light sensor's result
#define MICROP_KLT_ID_LIGHT_SENSOR	0x33 // Get light sensor's result
#define MICROP_KLT_ID_SPICTRL		0x21 // SPI Controll Topaz
#define MICROP_KLT_ID_SPILCMDDATA	0x70 // SPI Data Topaz
// Kovsky needs special addresses. 
#define MICROP_KLT_ID_VERSION_KOVS           0x07 // Chip revision
#define MICROP_KLT_ID_LED_STATE_KOVS         0x20 // Set LED behavior using above bitmasks
#define MICROP_KLT_ID_LCD_BRIGHTNESS_KOVS    0x12 // Set brightness of LCD backlight
#define MICROP_KLT_ID_KEYPAD_BRIGHTNESS_KOVS	0x14 // Set brightness of front keypad


//Mahimahi's constants names
#define MICROP_I2C_WCMD_MISC				0x20
#define MICROP_I2C_WCMD_SPI_EN				0x21
#define MICROP_I2C_WCMD_AUTO_BL_CTL			0x23
#define MICROP_I2C_RCMD_SPI_BL_STATUS			0x24
#define MICROP_I2C_RCMD_VERSION				0x30
#define MICROP_I2C_WCMD_LCM_REGISTER			0x70
#define MICROP_I2C_RCMD_GPI_STATUS			0x83

#define IRQ_GSENSOR	(1<<10)
#define IRQ_LSENSOR  	(1<<9)
#define IRQ_REMOTEKEY	(1<<7)
#define IRQ_HEADSETIN	(1<<2)
#define IRQ_SDCARD	(1<<0)

extern int micropklt_set_led_states(unsigned leds_mask, unsigned leds_values);
extern int micropklt_set_lcd_state(int on);
extern int micropklt_set_kbd_state(int on);

#endif
