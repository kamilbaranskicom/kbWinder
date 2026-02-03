import serial
import threading
import re

# Konfiguracja portów
ser_nano = serial.Serial('COM4', 115200, timeout=0.1)
ser_esp = serial.Serial('COM3', 115200, timeout=0.1)

# Regex do usuwania kolorów ANSI (standardowy wzorzec)
ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

def clean_log_line(line):
    """Usuwa kody ANSI z linii tekstu."""
    return ansi_escape.sub('', line)

def esp_to_nano():
    while True:
        if ser_esp.in_waiting > 0:
            # Czytamy całą linię z ESP
            raw_line = ser_esp.readline().decode('utf-8', errors='replace')
            if not raw_line: continue
            
            # 1. Czyścimy z kolorów ANSI i białych znaków
            clean_line = clean_log_line(raw_line).strip()
            
            # 2. Sprawdzamy czy linia zawiera nasz tag [SENDCMD]
            if "[SENDCMD]" in clean_line:
                # Wyciągamy to co jest PO zamknięciu nawiasu ]
                # Przykład: "[SENDCMD] W 10 100" -> "W 10 100"
                parts = clean_line.split(']', 1)
                if len(parts) > 1:
                    cmd_to_send = parts[1].strip()
                    
                    # 3. Wysyłamy czystą komendę do Nano
                    ser_nano.write((cmd_to_send + "\n").encode('utf-8'))
                    print(f"🚀 FORWARDED TO NANO: '{cmd_to_send}'")
            else:
                # Opcjonalnie: logujemy inne wiadomości z ESP tylko do konsoli PC
                print(f"   [ESP LOG]: {clean_line}")

def nano_to_esp():
    while True:
        if ser_nano.in_waiting > 0:
            raw_line = ser_nano.readline().decode('utf-8', errors='replace').strip()
            if raw_line:
                # Przekazujemy info z Nano do ESP (np. statusy), żeby ESP widziało co się dzieje
                # Tutaj nie musimy filtrować, chyba że chcesz
                print(f"📟 FROM NANO: {raw_line}")
                ser_esp.write((raw_line + "\n").encode('utf-8'))

# Uruchomienie wątków
threading.Thread(target=esp_to_nano, daemon=True).start()
threading.Thread(target=nano_to_esp, daemon=True).start()

print("Bridge V2 active. Filtering [SENDCMD] labels...")
while True:
    pass