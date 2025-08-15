# ENC28J60 Buffer Space Management Research

## Executive Summary

The ENC28J60 Ethernet controller provides 8KB of internal buffer memory that must be managed efficiently for packet transmission and reception. This research examines methods to determine available TX buffer space before attempting packet transmission, addressing the critical question: **"How can we know whether there is enough space left for our next packet?"**

## Current Project Context

Our UART2ETH project uses:
- **RX Buffer**: 4KB (0x0000-0x0FFF) 
- **TX Buffer**: 4KB (0x1040-0x1FFF)
- **Static allocation strategy** (ADR-005): Fixed worst-case allocation
- **Arduino-style driver implementation** based on verified reference code

## Key Research Findings

### 1. Buffer Architecture Overview

The ENC28J60 internal 8KB SRAM is divided into:
- **Shared buffer space**: Programmable division between RX and TX
- **Single buffer pointer system**: Only one transmission at a time
- **Circular RX buffer**: Hardware-managed FIFO for received packets
- **Linear TX buffer**: Software-managed space for outgoing packets

### 2. Transmission Buffer Management Strategies

#### Strategy A: Fixed Single-Packet Buffer (Current Implementation)
```c
// Current approach - allocate entire TX space for single packet
#define TX_BUF_START 0x1040
#define TX_BUF_SIZE  4096

bool enc28j60_send_packet(const enc28j60_packet_t* packet) {
    // Always use fixed start address
    enc28j60_write_register(ENC28J60_ETXSTL, TX_BUF_START & 0xFF);
    // ... write packet
    // No space checking - assumes entire buffer available
}
```

**Pros**: Simple, deterministic, matches our static allocation strategy
**Cons**: Inefficient buffer utilization, blocks on transmission

#### Strategy B: Queue-Based Buffer Management (Research Finding)
```c
// EtherCard library approach - dynamic buffer allocation
static uint16_t current_tx_end = TX_BUF_START;

uint16_t enc28j60_allocate_tx_space(uint16_t packet_size) {
    uint16_t required_space = packet_size + 1; // +1 for control byte
    
    // Check available space
    if (current_tx_end + required_space > TX_BUF_END) {
        // Reset to beginning if space available
        if (transmission_complete()) {
            current_tx_end = TX_BUF_START;
        } else {
            return 0; // No space available
        }
    }
    
    uint16_t allocated_start = current_tx_end;
    current_tx_end += required_space;
    return allocated_start;
}
```

#### Strategy C: Free Memory Calculation (RIOT-OS Approach)
```c
// Calculate available memory based on register pointers
uint16_t enc28j60_get_free_tx_memory(void) {
    uint16_t tx_start = TX_BUF_START;
    uint16_t tx_end = TX_BUF_END;
    uint16_t current_tx_end = enc28j60_read_register_pair(ENC28J60_ETXNDL);
    
    if (enc28j60_is_tx_complete()) {
        // Transmission complete - full buffer available
        return tx_end - tx_start;
    } else {
        // Transmission in progress - calculate remaining space
        return tx_end - current_tx_end;
    }
}
```

### 3. Space Checking Methods

#### Method 1: Register-Based Calculation
```c
// Check based on ETXST and ETXND register values
bool enc28j60_has_tx_space(uint16_t packet_size) {
    if (!enc28j60_is_tx_complete()) {
        return false; // Transmission in progress
    }
    
    uint16_t available_space = TX_BUF_END - TX_BUF_START;
    uint16_t required_space = packet_size + 1; // +1 for control byte
    
    return available_space >= required_space;
}
```

#### Method 2: Transmission Status Verification
```c
// Enhanced transmission completion check
bool enc28j60_is_tx_complete(void) {
    // Method A: ECON1.TXRTS bit (current implementation)
    uint8_t econ1 = enc28j60_read_register(ENC28J60_ECON1);
    bool txrts_clear = (econ1 & ENC28J60_ECON1_TXRTS) == 0;
    
    // Method B: Interrupt flags (more reliable per errata)
    uint8_t eir = enc28j60_read_register(ENC28J60_EIR);
    bool tx_interrupt = (eir & (ENC28J60_EIR_TXIF | ENC28J60_EIR_TXERIF)) != 0;
    
    return txrts_clear || tx_interrupt;
}
```

#### Method 3: Buffer Pointer Analysis
```c
// Comprehensive buffer state analysis
typedef struct {
    uint16_t tx_start;      // ETXST register
    uint16_t tx_end;        // ETXND register  
    uint16_t write_ptr;     // EWRPT register
    uint16_t available;     // Calculated free space
    bool     tx_active;     // Transmission status
} enc28j60_buffer_status_t;

enc28j60_buffer_status_t enc28j60_get_buffer_status(void) {
    enc28j60_buffer_status_t status;
    
    enc28j60_set_bank(ENC28J60_BANK0);
    status.tx_start = enc28j60_read_register_pair(ENC28J60_ETXSTL);
    status.tx_end = enc28j60_read_register_pair(ENC28J60_ETXNDL);
    status.write_ptr = enc28j60_read_register_pair(ENC28J60_EWRPTL);
    status.tx_active = !enc28j60_is_tx_complete();
    
    if (!status.tx_active) {
        status.available = TX_BUF_END - TX_BUF_START;
    } else {
        status.available = TX_BUF_END - status.tx_end;
    }
    
    return status;
}
```

### 4. Industry Best Practices Analysis

#### Arduino EtherCard Library Approach
- **Single packet transmission**: Wait for completion before next packet
- **Errata workarounds**: Handle silicon bugs #12 and #13
- **Timeout mechanisms**: Prevent infinite blocking
- **Buffer reset**: Clear transmission logic on errors

#### RIOT-OS Implementation  
- **Buffer partitioning**: 6KB RX, 2KB TX split
- **Free memory function**: Calculate available space
- **Multiple packet support**: Queue management
- **Interrupt-driven**: Async transmission handling

#### UIPEthernet Library Strategy
- **Packet queuing**: Multiple outstanding transmissions
- **Memory pools**: Dynamic buffer allocation
- **Flow control**: Backpressure on buffer exhaustion
- **Status vectors**: Detailed transmission results

### 5. Recommended Implementation for UART2ETH

Given our project constraints (industrial reliability, static allocation, sub-5ms latency), I recommend:

#### Phase 1: Enhanced Current Approach
```c
// Maintain current single-packet strategy with improved checking
bool enc28j60_can_send_packet(uint16_t packet_size) {
    // 1. Check transmission status
    if (!enc28j60_is_tx_complete()) {
        return false; // Previous transmission still active
    }
    
    // 2. Validate packet size against buffer capacity
    uint16_t max_packet_size = TX_BUF_SIZE - 1; // -1 for control byte
    if (packet_size > max_packet_size) {
        return false; // Packet too large
    }
    
    // 3. Check for transmission errors
    uint8_t estat = enc28j60_read_register(ENC28J60_ESTAT);
    if (estat & ENC28J60_ESTAT_TXABRT) {
        // Reset transmission logic (errata workaround)
        enc28j60_reset_tx_logic();
    }
    
    return true;
}
```

#### Phase 2: Advanced Buffer Management
```c
// Future enhancement - enable multiple packet queuing
typedef struct {
    uint16_t start_addr;
    uint16_t end_addr;
    uint16_t packet_size;
    bool     transmitted;
} tx_packet_descriptor_t;

// Queue management for multiple packets
bool enc28j60_queue_packet(const enc28j60_packet_t* packet) {
    uint16_t available_space = enc28j60_get_free_tx_space();
    uint16_t required_space = packet->length + 1;
    
    if (available_space < required_space) {
        return false; // Insufficient space
    }
    
    // Allocate buffer space and queue packet
    tx_packet_descriptor_t* desc = allocate_tx_descriptor();
    desc->start_addr = allocate_tx_buffer(required_space);
    // ... implement queuing logic
    
    return true;
}
```

### 6. Key Register Usage Summary

| Register | Purpose | Usage for Space Checking |
|----------|---------|---------------------------|
| ETXSTL/H | TX start pointer | Read to determine current allocation |
| ETXNDL/H | TX end pointer | Calculate used space |
| EWRPTL/H | Write pointer | Track buffer fill status |
| ECON1.TXRTS | Transmission active | Primary transmission status |
| EIR.TXIF | TX complete interrupt | Reliable completion indicator |
| EIR.TXERIF | TX error interrupt | Error detection |
| ESTAT.TXABRT | TX abort status | Error recovery indicator |

### 7. Critical Errata Considerations

**Silicon Errata #12 & #13**: Transmission logic may hang
- **Symptoms**: TXRTS never clears, transmission appears stuck
- **Workaround**: Monitor TXIF/TXERIF instead of just TXRTS
- **Recovery**: Reset transmission logic on errors

**Implementation**:
```c
bool enc28j60_wait_tx_complete_with_timeout(uint32_t timeout_us) {
    uint32_t start_time = time_us_32();
    
    while (time_us_32() - start_time < timeout_us) {
        uint8_t eir = enc28j60_read_register(ENC28J60_EIR);
        
        if (eir & ENC28J60_EIR_TXIF) {
            return true; // Success
        }
        
        if (eir & ENC28J60_EIR_TXERIF) {
            // Error occurred - reset TX logic
            enc28j60_reset_tx_logic();
            return false;
        }
        
        sleep_us(10);
    }
    
    // Timeout - force reset
    enc28j60_reset_tx_logic();
    return false;
}
```

## Recommendations for Implementation

1. **Immediate**: Implement `enc28j60_can_send_packet()` function
2. **Short term**: Add timeout and error recovery mechanisms  
3. **Future**: Consider queue-based buffer management for performance
4. **Testing**: Validate with worst-case packet sizes and error conditions

## Impact on Current Architecture

This research aligns with our ADR-005 static allocation strategy while providing:
- **Deterministic behavior**: Predictable space checking
- **Industrial reliability**: Error detection and recovery
- **Latency optimization**: Efficient transmission status checking
- **Future scalability**: Clear path to enhanced buffer management

The recommended approach maintains our reliability-first principles while enabling more sophisticated buffer management as the system evolves.

---

*Research conducted for UART2ETH project - ENC28J60 Buffer Space Management*
*Date: 2025-08-14*
*Architecture Reference: ADR-002 (ENC28J60 Selection), ADR-005 (Buffer Allocation)*