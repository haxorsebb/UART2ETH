#!/usr/bin/env python3
import socket
import time

# Test the 539-byte message that hangs
payload_size = 539 - 8
msg = f"#ABCD{'A' * payload_size}!\r\n"

print(f"Sending {len(msg)} bytes that should hang...")
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('10.10.10.10', 4001))
    s.send(msg.encode())
    print("Message sent, waiting for response...")
    response = s.recv(1024)
    print(f"Got response: {len(response)} bytes")
except socket.timeout:
    print("Timeout as expected")
except Exception as e:
    print(f"Error: {e}")
finally:
    try:
        s.close()
    except:
        pass
