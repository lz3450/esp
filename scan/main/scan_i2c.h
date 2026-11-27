#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Scan the configured I2C bus and log every device that responds.
     */
    void scan_i2c(void);

#ifdef __cplusplus
}
#endif
