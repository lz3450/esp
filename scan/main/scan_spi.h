#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Probe the configured SPI bus on a list of candidate CS pins and log
     *        detected SD cards, SPI flash chips and unidentified responding devices.
     */
    void scan_spi(void);

#ifdef __cplusplus
}
#endif
