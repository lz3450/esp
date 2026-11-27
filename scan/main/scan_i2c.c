/*
 * I2C bus scanner.
 *
 * Probes every 7-bit address on the configured bus and, for known chips, reads
 * the identification register to confirm the part. The M5Stack Fire's internal
 * I2C bus (SDA=21, SCL=22) is shared by the onboard IMU, magnetometer, PMIC and
 * the Grove A connector, so external Grove I2C units show up in the same scan.
 */

#include "scan_i2c.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "scan.i2c";

#define I2C_PROBE_TIMEOUT_MS 50

typedef struct
{
    uint8_t     addr;
    const char *name;
} i2c_known_t;

static const i2c_known_t i2c_known_devices[] = {
    {0x0D,                                           "QMC5883L magnetometer"},
    {0x10,                                             "BMM150 magnetometer"},
    {0x11,                         "BMM150 magnetometer (alternate address)"},
    {0x18,                                            "LIS3DH accelerometer"},
    {0x1E,                                  "HMC5883L / LSM303 magnetometer"},
    {0x23,                                             "BH1750 light sensor"},
    {0x29,                              "VL53L0X ToF / TSL2561 light sensor"},
    {0x38,                     "AHT10 / AHT20 temperature + humidity sensor"},
    {0x3C,                                           "SSD1306 / SH1106 OLED"},
    {0x3D,                       "SSD1306 / SH1106 OLED (alternate address)"},
    {0x44,                     "SHT3x / SHT4x temperature + humidity sensor"},
    {0x45, "SHT3x / SHT4x temperature + humidity sensor (alternate address)"},
    {0x53,                                           "ADXL345 accelerometer"},
    {0x57,                                         "MAX30102 pulse oximeter"},
    {0x5A,                                   "MLX90614 infrared thermometer"},
    {0x5C,                         "BH1750 light sensor (alternate address)"},
    {0x68,           "MPU6886 / MPU6050 / MPU9250 inertial measurement unit"},
    {0x69, "MPU6886 / MPU6050 inertial measurement unit (alternate address)"},
    {0x75,                            "IP5306 power management IC (onboard)"},
    {0x76,               "BME280 / BMP280 / BMP388 / MS5611 pressure sensor"},
    {0x77,             "BME280 / BMP280 pressure sensor (alternate address)"},
};

static const char *lookup_device_name(uint8_t addr)
{
    for (size_t i = 0; i < sizeof(i2c_known_devices) / sizeof(i2c_known_devices[0]); i++)
    {
        if (i2c_known_devices[i].addr == addr)
        {
            return i2c_known_devices[i].name;
        }
    }

    if (addr >= 0x20 && addr <= 0x27)
    {
        return "PCF8574 I/O expander";
    }
    if (addr >= 0x40 && addr <= 0x47)
    {
        return "HDC1080 / Si7021 / INA219 / PCA9685";
    }
    if (addr >= 0x48 && addr <= 0x4B)
    {
        return "ADS1115 ADC / LM75 temperature sensor";
    }
    return "unidentified device";
}

static bool read_register(i2c_master_bus_handle_t bus, uint8_t addr, uint8_t reg, uint8_t *value)
{
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = CONFIG_SCAN_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;

    if (i2c_master_bus_add_device(bus, &device_config, &dev) != ESP_OK)
    {
        return false;
    }

    const esp_err_t err = i2c_master_transmit_receive(dev, &reg, sizeof(reg), value, sizeof(*value), I2C_PROBE_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    return err == ESP_OK;
}

static void identify_device(i2c_master_bus_handle_t bus, uint8_t addr)
{
    uint8_t id = 0;

    switch (addr)
    {
    case 0x10:
    case 0x11:
        if (read_register(bus, addr, 0x40, &id))
        {
            ESP_LOGI(TAG, "             chip id 0x%02X %s", (unsigned)id, (id == 0x32) ? "(BMM150 confirmed)" : "(BMM150 expects 0x32)");
        }
        break;

    case 0x68:
    case 0x69:
        if (read_register(bus, addr, 0x75, &id))
        {
            const char *name = "unrecognized WHO_AM_I";
            if (id == 0x19)
            {
                name = "MPU6886 confirmed";
            }
            else if (id == 0x71)
            {
                name = "MPU9250 confirmed";
            }
            else if (id == 0x68)
            {
                name = "MPU6050 confirmed";
            }
            ESP_LOGI(TAG, "             WHO_AM_I 0x%02X (%s)", (unsigned)id, name);
        }
        break;

    case 0x76:
    case 0x77:
        if (read_register(bus, addr, 0xD0, &id))
        {
            const char *name = "unrecognized chip id";
            if (id == 0x60)
            {
                name = "BME280 confirmed";
            }
            else if (id == 0x58)
            {
                name = "BMP280 confirmed";
            }
            else if (id == 0x50)
            {
                name = "BMP388 confirmed";
            }
            ESP_LOGI(TAG, "             chip id 0x%02X (%s)", (unsigned)id, name);
        }
        break;

    case 0x29:
        if (read_register(bus, addr, 0xC0, &id))
        {
            ESP_LOGI(TAG, "             model id 0x%02X %s", (unsigned)id, (id == 0xEE) ? "(VL53L0X confirmed)" : "");
        }
        break;

    case 0x53:
        if (read_register(bus, addr, 0x00, &id))
        {
            ESP_LOGI(TAG, "             device id 0x%02X %s", (unsigned)id, (id == 0xE5) ? "(ADXL345 confirmed)" : "");
        }
        break;

    default:
        break;
    }
}

void scan_i2c(void)
{
    ESP_LOGI(TAG, "--- I2C scan: SDA=%d SCL=%d @ %d Hz ---", CONFIG_SCAN_I2C_SDA_GPIO, CONFIG_SCAN_I2C_SCL_GPIO, CONFIG_SCAN_I2C_FREQ_HZ);

    const i2c_master_bus_config_t bus_config = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = (gpio_num_t)CONFIG_SCAN_I2C_SDA_GPIO,
        .scl_io_num                   = (gpio_num_t)CONFIG_SCAN_I2C_SCL_GPIO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .intr_priority                = 0,
        .trans_queue_depth            = 0,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    const esp_err_t         err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize I2C bus: %s", esp_err_to_name(err));
        return;
    }

    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++)
    {
        if (i2c_master_probe(bus, addr, I2C_PROBE_TIMEOUT_MS) == ESP_OK)
        {
            found++;
            ESP_LOGI(TAG, "  found 0x%02X : %s", (unsigned)addr, lookup_device_name(addr));
            identify_device(bus, addr);
        }
    }

    i2c_del_master_bus(bus);
    ESP_LOGI(TAG, "--- I2C scan done: %d device(s) ---", found);
}
