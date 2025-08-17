#!/usr/bin/env python3
"""
Protocol message format definitions for UART2ETH validation.

Implements the message format: '#' + 4_hex_digits + payload + '!\r\n'
with character set validation and length enforcement per ADR-010.

References: ADR-010 External Protocol Validation Tool
"""

import re
from dataclasses import dataclass
from typing import Union


@dataclass
class ProtocolMessage:
    """
    Represents a UART2ETH protocol message with validation.
    
    Format: '#' + 4_hex_digits + payload + '!\r\n'
    Valid characters: '0'-'9', 'A'-'F', '#', '!', '\r', '\n'
    Maximum length: 1024 bytes total
    """
    
    hex_header: str
    payload: str
    
    def is_valid(self) -> bool:
        """
        Validate message format compliance.
        
        Returns:
            bool: True if message meets all format requirements
        """
        # Check hex header format (exactly 4 hex digits)
        if not self._is_valid_hex_header():
            return False
            
        # Check payload characters
        if not self._is_valid_payload():
            return False
            
        # Check total message length (max 1024 bytes)
        if not self._is_valid_length():
            return False
            
        return True
    
    def to_wire_format(self) -> bytes:
        """
        Convert message to wire transmission format.
        
        Returns:
            bytes: Message formatted as '#' + hex + payload + '!\r\n'
        """
        wire_message = f"#{self.hex_header}{self.payload}!\r\n"
        return wire_message.encode('ascii')
    
    def _is_valid_hex_header(self) -> bool:
        """Validate hex header is exactly 4 hex digits."""
        if len(self.hex_header) != 4:
            return False
            
        # Check if all characters are valid hex digits (0-9, A-F)
        hex_pattern = re.compile(r'^[0-9A-F]{4}$')
        return bool(hex_pattern.match(self.hex_header))
    
    def _is_valid_payload(self) -> bool:
        """
        Validate payload characters.
        
        According to ADR-010, valid characters are '0'-'9', 'A'-'F', '#', '!', '\r', '\n'
        However, payload should not contain protocol delimiters '#', '!', '\r', '\n'
        to avoid parsing conflicts.
        """
        # Payload should not contain protocol special characters
        forbidden_chars = {'#', '!', '\r', '\n'}
        return not any(char in forbidden_chars for char in self.payload)
    
    def _is_valid_length(self) -> bool:
        """Validate total message length does not exceed 1024 bytes."""
        total_length = len(self.to_wire_format())
        return total_length <= 1024
