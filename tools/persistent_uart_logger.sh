#!/bin/bash
#
# Persistent UART Logger for RP2350 Development
# Provides continuous logging from /dev/ttyUSB0 to a fixed logfile
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
LOGS_DIR="$PROJECT_ROOT/logs"
LOGFILE="$LOGS_DIR/uart_output.log"
SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
PIDFILE="$LOGS_DIR/uart_logger.pid"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

info() {
    echo -e "${BLUE}[UART-LOG]${NC} $1" >&2
}

success() {
    echo -e "${GREEN}[UART-LOG]${NC} $1" >&2
}

warning() {
    echo -e "${YELLOW}[UART-LOG]${NC} $1" >&2
}

error() {
    echo -e "${RED}[UART-LOG]${NC} $1" >&2
}

usage() {
    echo "Usage: $0 {start|stop|restart|truncate|status|tail}"
    echo ""
    echo "Commands:"
    echo "  start     - Start persistent logging from $SERIAL_PORT"
    echo "  stop      - Stop persistent logging"
    echo "  restart   - Stop and start logging"
    echo "  truncate  - Clear the logfile (keeps logging running)"  
    echo "  status    - Show logging status"
    echo "  tail      - Show live output from logfile"
    echo ""
    echo "Environment Variables:"
    echo "  SERIAL_PORT   Serial device (default: $SERIAL_PORT)"
    echo ""
    echo "Log file: $LOGFILE"
    exit 1
}

check_serial_port() {
    if [ ! -c "$SERIAL_PORT" ]; then
        error "Serial port $SERIAL_PORT does not exist"
        return 1
    fi
    
    if [ ! -r "$SERIAL_PORT" ]; then
        error "No read permission for $SERIAL_PORT"
        error "Make sure you're in the dialout group: sudo usermod -a -G dialout \$USER"
        return 1
    fi
    
    return 0
}

is_logging_active() {
    if [ -f "$PIDFILE" ]; then
        local pid=$(cat "$PIDFILE")
        if kill -0 "$pid" 2>/dev/null; then
            return 0  # Active
        else
            # Stale PID file
            rm -f "$PIDFILE"
            return 1  # Not active
        fi
    fi
    return 1  # Not active
}

start_logging() {
    if is_logging_active; then
        warning "Logging is already active (PID: $(cat "$PIDFILE"))"
        return 0
    fi
    
    if ! check_serial_port; then
        return 1
    fi
    
    # Create logs directory
    mkdir -p "$LOGS_DIR"
    
    info "Starting persistent logging from $SERIAL_PORT to $LOGFILE"
    
    # Configure serial port settings
    stty -F "$SERIAL_PORT" 115200 raw clocal min 0 -echo 2>/dev/null || true
    
    # Start tail -f in background and capture its PID
    nohup tail -f "$SERIAL_PORT" >> "$LOGFILE" 2>/dev/null &
    local logger_pid=$!
    
    # Save PID
    echo "$logger_pid" > "$PIDFILE"
    
    # Verify it started successfully
    sleep 0.5
    if is_logging_active; then
        success "Logging started successfully (PID: $logger_pid)"
        info "Use '$0 tail' to view live output"
        return 0
    else
        error "Failed to start logging"
        return 1
    fi
}

stop_logging() {
    if ! is_logging_active; then
        warning "Logging is not active"
        return 0
    fi
    
    local pid=$(cat "$PIDFILE")
    info "Stopping logging process (PID: $pid)"
    
    if kill "$pid" 2>/dev/null; then
        rm -f "$PIDFILE"
        success "Logging stopped"
        return 0
    else
        error "Failed to stop logging process"
        rm -f "$PIDFILE"  # Clean up stale PID file
        return 1
    fi
}

truncate_log() {
    info "Truncating logfile: $LOGFILE"
    truncate -s0 "$LOGFILE" 2>/dev/null || touch "$LOGFILE"
    success "Logfile cleared"
}

show_status() {
    echo "=== UART Logger Status ==="
    echo "Serial Port: $SERIAL_PORT"
    echo "Log File: $LOGFILE"
    echo ""
    
    if is_logging_active; then
        local pid=$(cat "$PIDFILE")
        echo -e "Status: ${GREEN}ACTIVE${NC} (PID: $pid)"
        
        # Show log file info
        if [ -f "$LOGFILE" ]; then
            local size=$(stat -c%s "$LOGFILE" 2>/dev/null || echo "unknown")
            local lines=$(wc -l < "$LOGFILE" 2>/dev/null || echo "unknown")
            echo "Log Size: $size bytes, $lines lines"
            echo "Last Modified: $(stat -c%y "$LOGFILE" 2>/dev/null || echo "unknown")"
        else
            echo "Log File: Not created yet"
        fi
    else
        echo -e "Status: ${RED}INACTIVE${NC}"
    fi
    
    # Check serial port status
    echo ""
    if [ -c "$SERIAL_PORT" ]; then
        echo -e "Serial Port: ${GREEN}AVAILABLE${NC}"
    else
        echo -e "Serial Port: ${RED}NOT FOUND${NC}"
    fi
}

tail_log() {
    if [ ! -f "$LOGFILE" ]; then
        error "Log file does not exist: $LOGFILE"
        return 1
    fi
    
    info "Showing live output from $LOGFILE (Ctrl+C to stop)..."
    echo "=================================="
    tail -f "$LOGFILE"
}

# Main command handling
case "${1:-}" in
    start)
        start_logging
        ;;
    stop)
        stop_logging
        ;;
    restart)
        stop_logging
        sleep 1
        start_logging
        ;;
    truncate)
        truncate_log
        ;;
    status)
        show_status
        ;;
    tail)
        tail_log
        ;;
    *)
        usage
        ;;
esac
