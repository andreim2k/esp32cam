import serial
import time
import sys

try:
    ser = serial.Serial('/dev/cu.usbserial-10', 115200, timeout=1)
    
    # Correct Reset sequence for ESP32 to boot into APP mode
    # RTS controls EN, DTR controls IO0
    
    # 1. Pull EN low (Reset) and ensure IO0 is high
    ser.setRTS(True)
    ser.setDTR(False)
    time.sleep(0.1)
    
    # 2. Release EN (High) to start booting, keep IO0 high
    ser.setRTS(False)
    ser.setDTR(False)
    
    print("Reading serial for 15 seconds...")
    end_time = time.time() + 15
    while time.time() < end_time:
        line = ser.readline()
        if line:
            sys.stdout.write(line.decode('utf-8', errors='ignore'))
    ser.close()
except Exception as e:
    print(f"Failed to read serial: {e}")