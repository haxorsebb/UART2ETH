#!/usr/bin/env python3
import socket
import re
from datetime import datetime

def decode_software_version(version_hex):
    """
    Decode software version from 32-bit hex value.
    Format: 1 byte per digit, lowest byte a-z for minor versions
    """
    try:
        version_int = int(version_hex, 16)
        
        # Extract 4 bytes (8 hex chars = 4 bytes)
        byte1 = (version_int >> 24) & 0xFF
        byte2 = (version_int >> 16) & 0xFF  
        byte3 = (version_int >> 8) & 0xFF
        byte4 = version_int & 0xFF
        
        # Convert to version string
        # Based on example: 01010616 hex = V1.16w
        major = byte1
        minor1 = byte2
        minor2 = byte3
        
        # Last byte: if > 25, it's a number, otherwise convert to letter
        if byte4 <= 25:
            suffix = chr(ord('a') + byte4)
        else:
            suffix = str(byte4)
            
        return f"V{major}.{minor1}{minor2}{suffix}"
    except:
        return f"Unknown ({version_hex})"

def decode_status(status_hex):
    """Decode status byte"""
    status_map = {
        "00": "OK",
        "01": "ERROR", 
        "02": "METAL"
    }
    return status_map.get(status_hex.upper(), f"Unknown ({status_hex})")

def parse_status_response(response):
    """Parse the full status response according to SHARKNET 2 protocol"""
    
    # Remove CR/LF and whitespace
    response = response.strip()
    
    # Expected format: #0000QQRRRRRRRRSSSSSSSSTTTTTTTTUUUUUUUUVVVVVVVVWW!
    # Check if it matches the expected pattern
    pattern = r'^#0000([0-9A-Fa-f]{2})([0-9A-Fa-f]{8})([0-9A-Fa-f]{8})([0-9A-Fa-f]{8})([0-9A-Fa-f]{8})([0-9A-Fa-f]{8})([0-9A-Fa-f]{2})!?$'
    
    match = re.match(pattern, response)
    if not match:
        return f"Invalid response format: {response}"
    
    unit_hex, metal_hex, total_hex, uid_low_hex, uid_high_hex, version_hex, status_hex = match.groups()
    
    # Convert hex values to decimal
    unit_num = int(unit_hex, 16)
    metal_counter = int(metal_hex, 16)  
    total_counter = int(total_hex, 16)
    uid_low = int(uid_low_hex, 16)
    uid_high = int(uid_high_hex, 16)
    
    # Combine unique ID
    unique_id = (uid_high << 32) | uid_low
    
    # Decode version and status
    software_version = decode_software_version(version_hex)
    status = decode_status(status_hex)
    
    # Format results
    result = f"""
SHARK 2 Status Response:
========================
Unit Number:      {unit_num}
Metal Counter:    {metal_counter:,}
Total Counter:    {total_counter:,}
Unique ID:        {unique_id:016X} (High: {uid_high:08X}, Low: {uid_low:08X})
Software Version: {software_version}
Status:           {status}

Raw Response:     {response}
Parsed Fields:
  Unit:           {unit_hex} (0x{unit_hex})
  Metal Count:    {metal_hex} (0x{metal_hex})
  Total Count:    {total_hex} (0x{total_hex})  
  UID Low:        {uid_low_hex} (0x{uid_low_hex})
  UID High:       {uid_high_hex} (0x{uid_high_hex})
  Version:        {version_hex} (0x{version_hex})
  Status:         {status_hex} (0x{status_hex})
"""
    return result

def send_shark_status(ip, port, unit_number=1, timeout=5):
    """Send status request to SHARK 2 device and decode response"""
    
    # Convert unit number to 2-digit hex
    unit_hex = f"{unit_number:02X}"
    command = f"#0000{unit_hex}!\r\n"
    
    print(f"Connecting to {ip}:{port}")
    print(f"Sending command: {repr(command)}")
    print(f"Requesting status for unit #{unit_number}")
    print("-" * 50)
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((ip, port))
        
        # Send command
        sock.send(command.encode('ascii'))
        while(1):
            # Receive response
            response = sock.recv(1024).decode('ascii')
            
            if response:
                print("Raw response received:")
                print(repr(response))
                print("-" * 50)
                
                # Parse and decode the response
                decoded = parse_status_response(response)
                print(decoded)
                
                return response
            else:
                print("No response received")
                return None
                
    except socket.timeout:
        print(f"Connection timeout after {timeout} seconds")
    except ConnectionRefusedError:
        print(f"Connection refused to {ip}:{port}")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        try:
            sock.close()
        except:
            pass
    
    return None

def main():
    """Main function - modify these parameters as needed"""
    
    # Configuration
    SHARK_IP = "192.168.1.201"
    SHARK_PORT = 4002
    UNIT_NUMBER = 0  # Change this for different units
    
    print(f"SHARK 2 Status Request Tool")
    print(f"Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 50)
    
    # Send status request
    response = send_shark_status(SHARK_IP, SHARK_PORT, UNIT_NUMBER)
    
    if not response:
        print("\nNo valid response received. Check:")
        print("- Network connectivity to device")  
        print("- Device IP address and port")
        print("- Device power and network status")
        print("- Firewall settings")

if __name__ == "__main__":
    main()
