import serial
import threading
import re
import time

# --- TWOJA KONFIGURACJA ---
PORT_NANO = 'COM3'
PORT_ESP = 'COM4'
BAUD_NANO = 57600
BAUD_ESP = 57600

# Flaga kontrolująca pracę wątków
running = True

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
    global running
    while running:
        if ser_esp.in_waiting > 0:
            try:
                # Odczyt surowej linii
                raw_line = ser_esp.readline().decode('utf-8', errors='replace')
                
                # Czyścimy kolory ANSI i białe znaki
                clean_line = clean_log_line(raw_line).strip()
                
                if not clean_line:
                    continue

                # LOGIKA FILTROWANIA:
                # Logi systemowe zawsze zaczynają się od nagłówka w nawiasach, np. [DEBUG ], [INFO  ]
                if clean_line.startswith('['):
                    # To jest log z ESP, wypisujemy go tylko na konsolę bridge'a
                    print(f"☁️  ESP LOG: {clean_line}")
                else:
                    # To nie ma nagłówka, więc to "czysta" komenda przeznaczona dla Nano
                    ser_nano.write((clean_line + "\n").encode('utf-8'))
                    # Opcjonalne: logujemy w bridge'u, że przepchnęliśmy komendę
                    print(f"🚀 ESP -> NANO: '{clean_line}'")
                    
            except Exception as e:
                # print(f"Bridge Error: {e}")
                pass
        time.sleep(0.01) # Mała pauza dla CPU

def nano_relay():
    global running
    while running:
        if ser_nano.in_waiting > 0:
            try:
                # Odczytujemy linię z Nano
                raw_data = ser_nano.readline()
                raw_line = raw_data.decode('utf-8', errors='replace').strip()
                
                if raw_line:
                    # 1. Wypisz w konsoli PC (żebyś widział debug)
                    print(f"📟 NANO: {raw_line}")
                    
                    # 2. PRZEŚLIJ DO ESP (żeby ESP mogło to przetworzyć)
                    ser_esp.write((raw_line + "\n").encode('utf-8'))
            except: pass
        time.sleep(0.001)

t1 = threading.Thread(target=esp_to_nano)
t2 = threading.Thread(target=nano_relay)
t1.start()
t2.start()

print("-" * 50)
print("Bridge V4 Active (Stable Shutdown)")
print("-" * 50)

try:
    while True:
        user_cmd = input() 
        if user_cmd.strip():
            ser_nano.write((user_cmd.strip() + "\n").encode('utf-8'))
            print(f"⌨️  KLAWIATURA -> NANO: '{user_cmd.strip()}'")
except KeyboardInterrupt:
    print("\n🛑 Zamykanie mostka...")
    running = False # Zatrzymujemy pętle w wątkach
    t1.join(timeout=1) # Czekamy aż wątki skończą
    t2.join(timeout=1)
    ser_nano.close()
    ser_esp.close()
    print("👋 Do zobaczenia!")