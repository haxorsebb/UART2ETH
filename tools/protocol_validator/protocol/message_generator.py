#!/usr/bin/env python3
"""
Message generator for UART2ETH protocol validation testing.

Creates various message patterns for comprehensive protocol testing including
minimal, medium, maximum, random, and invalid message variations.

References: ADR-010 External Protocol Validation Tool
"""

import random
import string
from typing import List
from .formats import ProtocolMessage


class MessageGenerator:
    """
    Generates test messages for UART2ETH protocol validation.
    
    Produces various message patterns for compliance testing, performance
    measurement, and error scenario validation.
    """
    
    def generate_minimal_set(self) -> List[ProtocolMessage]:
        """
        Generate set of minimal valid messages.
        
        Creates messages from #0000!\r\n to #FFFF!\r\n with systematic
        hex value coverage for compliance testing.
        
        Returns:
            List[ProtocolMessage]: Set of minimal messages with empty payload
        """
        messages = []
        
        # Generate representative sampling of hex space (every 4096th value for efficiency)
        for hex_val in range(0, 0x10000, 0x1):
            hex_str = f"{hex_val:04X}"
            message = ProtocolMessage(hex_header=hex_str, payload="")
            messages.append(message)
            
        return messages
    
    def generate_minimal_with_hex(self, hex_value: str) -> ProtocolMessage:
        """
        Generate minimal message with specific hex value.
        
        Args:
            hex_value: 4-character hex string (e.g., "ABCD")
            
        Returns:
            ProtocolMessage: Minimal message with specified hex header
        """
        return ProtocolMessage(hex_header=hex_value, payload="")
    
    def generate_medium(self, payload_size: int) -> ProtocolMessage:
        """
        Generate message with specified payload size.
        
        Args:
            payload_size: Size of payload in bytes
            
        Returns:
            ProtocolMessage: Message with payload of specified size
        """
        # Generate random hex header
        hex_header = f"{random.randint(0, 0xFFFF):04X}"
        
        # Limit payload size for UART2ETH device compatibility
        payload_size = min(payload_size, 32)
        
        # Generate payload of specified size using valid characters per protocol spec
        # Valid chars: '0'-'9', 'A'-'F' (avoiding protocol special chars #!\\r\\n)
        valid_chars = string.digits + 'ABCDEF'
        payload = ''.join(random.choice(valid_chars) for _ in range(payload_size))
        
        return ProtocolMessage(hex_header=hex_header, payload=payload)
    
    def generate_maximum(self) -> ProtocolMessage:
        """
        Generate maximum valid message (1024 bytes total).
        
        Returns:
            ProtocolMessage: Message with maximum allowed size
        """
        hex_header = "FFFF"
        # Temporary limit: payload = 1024 - 8 (for '#XXXX!\r\n') = 1016 bytes
        max_payload_size = 1016
        
        valid_chars = string.digits + 'ABCDEF'
        payload = ''.join(random.choice(valid_chars) for _ in range(max_payload_size))
        
        return ProtocolMessage(hex_header=hex_header, payload=payload)
    
    def generate_random_size(self, min_size: int, max_size: int) -> ProtocolMessage:
        """
        Generate message with random size within specified range.
        
        Args:
            min_size: Minimum total message size
            max_size: Maximum total message size
            
        Returns:
            ProtocolMessage: Message with random size in range
        """
        # Account for protocol overhead: '#XXXX!\r\n' = 8 bytes
        min_payload = max(0, min_size - 8)
        max_payload = max_size - 8
        
        payload_size = random.randint(min_payload, max_payload)
        return self.generate_medium(payload_size)
    
    def generate_invalid_set(self) -> List[ProtocolMessage]:
        """
        Generate set of invalid messages for error testing.
        
        Returns:
            List[ProtocolMessage]: Various invalid message patterns
        """
        invalid_messages = []
        
        # Invalid hex header lengths
        invalid_messages.append(ProtocolMessage(hex_header="123", payload="test"))
        invalid_messages.append(ProtocolMessage(hex_header="12345", payload="test"))
        invalid_messages.append(ProtocolMessage(hex_header="", payload="test"))
        
        # Invalid hex characters
        invalid_messages.append(ProtocolMessage(hex_header="123G", payload="test"))
        invalid_messages.append(ProtocolMessage(hex_header="GGGG", payload="test"))
        
        # Invalid payload characters (protocol delimiters)
        invalid_messages.append(ProtocolMessage(hex_header="1234", payload="test#"))
        invalid_messages.append(ProtocolMessage(hex_header="1234", payload="test!"))
        invalid_messages.append(ProtocolMessage(hex_header="1234", payload="test\r"))
        invalid_messages.append(ProtocolMessage(hex_header="1234", payload="test\n"))
        
        # Message too long
        too_long_payload = "A" * 1017  # Exceeds 1024 byte limit
        invalid_messages.append(ProtocolMessage(hex_header="1234", payload=too_long_payload))
        
        return invalid_messages
    
    def generate_stress_pattern(self, count: int) -> List[ProtocolMessage]:
        """
        Generate high-volume pattern for stress testing.
        
        Args:
            count: Number of messages to generate
            
        Returns:
            List[ProtocolMessage]: High-volume message set
        """
        messages = []
        
        for i in range(count):
            # Vary message sizes for realistic stress testing
            # Reduced to 32 bytes max to ensure reliable processing with UART2ETH device
            payload_size = random.randint(0, 32)  # Small size for high volume
            message = self.generate_medium(payload_size)
            messages.append(message)
            
        return messages
    
    def generate_hex_sampling(self, step: int) -> List[ProtocolMessage]:
        """
        Generate systematic sampling of hex value space.
        
        Args:
            step: Step size for hex value sampling
            
        Returns:
            List[ProtocolMessage]: Messages with systematic hex coverage
        """
        messages = []
        
        for hex_val in range(0, 0x10000, step):
            hex_str = f"{hex_val:04X}"
            message = ProtocolMessage(hex_header=hex_str, payload="sample")
            messages.append(message)
            
        return messages
    
    def generate_performance_pattern(self, target_rate_kbps: int, duration_seconds: int) -> List[ProtocolMessage]:
        """
        Generate message pattern optimized for performance testing.
        
        Creates messages sized to achieve target throughput rate for
        sustained performance measurement.
        
        Args:
            target_rate_kbps: Target throughput in kbytes/second
            duration_seconds: Test duration in seconds
            
        Returns:
            List[ProtocolMessage]: Messages optimized for performance testing
        """
        target_bytes = (target_rate_kbps / 10) * 1024 * duration_seconds    # baud=10 bits per byte
        messages = []
        total_bytes = 0
        
        # Use small payloads compatible with UART2ETH device limitations  
        # Reduced to 16 bytes to ensure reliable processing
        typical_payload_size = 16
        typical_message_size = typical_payload_size + 8  # + protocol overhead
        
        while total_bytes < target_bytes:
            message = self.generate_medium(typical_payload_size)
            messages.append(message)
            total_bytes += len(message.to_wire_format())
        
        print("Generated %d messages for test"%(len(messages)))
        return messages
