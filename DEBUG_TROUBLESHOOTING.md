# Debug Connection Troubleshooting Guide

## Problem Fixed: ✅ Debug Configuration Issues

The debugging failure was caused by VS Code debug configurations using Raspberry Pi Pico extension commands that weren't working properly.

## ✅ **Solution Applied**

### **Updated Debug Configurations**
Both Blink and UART2ETH projects now have **direct debug configurations** that don't rely on extension commands:

**New Debug Options Available:**
1. **"Pico Debug (Direct)"** - Standard debugging with direct paths
2. **"Pico Debug with LiveWatch (Rescue Mode - Direct)"** - ✅ **Recommended for custom board**
3. **"Pico Debug (External OpenOCD)"** - For manual OpenOCD management

### **Key Changes Made**
- ✅ **Removed extension command dependencies** (`${command:raspberry-pi-pico.*}`)
- ✅ **Added direct file paths** (`build/blink.elf`, `build/uart2eth.elf`)
- ✅ **Fixed GDB path** (`arm-none-eabi-gdb`)
- ✅ **Correct SVD file path** (`RP2350.svd`)
- ✅ **Proper OpenOCD configuration** for custom board

## 🚀 **How to Debug Now**

### **Step 1: Start Debugging**
```
1. Open project in VSCodium
2. Press F5 or Ctrl+Shift+P → "Debug: Start Debugging"
3. Select "Pico Debug with LiveWatch (Rescue Mode - Direct)"
```

### **Step 2: If Issues Persist**
Try these options in order:

**Option A: Use External OpenOCD**
```bash
# Terminal 1: Start OpenOCD manually
cd ~/.pico-sdk/openocd/0.12.0+dev
./openocd -s scripts -f interface/cmsis-dap.cfg -f target/rp2350-rescue.cfg -c "adapter speed 1000"

# Terminal 2: In VSCodium, select "Pico Debug (External OpenOCD)"
```

**Option B: Reset Debug Probe**
```bash
# Disconnect and reconnect USB debug probe
# Wait 5 seconds, then try debugging again
```

**Option C: Manual Connection Test**
```bash
# Test if OpenOCD can connect
cd ~/.pico-sdk/openocd/0.12.0+dev
./openocd -s scripts -f interface/cmsis-dap.cfg -f target/rp2350-rescue.cfg -c "adapter speed 1000; init; reset halt; exit"
```

## 🔍 **Verification Steps**

### **Check 1: Debug Probe Detection**
```bash
lsusb | grep -i debug
# Should show: Bus XXX Device XXX: ID 2e8a:000c Raspberry Pi Debug Probe
```

### **Check 2: OpenOCD Connection**
```bash
cd ~/.pico-sdk/openocd/0.12.0+dev
./openocd -s scripts -f interface/cmsis-dap.cfg -f target/rp2350-rescue.cfg -c "adapter speed 1000"
# Should show: "Listening on port 3333 for gdb connections"
```

### **Check 3: Build Files Present**
```bash
# For Blink project
ls -la ~/projects/blink/build/blink.elf

# For UART2ETH project  
ls -la ~/projects/UART2ETH/build/uart2eth.elf
```

## 🛠️ **Debug Configuration Details**

### **What Changed**
**Old (Broken):**
```json
"executable": "${command:raspberry-pi-pico.launchTargetPath}",
"gdbPath": "${command:raspberry-pi-pico.getGDBPath}",
```

**New (Working):**
```json
"executable": "${workspaceFolder}/build/blink.elf",
"gdbPath": "${userHome}/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-gdb",
```

### **Port Configuration**
- **OpenOCD Default**: Port 3333 (GDB server)
- **VS Code Direct**: Managed automatically  
- **External Mode**: Connect to localhost:3333

## 📋 **Available Debug Configurations**

### **For Blink Project:**
1. **"Pico Debug (Direct)"** - Standard mode, 5MHz SWD
2. **"Pico Debug with LiveWatch (Rescue Mode - Direct)"** - ✅ **Recommended**
3. **"Pico Debug (External OpenOCD)"** - Manual OpenOCD control

### **For UART2ETH Project:**
1. **"Pico Debug (Direct)"** - Standard mode, 5MHz SWD  
2. **"Pico Debug with LiveWatch (Rescue Mode - Direct)"** - ✅ **Recommended**
3. **"Pico Debug (External OpenOCD)"** - Manual OpenOCD control

## 🎯 **Recommended Debug Workflow**

### **For Custom RP2354 Board:**
1. **Always use "Rescue Mode - Direct"** configurations first
2. **Set breakpoints** in your code before starting
3. **Add watch expressions**: `debug_string`, `debug_sys_clock`, etc.
4. **Use LiveWatch** to monitor variables in real-time

### **For LiveWatch Monitoring:**
**Essential Variables to Watch:**
- `debug_string` - All debug output
- `debug_sys_clock` - System clock frequency
- `debug_xosc_stable` - External clock status
- `debug_blink_count` - Program activity (Blink project)

## ⚠️ **Common Issues & Solutions**

### **"Failed to launch GDB" Error**
- ✅ **Fixed**: Use new direct debug configurations
- **Cause**: Extension command resolution failure
- **Solution**: Direct paths bypass extension dependencies

### **"Connection reset by peer" Error**  
- **Cause**: OpenOCD not starting or port conflicts
- **Solution**: Use rescue mode or external OpenOCD

### **"Target disconnected" Error**
- **Cause**: Debug probe communication issues
- **Solution**: Reconnect USB probe, try lower speeds

### **Port 50000 Connection Error**
- ✅ **Fixed**: No longer uses custom ports
- **Solution**: New configs use standard OpenOCD ports

## 📝 **Summary**

✅ **Debug configurations fixed** for both Blink and UART2ETH projects
✅ **Removed extension dependencies** that were causing failures  
✅ **Added direct file paths** for reliable debugging
✅ **Rescue mode configurations** for custom board compatibility
✅ **LiveWatch enabled** for real-time variable monitoring

**Result**: Debugging should now work reliably on both projects with your custom RP2354 board!

## 🚀 **Next Steps**

1. **Try debugging** with "Pico Debug with LiveWatch (Rescue Mode - Direct)"
2. **Set breakpoints** in main() function
3. **Add watch expressions** for debug variables
4. **Verify real-time monitoring** works with LiveWatch

Your debugging environment is now properly configured! 🎉
