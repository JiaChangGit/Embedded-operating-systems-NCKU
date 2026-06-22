#include "lcd16x2_i2c.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define LCD_CLEARDISPLAY   0x01U
#define LCD_DISPLAYCONTROL 0x08U
#define LCD_FUNCTIONSET    0x20U
#define LCD_ENTRY_ID       0x02U
#define LCD_DISPLAY_B      0x01U
#define LCD_DISPLAY_C      0x02U
#define LCD_DISPLAY_D      0x04U
#define LCD_FUNCTION_N     0x08U

#define LCD_RS       (1U << 0)
#define LCD_EN       (1U << 2)
#define LCD_BK_LIGHT (1U << 3)

/* STM32 HAL 使用左移一位後的位址：0x27 -> 0x4E、0x3F -> 0x7E。 */
#define LCD_I2C_SLAVE_ADDRESS_0 0x4EU
#define LCD_I2C_SLAVE_ADDRESS_1 0x7EU

static I2C_HandleTypeDef *lcd_i2c_handle;
static uint8_t lcd_i2c_address;
static bool lcd_bus_available;

static void lcd_delay_ms(uint32_t delay_ms) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  } else {
    HAL_Delay(delay_ms);
  }
}

static bool lcd_transmit(uint8_t *data, uint16_t size) {
  if (!lcd_bus_available || (lcd_i2c_handle == NULL)) {
    return false;
  }
  if (HAL_I2C_Master_Transmit(lcd_i2c_handle, lcd_i2c_address, data, size,
                              50U) != HAL_OK) {
    lcd_bus_available = false;
    return false;
  }
  return true;
}

static void lcd_send_nibble(uint8_t nibble, bool data_mode) {
  uint8_t control = data_mode ? LCD_RS : 0U;
  uint8_t i2c_data[2] = {
      (uint8_t)((nibble & 0xF0U) | LCD_EN | LCD_BK_LIGHT | control),
      (uint8_t)((nibble & 0xF0U) | LCD_BK_LIGHT | control),
  };
  (void)lcd_transmit(i2c_data, sizeof(i2c_data));
}

static void lcd_send_command(uint8_t command) {
  const uint8_t low_nibble = (uint8_t)(0xF0U & (command << 4));
  const uint8_t high_nibble = (uint8_t)(0xF0U & command);
  uint8_t i2c_data[4] = {
      (uint8_t)(high_nibble | LCD_EN | LCD_BK_LIGHT),
      (uint8_t)(high_nibble | LCD_BK_LIGHT),
      (uint8_t)(low_nibble | LCD_EN | LCD_BK_LIGHT),
      (uint8_t)(low_nibble | LCD_BK_LIGHT),
  };
  (void)lcd_transmit(i2c_data, sizeof(i2c_data));
}

static void lcd_send_data(uint8_t data) {
  const uint8_t low_nibble = (uint8_t)(0xF0U & (data << 4));
  const uint8_t high_nibble = (uint8_t)(0xF0U & data);
  uint8_t i2c_data[4] = {
      (uint8_t)(high_nibble | LCD_EN | LCD_BK_LIGHT | LCD_RS),
      (uint8_t)(high_nibble | LCD_BK_LIGHT | LCD_RS),
      (uint8_t)(low_nibble | LCD_EN | LCD_BK_LIGHT | LCD_RS),
      (uint8_t)(low_nibble | LCD_BK_LIGHT | LCD_RS),
  };
  (void)lcd_transmit(i2c_data, sizeof(i2c_data));
}

bool lcd16x2_i2c_init(I2C_HandleTypeDef *i2c_handle) {
  if (i2c_handle == NULL) {
    return false;
  }
  lcd_i2c_handle = i2c_handle;
  lcd_bus_available = false;
  lcd_delay_ms(50U);

  if (HAL_I2C_IsDeviceReady(lcd_i2c_handle, LCD_I2C_SLAVE_ADDRESS_0, 5U,
                            100U) == HAL_OK) {
    lcd_i2c_address = LCD_I2C_SLAVE_ADDRESS_0;
  } else if (HAL_I2C_IsDeviceReady(lcd_i2c_handle, LCD_I2C_SLAVE_ADDRESS_1, 5U,
                                   100U) == HAL_OK) {
    lcd_i2c_address = LCD_I2C_SLAVE_ADDRESS_1;
  } else {
    return false;
  }
  lcd_bus_available = true;

  lcd_delay_ms(45U);
  /*
   * HD44780 上電時仍在 8-bit mode，前三次 0x3 與切換 0x2 必須只送
   * high nibble；若用一般 4-bit command routine 會多送一個 0x0。
   */
  lcd_send_nibble(0x30U, false);
  lcd_delay_ms(5U);
  lcd_send_nibble(0x30U, false);
  lcd_delay_ms(1U);
  lcd_send_nibble(0x30U, false);
  lcd_delay_ms(8U);
  lcd_send_nibble(0x20U, false);
  lcd_delay_ms(8U);
  lcd_send_command(LCD_FUNCTIONSET | LCD_FUNCTION_N);
  lcd_delay_ms(1U);
  lcd_send_command(LCD_DISPLAYCONTROL);
  lcd_delay_ms(1U);
  lcd_send_command(LCD_CLEARDISPLAY);
  lcd_delay_ms(3U);
  lcd_send_command(0x04U | LCD_ENTRY_ID);
  lcd_delay_ms(1U);
  lcd_send_command(LCD_DISPLAYCONTROL | LCD_DISPLAY_D);
  lcd_delay_ms(3U);
  return lcd_bus_available;
}

void lcd16x2_i2c_setCursor(uint8_t row, uint8_t column) {
  uint8_t address = (uint8_t)(column & 0x0FU);
  address |= (row == 0U) ? 0x80U : 0xC0U;
  lcd_send_command(address);
}

void lcd16x2_i2c_1stLine(void) { lcd16x2_i2c_setCursor(0U, 0U); }

void lcd16x2_i2c_2ndLine(void) { lcd16x2_i2c_setCursor(1U, 0U); }

void lcd16x2_i2c_TwoLines(void) {
  lcd_send_command(LCD_FUNCTIONSET | LCD_FUNCTION_N);
}

void lcd16x2_i2c_OneLine(void) { lcd_send_command(LCD_FUNCTIONSET); }

void lcd16x2_i2c_cursorShow(bool state) {
  lcd_send_command(state ? (LCD_DISPLAYCONTROL | LCD_DISPLAY_B | LCD_DISPLAY_C |
                            LCD_DISPLAY_D)
                         : (LCD_DISPLAYCONTROL | LCD_DISPLAY_D));
}

void lcd16x2_i2c_clear(void) {
  lcd_send_command(LCD_CLEARDISPLAY);
  lcd_delay_ms(3U);
}

void lcd16x2_i2c_display(bool state) {
  lcd_send_command(state ? (LCD_DISPLAYCONTROL | LCD_DISPLAY_B | LCD_DISPLAY_C |
                            LCD_DISPLAY_D)
                         : (LCD_DISPLAYCONTROL | LCD_DISPLAY_B | LCD_DISPLAY_C));
}

void lcd16x2_i2c_shiftRight(uint8_t offset) {
  for (uint8_t index = 0U; index < offset; ++index) {
    lcd_send_command(0x1CU);
  }
}

void lcd16x2_i2c_shiftLeft(uint8_t offset) {
  for (uint8_t index = 0U; index < offset; ++index) {
    lcd_send_command(0x18U);
  }
}

void lcd16x2_i2c_printf(const char *format, ...) {
  char text[20];
  va_list arguments;

  if (format == NULL) {
    return;
  }
  va_start(arguments, format);
  (void)vsnprintf(text, sizeof(text), format, arguments);
  va_end(arguments);
  for (size_t index = 0U; (index < strlen(text)) && (index < 16U); ++index) {
    lcd_send_data((uint8_t)text[index]);
  }
}

void lcd16x2_i2c_write_line(uint8_t row, const char *text) {
  char padded_line[17];
  size_t copy_length = 0U;

  memset(padded_line, ' ', 16U);
  padded_line[16] = '\0';
  if (text != NULL) {
    copy_length = strlen(text);
    if (copy_length > 16U) {
      copy_length = 16U;
    }
    memcpy(padded_line, text, copy_length);
  }
  lcd16x2_i2c_setCursor(row, 0U);
  for (size_t index = 0U; index < 16U; ++index) {
    lcd_send_data((uint8_t)padded_line[index]);
  }
}
