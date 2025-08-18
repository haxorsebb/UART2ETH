#!/usr/bin/env python3
import socket
import time

def send_byte_by_byte(size, delay_ms=10):
    """Send message byte-by-byte with delay"""
    payload_size = size - 8
    msg = f"#ABCD{'A' * payload_size}!\r\n"
    
    print(f"Sending {len(msg)} bytes, one by one...")
    
    try:
        s = socket.socket()
        s.connect(('10.10.10.10', 4001))
        
        for i, byte in enumerate(msg.encode()):
            s.send(bytes([byte]))
            print(f"Sent byte {i+1}: 0x{byte:02X} ('{chr(byte) if 32 <= byte <= 126 else '?'}')")
            time.sleep(delay_ms / 1000.0)
            
        print("All bytes sent, waiting for response...")
        s.settimeout(3)
        response = s.recv(1024)
        print(f"Got response: {len(response)} bytes")
        return True
        
    except Exception as e:
        print(f"Error at byte {i+1}: {e}")
        return False
    finally:
        try:
            s.close()
        except:
            pass

# Test the problematic 539-byte message
send_byte_by_byte(539, delay_ms=50)  # 50ms between bytes
