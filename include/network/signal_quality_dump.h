/**
 * @file signal_quality_dump.h
 * @brief ENC28J60 Signal Quality Analysis Function Header
 * 
 * This header provides the interface for comprehensive ENC28J60 signal quality
 * analysis and register dumping for troubleshooting packet loss and communication issues.
 */

#ifndef SIGNAL_QUALITY_DUMP_H
#define SIGNAL_QUALITY_DUMP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dump all ENC28J60 registers relevant for signal quality analysis
 * 
 * This function outputs comprehensive register information to help diagnose
 * signal quality issues, packet loss, and communication problems.
 * 
 * The function examines:
 * - Driver statistics (TX/RX errors, packet counts)
 * - Control and status registers (ECON1/2, ESTAT, EIR, EIE)
 * - Buffer control registers (ERXST/ND/RDPT, ETXST/ND, EWRPT)
 * - Packet count and filter control (EPKTCNT, ERXFCON)
 * - MAC control registers (MACON1/3, MAMXFL, etc.)
 * - MAC address and chip revision info
 * - PHY registers (PHCON1/2, PHSTAT1/2) - critical for signal quality
 * - Analysis of common errata-related issues
 * 
 * Call this function when experiencing:
 * - Packet loss or corruption
 * - Intermittent communication failures
 * - Link stability issues
 * - Suspected signal integrity problems
 * 
 * Requires: ENC28J60 driver must be initialized (enc28j60_init() called)
 * 
 * @note This function temporarily changes register banks but restores the
 *       original bank when complete.
 * @note Output goes to printf() - redirect stdout as needed for logging.
 */
void enc28j60_dump_signal_quality_registers(void);

#ifdef __cplusplus
}
#endif

#endif // SIGNAL_QUALITY_DUMP_H
