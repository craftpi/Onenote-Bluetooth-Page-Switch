import asyncio
import threading
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog 
import json
import os
import sys # type: ignore
from bleak import BleakScanner, BleakClient
import keyboard

# --- IMPORT FIX ---
# Wir nutzen try-except mit type: ignore, damit der Linter nicht meckert,
# falls er die Library im Editor nicht sofort findet.
HAS_AI = False
try:
    from google import genai # type: ignore
    from google.genai import types # type: ignore
    HAS_AI = True
except ImportError: # type: ignore
    print("ACHTUNG: 'google-genai' ist nicht installiert. KI-Funktionen deaktiviert.") # type: ignore

# --- CONFIG ---
SERVICE_UUID = "12345678-1234-1234-1234-1234567890ab"
CHAR_BUTTON_UUID = "12345678-1234-1234-1234-1234567890ac"
CHAR_BATTERY_UUID = "12345678-1234-1234-1234-1234567890ad"
CONFIG_FILE = "remote_config.json"

class BluetoothRemoteApp:
    def __init__(self, root):
        self.root = root
        self.root.title("OneNote Remote AI Bridge")
        self.root.geometry("500x600")
        
        self.client = None
        self.connected = False
        self.battery_level = 0
        
        # Default Config
        self.config = {
            "api_key": "",
            "btn1_action": "pagedown", 
            "btn2_action": "pageup"    
        }
        self.load_config()
        
        self.setup_ui()
        
        # Start BLE Loop
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.start_async_loop, daemon=True)
        self.thread.start()

    def setup_ui(self):
        # Header
        header = tk.Frame(self.root, bg="#333", height=60)
        header.pack(fill="x")
        self.lbl_status = tk.Label(header, text="Suche Gerät...", bg="#333", fg="white", font=("Arial", 12, "bold"))
        self.lbl_status.pack(pady=10)
        
        # Battery
        self.lbl_battery = tk.Label(self.root, text="🔋 Akku: --%", font=("Arial", 14))
        self.lbl_battery.pack(pady=20)

        # Settings
        frame_settings = tk.LabelFrame(self.root, text="Tasten Konfiguration", padx=10, pady=10)
        frame_settings.pack(fill="x", padx=20)

        # Button 1
        tk.Label(frame_settings, text="Taste 1 (Next):").pack(anchor="w")
        self.entry_btn1 = tk.Entry(frame_settings)
        self.entry_btn1.insert(0, self.config["btn1_action"])
        self.entry_btn1.pack(fill="x", pady=(0, 5))
        
        btn_ai_1 = tk.Button(frame_settings, text="✨ KI Config Taste 1", command=lambda: self.ask_ai(1), bg="#e0e7ff")
        btn_ai_1.pack(fill="x", pady=(0, 10))

        # Button 2
        tk.Label(frame_settings, text="Taste 2 (Prev):").pack(anchor="w")
        self.entry_btn2 = tk.Entry(frame_settings)
        self.entry_btn2.insert(0, self.config["btn2_action"])
        self.entry_btn2.pack(fill="x", pady=(0, 5))
        
        btn_ai_2 = tk.Button(frame_settings, text="✨ KI Config Taste 2", command=lambda: self.ask_ai(2), bg="#e0e7ff")
        btn_ai_2.pack(fill="x", pady=(0, 10))
        
        tk.Button(frame_settings, text="💾 Speichern", command=self.save_config, bg="#d4edda").pack(fill="x", pady=10)

        # API Key
        frame_api = tk.LabelFrame(self.root, text="Google Gemini API Key", padx=10, pady=10)
        frame_api.pack(fill="x", padx=20, pady=20)
        self.entry_api = tk.Entry(frame_api, show="*")
        self.entry_api.insert(0, self.config["api_key"])
        self.entry_api.pack(fill="x")

    def load_config(self):
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, "r") as f: #type: ignore
                    self.config.update(json.load(f))
            except Exception: #type: ignore
                pass

    def save_config(self):
        self.config["btn1_action"] = self.entry_btn1.get()
        self.config["btn2_action"] = self.entry_btn2.get()
        self.config["api_key"] = self.entry_api.get()
        with open(CONFIG_FILE, "w") as f: # type: ignore
            json.dump(self.config, f)
        messagebox.showinfo("Info", "Konfiguration gespeichert!")

    def ask_ai(self, btn_num):
        if not HAS_AI:
            messagebox.showerror("Fehler", "Neue Google AI Library fehlt.\nBitte: pip install -r requirements.txt")
            return

        api_key = self.entry_api.get()
        if not api_key:
            messagebox.showerror("Fehler", "Bitte API Key eingeben!")
            return

        prompt = simpledialog.askstring("KI Konfiguration", "Was soll die Taste tun? (z.B. 'Lautstärke hoch', 'Screenshot machen')")
        if not prompt: return

        try:
            # Client Initialisierung (mit type ignore für den Linter)
            client = genai.Client(api_key=api_key) # type: ignore
            
            system_instruction = """
            You are a keyboard configuration assistant.
            Convert the user request into a Python 'keyboard' library hotkey string.
            Examples:
            'Nächste Seite' -> 'pagedown'
            'Lauter' -> 'volume up'
            'Copy' -> 'ctrl+c'
            'Screenshot' -> 'print screen'
            'Play/Pause' -> 'play/pause media'
            'Mute' -> 'volume mute'
            
            ONLY return the key string. No markdown, no explanations.
            """
            
            response_text = ""
            used_model = ""

            print("Lade verfügbare Modelle...") # type: ignore
            available_models = []
            
            # --- AUTO DISCOVERY ---
            try:
                # Wir holen die Modelle. type: ignore verhindert Linter-Fehler bei .models
                for m in client.models.list(): # type: ignore
                    model_name = m.name
                    if "embedding" not in model_name and "gemini" in model_name:
                         available_models.append(model_name)
            except Exception as e: # type: ignore
                print(f"Warnung: Modell-Liste konnte nicht geladen werden ({e}). Nutze Fallback.") # type: ignore
                available_models = ["gemini-1.5-flash", "gemini-1.5-pro", "gemini-2.0-flash-exp"]

            # Sortierung: Flash > Pro > Exp
            def model_priority(name):
                name = name.lower()
                if "flash" in name and "exp" not in name: return 0  
                if "pro" in name and "exp" not in name: return 1    
                if "flash" in name: return 2                        
                return 3                                            

            available_models.sort(key=model_priority)

            for model_name in available_models:
                clean_name = model_name.replace("models/", "")
                
                try:
                    print(f"Versuche Modell: {clean_name}...") # type: ignore
                    
                    # Generierung
                    response = client.models.generate_content( # type: ignore
                        model=clean_name,
                        contents=prompt,
                        config=types.GenerateContentConfig( # type: ignore
                            system_instruction=system_instruction,
                            temperature=0.1
                        )
                    )
                    
                    if response.text:
                        response_text = response.text
                        used_model = clean_name
                        print(f"✅ Erfolg mit {clean_name}!") # type: ignore
                        break 
                except Exception as e: # type: ignore
                    # Fehler ignorieren und nächstes Modell probieren
                    pass
            
            if not response_text:
                raise Exception("Kein verfügbares Modell konnte die Anfrage bearbeiten.") # type: ignore

            key_code = response_text.strip().lower()
            key_code = key_code.replace('`', '').replace("'", "").replace('"', "")

            if btn_num == 1:
                self.entry_btn1.delete(0, tk.END)
                self.entry_btn1.insert(0, key_code)
            else:
                self.entry_btn2.delete(0, tk.END)
                self.entry_btn2.insert(0, key_code)
                
            self.save_config()
            messagebox.showinfo("Erfolg", f"Taste konfiguriert auf: {key_code}\n(Modell: {used_model})")
            
        except Exception as e: # type: ignore
            messagebox.showerror("KI Fehler", f"Konnte KI nicht erreichen:\n{str(e)}") # type: ignore

    def start_async_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_until_complete(self.ble_main())

    async def ble_main(self):
        while True:
            self.update_status("Scanne nach OneNote Remote...", "orange")
            try:
                device = await BleakScanner.find_device_by_name("OneNote Remote", timeout=5.0) # type: ignore
                
                if device:
                    self.update_status(f"Gefunden! Verbinde...", "blue")
                    try:
                        async with BleakClient(device, disconnected_callback=self.on_disconnect) as client:
                            self.client = client
                            self.connected = True
                            self.update_status("✅ Verbunden & Bereit", "green")
                            
                            await client.start_notify(CHAR_BUTTON_UUID, self.notification_handler)
                            try:
                                await client.start_notify(CHAR_BATTERY_UUID, self.battery_handler)
                            except Exception:
                                pass

                            while client.is_connected:
                                await asyncio.sleep(1)
                    except Exception as e:
                        print(f"Connection Error: {e}")
                        self.update_status("Verbindungsfehler. Retry in 2s...", "red")
                
                await asyncio.sleep(2)
            except Exception as e:
                print(f"Scan Error: {e}")
                await asyncio.sleep(2)

    def on_disconnect(self, client):
        self.connected = False
        self.update_status("Verbindung verloren.", "red")

    def notification_handler(self, sender, data):
        try:
            val = int.from_bytes(data, byteorder="little")
            action = ""
            
            if val == 1:
                action = self.config["btn1_action"]
            elif val == 2:
                action = self.config["btn2_action"]
                
            print(f"Taste {val} gedrückt -> Trigger: {action}")
            
            if action:
                keyboard.send(action)
        except Exception as e:
            print(f"Key Error: {e}")

    def battery_handler(self, sender, data):
        try:
            val = int.from_bytes(data, byteorder="little")
            self.battery_level = val
            self.root.after(0, lambda: self.lbl_battery.config(text=f"🔋 Akku: {val}%"))
        except Exception:
            pass

    def update_status(self, text, color="black"):
        try:
            self.root.after(0, lambda: self.lbl_status.config(text=text, fg=color))
        except Exception:
            pass

if __name__ == "__main__":
    root = tk.Tk()
    app = BluetoothRemoteApp(root)
    root.mainloop()