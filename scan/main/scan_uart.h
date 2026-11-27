#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Listen on a list of candidate UART RX pins at common baud rates and
     *        log serial devices (GPS/NMEA, text devices, unidentified data).
     */
    void scan_uart(void);

#ifdef __cplusplus
}
#endif
