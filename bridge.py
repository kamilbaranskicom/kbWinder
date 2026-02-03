import serial
import threading

# Konfiguracja portów (zmień na swoje COMx)
ser_nano = serial.Serial('COM4', 57600, timeout=0.1) # 5V Nano
ser_esp = serial.Serial('COM3', 57600, timeout=0.1)  # 3.3V ESP

def bridge(src, dest, label):
    while True:
        if src.in_waiting > 0:
            data = src.read(src.in_waiting)
            dest.write(data)
            # Wyświetlanie w konsoli
            print(f"[{label}]: {data.decode('utf-8', errors='replace').strip()}")

# Uruchamiamy dwa wątki do przesyłu w obie strony
threading.Thread(target=bridge, args=(ser_nano, ser_esp, "NANO -> ESP"), daemon=True).start()
threading.Thread(target=bridge, args=(ser_esp, ser_nano, "ESP -> NANO"), daemon=True).start()

print("Bridge active. Press Ctrl+C to stop.")
while True:
    pass