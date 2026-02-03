import serial
import threading
import re

# --- KONFIGURACJA ---
PORT_NANO = 'COM4'
PORT_ESP = 'COM3' # Zmień na swój port ESP
BAUD_NANO = 57600
BAUD_ESP = 57600

try:
    ser_nano = serial.Serial(PORT_NANO, BAUD_NANO, timeout=0.1)
    ser_esp = serial.Serial(PORT_ESP, BAUD_ESP, timeout=0.1)
except Exception as e:
    print(f"❌ BŁĄD PORTÓW: {e}")
    exit()

ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

def clean_log_line(line):
    return ansi_escape.sub('', line)

def esp_to_nano():
    while True:
        if ser_esp.in_waiting > 0:
            try:
                raw_line = ser_esp.readline().decode('utf-8', errors='replace')
                clean_line = clean_log_line(raw_line).strip()
                if "[SENDCMD]" in clean_line:
                    parts = clean_line.split(']', 1)
                    if len(parts) > 1:
                        cmd = parts[1].strip()
                        ser_nano.write((cmd + "\n").encode('utf-8'))
                        print(f"🚀 ESP -> NANO: '{cmd}'")
                elif clean_line:
                    print(f"☁️  ESP LOG: {clean_line}")
            except: pass

def nano_to_pc():
    while True:
        if ser_nano.in_waiting > 0:
            try:
                raw_line = ser_nano.readline().decode('utf-8', errors='replace').strip()
                if raw_line:
                    print(f"📟 NANO: {raw_line}")
            except: pass

# --- URUCHOMIENIE WĄTKÓW TŁA ---
threading.Thread(target=esp_to_nano, daemon=True).start()
threading.Thread(target=nano_to_pc, daemon=True).start()

print("-" * 50)
print("Bridge V3 Active")
print("Wpisz komendę i naciśnij Enter, aby wysłać do NANO.")
print("-" * 50)

# --- GŁÓWNA PĘTLA: OBSŁUGA KLAWIATURY ---
try:
    while True:
        # input() blokuje pętlę główną, ale wątki w tle działają niezależnie!
        user_cmd = input() 
        if user_cmd.strip():
            ser_nano.write((user_cmd.strip() + "\n").encode('utf-8'))
            print(f"⌨️  KLAWIATURA -> NANO: '{user_cmd.strip()}'")
except KeyboardInterrupt:
    print("\nZamykanie mostka...")
    ser_nano.close()
    ser_esp.close()