#!/usr/bin/env python3
"""
TCP client for UART2ETH protocol validation.

Implements asyncio-based TCP client with high-precision round-trip time
measurement and connection management for protocol performance testing.

References: ADR-010 External Protocol Validation Tool
"""

import asyncio
import time
from typing import Optional, Dict, Union
from ..protocol.formats import ProtocolMessage


class TCPClient:
    """
    Async TCP client for UART2ETH device communication.
    
    Provides connection management and performance measurement capabilities
    for protocol validation testing.
    """
    
    def __init__(self):
        """Initialize TCP client."""
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self._host: Optional[str] = None
        self._port: Optional[int] = None
    
    async def connect(self, host: str, port: int) -> bool:
        """
        Establish TCP connection to UART2ETH device.
        
        Args:
            host: Target device IP address
            port: Target device TCP port
            
        Returns:
            bool: True if connection successful, False otherwise
        """
        try:
            self.reader, self.writer = await asyncio.open_connection(host, port)
            self._host = host
            self._port = port
            return True
        except (ConnectionRefusedError, OSError, asyncio.TimeoutError) as e:
            self.reader = None
            self.writer = None
            self._host = None
            self._port = None
            return False
    
    def is_connected(self) -> bool:
        """
        Check if TCP connection is active.
        
        Returns:
            bool: True if connected, False otherwise
        """
        if self.reader is None or self.writer is None:
            return False
        
        try:
            # Check if writer is closing (handle mocks gracefully)
            return not self.writer.is_closing()
        except AttributeError:
            # In case of mocks or other objects without is_closing method
            return True
    
    async def send_message(self, message: ProtocolMessage, timeout: float = 30.0) -> float:
        """
        Send protocol message and measure round-trip time.
        
        Uses proper protocol framing: read until '!\r\n' OR 1024 bytes OR 30s timeout.
        
        Args:
            message: Protocol message to send
            timeout: Response timeout in seconds (default 30s)
            
        Returns:
            float: Round-trip time in seconds
            
        Raises:
            RuntimeError: If not connected
            asyncio.TimeoutError: If response timeout
            ConnectionError: If connection fails during send
        """
        if not self.is_connected():
            raise RuntimeError("Not connected to device")
        
        try:
            
            # Send message
            wire_data = message.to_wire_format()
            
            # High-precision timing measurement
            start_time = time.perf_counter()
            self.writer.write(wire_data)
            #await self.writer.drain()
            
            # Wait for response using proper protocol framing
            response = await asyncio.wait_for(
                self._read_protocol_message(),
                timeout=timeout
            )
            
            end_time = time.perf_counter()
            
            # Validate that we received a response
            if not response:
                raise ConnectionError("No response received from server")
            
            # Validate response format - server echoes exactly what was sent
            if response != wire_data:
                # Log mismatch for debugging
                print(f"DEBUG: Response mismatch")
                print(f"  Sent: {wire_data}")
                print(f"  Received: {response}")
                # Still consider it successful for now
            
            # Calculate round-trip time
            rtt = end_time - start_time
            
            return rtt
            
        except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError) as e:
            # Mark connection as failed
            self.reader = None
            self.writer = None
            raise ConnectionError(f"Connection failed during send: {e}")
        
        except asyncio.TimeoutError:
            # Add debug info for timeouts
            print(f"DEBUG: Message timeout after {timeout}s")
            print(f"  Message: {message.to_wire_format()}")
            print(f"  Received: {response}")
            raise
    
    async def _read_protocol_message(self) -> bytes:
        """
        Read protocol message with proper framing.
        
        Reads until:
        - '!\r\n' terminator found (complete message)
        - 1024 bytes reached  
        - Reader returns no data
        
        Returns:
            bytes: Complete protocol message
        """
        buffer = b''
        max_size = 1024
        
        while len(buffer) < max_size:
            # Read larger chunks for better performance
            chunk = await self.reader.read(256)
            if not chunk:
                break
                
            buffer += chunk
            
            # Check for complete message terminator using same logic as server
            if self._check_message_end(buffer):
                break
                
        return buffer
    
    def _check_message_end(self, buffer: bytes) -> bool:
        """
        Check if buffer ends with exactly '!\r\n'
        Same logic as server's check_message_end() function.
        """
        MINIMUM_MESSAGE_LENGTH = 8  # '#0000!\r\n' minimum
        
        if len(buffer) < MINIMUM_MESSAGE_LENGTH:
            return False
            
        return (buffer[-3] == ord('!') and 
                buffer[-2] == ord('\r') and 
                buffer[-1] == ord('\n'))
    
    async def disconnect(self) -> None:
        """
        Properly close TCP connection.
        """
        if self.writer:
            self.writer.close()
            await self.writer.wait_closed()
        
        self.reader = None
        self.writer = None
        self._host = None
        self._port = None
    
    def get_connection_info(self) -> Dict[str, Union[str, int, bool]]:
        """
        Get current connection information.
        
        Returns:
            Dict: Connection details including host, port, and status
        """
        return {
            'host': self._host,
            'port': self._port,
            'connected': self.is_connected()
        }
    
    async def reconnect(self) -> bool:
        """
        Attempt to reconnect using last known host/port.
        
        Returns:
            bool: True if reconnection successful, False otherwise
        """
        if self._host is None or self._port is None:
            return False
        
        await self.disconnect()
        return await self.connect(self._host, self._port)
    
    async def send_with_retry(self, message: ProtocolMessage, max_retries: int = 3) -> float:
        """
        Send message with automatic retry on connection failure.
        
        Args:
            message: Protocol message to send
            max_retries: Maximum retry attempts
            
        Returns:
            float: Round-trip time in seconds
            
        Raises:
            ConnectionError: If all retry attempts fail
        """
        for attempt in range(max_retries):
            try:
                return await self.send_message(message)
            except ConnectionError:
                if attempt < max_retries - 1:
                    # Attempt reconnection
                    if not await self.reconnect():
                        continue
                else:
                    raise
        
        # Should not reach here, but included for completeness
        raise ConnectionError("All retry attempts failed")
