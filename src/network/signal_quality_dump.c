/**
 * @file signal_quality_dump.c
 * @brief ENC28J60 Signal Quality Analysis Function
 * 
 * This function outputs all ENC28J60 registers that are relevant for 
 * signal quality analysis and packet loss troubleshooting.
 * 
 * Based on ENC28J60 Errata issues that can cause packet loss:
 * - Issue 7: TPIN+/- automatic polarity detection unreliable
 * - Issue 8: RBIAS resistor value affects transmit waveform  
 * - Issue 12: Transmit abort may stall transmit logic
 * - Issue 14: Even values in ERXRDPT may corrupt receive buffer
 * - Issue 15: LATECOL Status bit unreliable
 * - Issue 18: Pattern match filter allows reception of extra packets
 * 
 * Documentation Reference:
 * - ENC28J60 Errata DS80349C
 * - ENC28J60 Datasheet Rev. B7
 */

#include "network/enc28j60_driver.h"
#include <stdio.h>
#include <inttypes.h>

/**
 * @brief Read PHY register via MII interface (copy from driver)
 * Note: This is a copy of the internal function from enc28j60_driver.c
 */
static uint16_t enc28j60_read_phy_register_dump(uint8_t phy_reg) {
    // Set bank 2 for MII access
    enc28j60_set_bank(2);
    
    // Set the PHY register address to read
    enc28j60_write_register(0x14, phy_reg); // MIREGADR
    
    // Start the PHY read operation
    enc28j60_write_register(0x12, 0x01); // MICMD = MIIRD
    
    // Wait for the PHY read to complete
    // MISTAT is in Bank 3
    enc28j60_set_bank(3);
        
    uint32_t timeout = 1000;  // 1ms timeout
    while (timeout > 0) {
        uint8_t mistat = enc28j60_read_register(0x0A); // MISTAT
        if ((mistat & 0x01) == 0) { // BUSY bit
            break;  // Read complete
        }
        timeout--;
    }
    
    if (timeout == 0) {
        printf("  WARNING: PHY register 0x%02X read timeout!\n", phy_reg);
        return 0xFFFF;
    }
    
    // Return to Bank 2 for reading result
    enc28j60_set_bank(2);
    
    // Clear the read command
    enc28j60_write_register(0x12, 0x00); // MICMD = 0
    
    // Read the result from MIRDL and MIRDH registers
    uint8_t low_byte = enc28j60_read_register(0x18);  // MIRDL
    uint8_t high_byte = enc28j60_read_register(0x19); // MIRDH
    
    return (high_byte << 8) | low_byte;
}

/**
 * @brief Dump all ENC28J60 registers relevant for signal quality analysis
 * 
 * This function outputs comprehensive register information to help diagnose
 * signal quality issues, packet loss, and communication problems.
 * Call this function when experiencing packet loss or communication issues.
 */
void enc28j60_dump_signal_quality_registers(void) {
    if (!enc28j60_is_ready()) {
        printf("ENC28J60 Signal Quality Dump: Driver not ready!\n");
        return;
    }
    
    printf("\n");
    printf("================================================================================\n");
    printf("                    ENC28J60 SIGNAL QUALITY ANALYSIS DUMP\n");
    printf("================================================================================\n");
    
    // Get driver statistics first
    const enc28j60_state_t* state = enc28j60_get_state();
    
    printf("\n--- DRIVER STATISTICS ---\n");
    printf("  Initialized:              %s\n", state->initialized ? "YES" : "NO");
    printf("  Packets Sent:             %" PRIu32 "\n", state->packets_sent);
    printf("  Packets Received:         %" PRIu32 "\n", state->packets_received);
    printf("  TX Errors:                %" PRIu32 "\n", state->tx_errors);
    printf("  RX Errors:                %" PRIu32 "\n", state->rx_errors);
    printf("  False Collisions:         %" PRIu32 "\n", state->likely_false_collisions);
    printf("  Next Packet Pointer:      0x%04X\n", state->next_packet_ptr);
    
    // === CONTROL AND STATUS REGISTERS (Available in all banks) ===
    printf("\n--- CONTROL/STATUS REGISTERS (All Banks) ---\n");
    
    // Read control registers (save current bank first)
    uint8_t saved_bank = enc28j60_read_register(0x1F) & 0x03; // Current ECON1 bank bits
    
    uint8_t econ1 = enc28j60_read_register(0x1F);  // ECON1
    uint8_t econ2 = enc28j60_read_register(0x1E);  // ECON2  
    uint8_t estat = enc28j60_read_register(0x1D);  // ESTAT
    uint8_t eir = enc28j60_read_register(0x1C);    // EIR
    uint8_t eie = enc28j60_read_register(0x1B);    // EIE
    
    printf("  ECON1 (0x1F):             0x%02X\n", econ1);
    printf("    TXRTS (Transmit Req):   %s\n", (econ1 & 0x08) ? "ACTIVE" : "idle");
    printf("    RXEN (Receive Enable):  %s\n", (econ1 & 0x04) ? "ENABLED" : "disabled"); 
    printf("    TXRST (TX Reset):       %s\n", (econ1 & 0x80) ? "RESET" : "normal");
    printf("    RXRST (RX Reset):       %s\n", (econ1 & 0x40) ? "RESET" : "normal");
    printf("    Current Bank:           %d\n", econ1 & 0x03);
    
    printf("  ECON2 (0x1E):             0x%02X\n", econ2);
    printf("    AUTOINC (Auto Incr):    %s\n", (econ2 & 0x80) ? "ENABLED" : "disabled");
    printf("    PKTDEC (Packet Dec):    %s\n", (econ2 & 0x40) ? "ACTIVE" : "idle");
    
    printf("  ESTAT (0x1D):             0x%02X\n", estat);
    printf("    CLKRDY (Clock Ready):   %s\n", (estat & 0x01) ? "READY" : "NOT READY");
    printf("    TXABRT (TX Abort):      %s\n", (estat & 0x02) ? "ABORTED" : "normal");
    
    printf("  EIR (0x1C):               0x%02X\n", eir);
    printf("    PKTIF (RX Packet):      %s\n", (eir & 0x40) ? "PENDING" : "none");
    printf("    LINKIF (Link Change):   %s\n", (eir & 0x10) ? "PENDING" : "none");
    printf("    TXIF (TX Complete):     %s\n", (eir & 0x08) ? "PENDING" : "none");
    printf("    TXERIF (TX Error):      %s\n", (eir & 0x02) ? "ERROR" : "none");
    printf("    RXERIF (RX Error):      %s\n", (eir & 0x01) ? "ERROR" : "none");
    
    printf("  EIE (0x1B):               0x%02X\n", eie);
    printf("    INTIE (Global Int):     %s\n", (eie & 0x80) ? "ENABLED" : "disabled");
    printf("    PKTIE (RX Int):         %s\n", (eie & 0x40) ? "ENABLED" : "disabled");
    printf("    LINKIE (Link Int):      %s\n", (eie & 0x10) ? "ENABLED" : "disabled");
    printf("    TXIE (TX Int):          %s\n", (eie & 0x08) ? "ENABLED" : "disabled");
    printf("    TXERIE (TX Err Int):    %s\n", (eie & 0x02) ? "ENABLED" : "disabled");
    
    // === BANK 0 - BUFFER CONTROL REGISTERS ===
    printf("\n--- BANK 0 - BUFFER CONTROL REGISTERS ---\n");
    enc28j60_set_bank(0);
    
    uint16_t erxst = (enc28j60_read_register(0x09) << 8) | enc28j60_read_register(0x08);   // ERXST
    uint16_t erxnd = (enc28j60_read_register(0x0B) << 8) | enc28j60_read_register(0x0A);   // ERXND
    uint16_t erxrdpt = (enc28j60_read_register(0x0D) << 8) | enc28j60_read_register(0x0C); // ERXRDPT
    uint16_t erdpt = (enc28j60_read_register(0x01) << 8) | enc28j60_read_register(0x00);   // ERDPT
    uint16_t ewrpt = (enc28j60_read_register(0x03) << 8) | enc28j60_read_register(0x02);   // EWRPT
    uint16_t etxst = (enc28j60_read_register(0x05) << 8) | enc28j60_read_register(0x04);   // ETXST
    uint16_t etxnd = (enc28j60_read_register(0x07) << 8) | enc28j60_read_register(0x06);   // ETXND
    
    printf("  RX Buffer Configuration:\n");
    printf("    ERXST (RX Start):       0x%04X\n", erxst);
    printf("    ERXND (RX End):         0x%04X\n", erxnd);
    printf("    ERXRDPT (RX Read Ptr):  0x%04X", erxrdpt);
    if (erxrdpt % 2 == 0 && erxrdpt != 0) {
        printf(" *** WARNING: Even ERXRDPT value (Errata #14) ***");
    }
    printf("\n");
    printf("    ERDPT (Read Ptr):       0x%04X\n", erdpt);
    printf("    RX Buffer Size:         %d bytes\n", erxnd - erxst + 1);
    
    printf("  TX Buffer Configuration:\n");
    printf("    ETXST (TX Start):       0x%04X\n", etxst);
    printf("    ETXND (TX End):         0x%04X\n", etxnd);
    printf("    EWRPT (Write Ptr):      0x%04X\n", ewrpt);
    printf("    TX Buffer Size:         %d bytes\n", etxnd - etxst + 1);
    
    // === BANK 1 - PACKET COUNT AND FILTER CONTROL ===
    printf("\n--- BANK 1 - PACKET COUNT AND FILTER CONTROL ---\n");
    enc28j60_set_bank(1);
    
    uint8_t epktcnt = enc28j60_read_register(0x19);  // EPKTCNT
    uint8_t erxfcon = enc28j60_read_register(0x18);  // ERXFCON
    
    printf("  EPKTCNT (Packet Count):   %d\n", epktcnt);
    printf("  ERXFCON (RX Filter):      0x%02X\n", erxfcon);
    printf("    UCEN (Unicast):         %s\n", (erxfcon & 0x80) ? "ENABLED" : "disabled");
    printf("    ANDOR (Filter Logic):   %s\n", (erxfcon & 0x40) ? "OR" : "AND");
    printf("    CRCEN (CRC Check):      %s\n", (erxfcon & 0x20) ? "ENABLED" : "disabled");
    printf("    MCEN (Multicast):       %s\n", (erxfcon & 0x02) ? "ENABLED" : "disabled");
    printf("    BCEN (Broadcast):       %s\n", (erxfcon & 0x01) ? "ENABLED" : "disabled");
    
    // === BANK 2 - MAC CONTROL REGISTERS ===
    printf("\n--- BANK 2 - MAC CONTROL REGISTERS ---\n");
    enc28j60_set_bank(2);
    
    uint8_t macon1 = enc28j60_read_register(0x00);   // MACON1
    uint8_t macon3 = enc28j60_read_register(0x02);   // MACON3
    uint16_t mamxfl = (enc28j60_read_register(0x0B) << 8) | enc28j60_read_register(0x0A); // MAMXFL
    uint8_t mabbipg = enc28j60_read_register(0x04);  // MABBIPG
    uint8_t maipgl = enc28j60_read_register(0x06);   // MAIPGL
    
    printf("  MACON1 (MAC Control 1):   0x%02X\n", macon1);
    printf("    MARXEN (RX Enable):     %s\n", (macon1 & 0x01) ? "ENABLED" : "disabled");
    printf("    RXPAUS (RX Pause):      %s\n", (macon1 & 0x04) ? "ENABLED" : "disabled");
    printf("    TXPAUS (TX Pause):      %s\n", (macon1 & 0x08) ? "ENABLED" : "disabled");
    
    printf("  MACON3 (MAC Control 3):   0x%02X\n", macon3);
    printf("    FULDPX (Full Duplex):   %s\n", (macon3 & 0x01) ? "ENABLED" : "disabled");
    printf("    FRMLNEN (Frame Len):    %s\n", (macon3 & 0x02) ? "ENABLED" : "disabled");
    printf("    TXCRCEN (TX CRC):       %s\n", (macon3 & 0x10) ? "ENABLED" : "disabled");
    printf("    PADCFG (Padding):       0x%02X\n", (macon3 & 0xE0) >> 5);
    
    printf("  MAMXFL (Max Frame Len):   %d bytes\n", mamxfl);
    printf("  MABBIPG (Back-to-Back):   %d\n", mabbipg);
    printf("  MAIPGL (Non-Back-to-Back): %d\n", maipgl);
    
    // === BANK 3 - MAC ADDRESS AND CHIP INFO ===
    printf("\n--- BANK 3 - MAC ADDRESS AND CHIP INFO ---\n");
    enc28j60_set_bank(3);
    
    uint8_t mac_addr[6];
    mac_addr[0] = enc28j60_read_register(0x04);  // MAADR1
    mac_addr[1] = enc28j60_read_register(0x05);  // MAADR2  
    mac_addr[2] = enc28j60_read_register(0x02);  // MAADR3
    mac_addr[3] = enc28j60_read_register(0x03);  // MAADR4
    mac_addr[4] = enc28j60_read_register(0x00);  // MAADR5
    mac_addr[5] = enc28j60_read_register(0x01);  // MAADR6
    
    uint8_t erevid = enc28j60_read_register(0x12);   // EREVID
    
    printf("  MAC Address:              %02X:%02X:%02X:%02X:%02X:%02X\n", 
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    printf("  EREVID (Chip Revision):   0x%02X", erevid);
    switch(erevid) {
        case 0x02: printf(" (Rev B1)"); break;
        case 0x04: printf(" (Rev B4)"); break; 
        case 0x05: printf(" (Rev B5)"); break;
        case 0x06: printf(" (Rev B7)"); break;
        default: printf(" (Unknown)"); break;
    }
    printf("\n");
    
    // === PHY REGISTERS (Signal Quality Critical) ===
    printf("\n--- PHY REGISTERS (Signal Quality Critical) ---\n");
    
    uint16_t phcon1 = enc28j60_read_phy_register_dump(0x00);   // PHCON1
    uint16_t phstat1 = enc28j60_read_phy_register_dump(0x01);  // PHSTAT1
    uint16_t phcon2 = enc28j60_read_phy_register_dump(0x10);   // PHCON2
    uint16_t phstat2 = enc28j60_read_phy_register_dump(0x11);  // PHSTAT2
    
    printf("  PHCON1 (PHY Control 1):   0x%04X\n", phcon1);
    printf("    PDPXMD (Duplex Mode):   %s\n", (phcon1 & 0x0100) ? "Full Duplex" : "Half Duplex");
    printf("    PLOOPBK (Loopback):     %s\n", (phcon1 & 0x4000) ? "ENABLED" : "disabled");
    
    printf("  PHSTAT1 (PHY Status 1):   0x%04X\n", phstat1);
    printf("    LLSTAT (Link Status):   %s\n", (phstat1 & 0x0004) ? "LINKED" : "not linked");
    printf("    JBSTAT (Jabber):        %s\n", (phstat1 & 0x0002) ? "DETECTED" : "none");
    
    printf("  PHCON2 (PHY Control 2):   0x%04X\n", phcon2);
    printf("    HDLDIS (Half-Dup Dis):  %s\n", (phcon2 & 0x0100) ? "DISABLED" : "enabled");
    printf("    FRCLNK (Force Link):    %s\n", (phcon2 & 0x4000) ? "FORCED" : "normal");
    
    printf("  PHSTAT2 (PHY Status 2):   0x%04X\n", phstat2);
    printf("    LSTAT (Link Status):    %s\n", (phstat2 & 0x0400) ? "LINKED" : "not linked");
    printf("    DPXSTAT (Duplex Stat):  %s\n", (phstat2 & 0x0200) ? "Full Duplex" : "Half Duplex");
    printf("    TXSTAT (TX Status):     %s\n", (phstat2 & 0x2000) ? "TRANSMITTING" : "idle");
    printf("    RXSTAT (RX Status):     %s\n", (phstat2 & 0x1000) ? "RECEIVING" : "idle");
    printf("    COLSTAT (Collision):    %s\n", (phstat2 & 0x0800) ? "DETECTED" : "none");
    printf("    PLRITY (Polarity):      %s\n", (phstat2 & 0x0020) ? "CORRECTED" : "normal");
    
    // === SIGNAL QUALITY ANALYSIS ===
    printf("\n--- SIGNAL QUALITY ANALYSIS ---\n");
    
    // Check for common signal quality issues based on errata
    bool issues_found = false;
    
    if (!(estat & 0x01)) {
        printf("  *** CRITICAL: Clock not ready (ESTAT.CLKRDY = 0) ***\n");
        issues_found = true;
    }
    
    if (estat & 0x02) {
        printf("  *** ERROR: Transmit abort detected (ESTAT.TXABRT = 1) ***\n");
        issues_found = true;
    }
    
    if (eir & 0x02) {
        printf("  *** ERROR: Transmit error interrupt pending (EIR.TXERIF = 1) ***\n");
        issues_found = true;
    }
    
    if (eir & 0x01) {
        printf("  *** ERROR: Receive error interrupt pending (EIR.RXERIF = 1) ***\n");
        issues_found = true;
    }
    
    if (erxrdpt % 2 == 0 && erxrdpt != 0) {
        printf("  *** WARNING: ERXRDPT has even value (Errata #14 - may corrupt RX buffer) ***\n");
        issues_found = true;
    }
    
    if (!(phstat1 & 0x0004) || !(phstat2 & 0x0400)) {
        printf("  *** WARNING: PHY reports link down - check cable/connection ***\n");
        issues_found = true;
    }
    
    if (phstat1 & 0x0002) {
        printf("  *** WARNING: Jabber condition detected - possible signal integrity issue ***\n");
        issues_found = true;
    }
    
    if (phstat2 & 0x0020) {
        printf("  *** INFO: Polarity correction active (Errata #7 - verify TPIN+/- wiring) ***\n");
        issues_found = true;
    }
    
    if (state->tx_errors > 0) {
        printf("  *** WARNING: %d transmit errors recorded ***\n", (int)state->tx_errors);
        issues_found = true;
    }
    
    if (state->rx_errors > 0) {
        printf("  *** WARNING: %d receive errors recorded ***\n", (int)state->rx_errors);
        issues_found = true;
    }
    
    if (state->likely_false_collisions > 0) {
        printf("  *** INFO: %d false collision detections (Errata #15) ***\n", (int)state->likely_false_collisions);
        issues_found = true;
    }
    
    if (!issues_found) {
        printf("  No obvious signal quality issues detected.\n");
        printf("  If experiencing packet loss, consider:\n");
        printf("  - Check RBIAS resistor value for your chip revision (Errata #8)\n");
        printf("  - Verify TPIN+/TPIN- wiring polarity (Errata #7)\n");
        printf("  - Check SPI clock frequency >= 8MHz (Errata #1)\n");
        printf("  - Monitor for intermittent issues over time\n");
    }
    
    // Restore original bank
    enc28j60_set_bank(saved_bank);
    
    printf("\n================================================================================\n");
    printf("                         END SIGNAL QUALITY ANALYSIS\n");
    printf("================================================================================\n\n");
}
