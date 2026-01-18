from PIL import Image, ImageDraw, ImageFont
import os

def create_onenote_remote_icon():
    # Größe für das Icon (256x256 ist Standard für Windows Icons)
    size = (256, 256)
    image = Image.new('RGBA', size, (0, 0, 0, 0)) # Transparenter Hintergrund
    draw = ImageDraw.Draw(image)

    # Farben
    onenote_purple = (119, 25, 170) # Das typische OneNote Lila
    white = (255, 255, 255)
    dark_purple = (80, 10, 120)

    # 1. Hintergrund-Kreis (Remote Form)
    margin = 20
    draw.ellipse([margin, margin, size[0]-margin, size[1]-margin], fill=onenote_purple, outline=dark_purple, width=5)

    # 2. Das "N" Logo (stilisiert)
    # Wir zeichnen ein Buch/Notizbuch Symbol
    rect_margin = 70
    rect_w = size[0] - 2 * rect_margin
    rect_h = size[1] - 2 * rect_margin
    
    # Notizbuch Body
    draw.rectangle(
        [rect_margin, rect_margin, size[0]-rect_margin, size[1]-rect_margin], 
        fill=white, outline=white
    )
    
    # Das "N"
    # Da wir keine Schriftart garantieren können, zeichnen wir das N als Polygone
    n_color = onenote_purple
    start_x = rect_margin + 25
    end_x = size[0] - rect_margin - 25
    top_y = rect_margin + 20
    bottom_y = size[1] - rect_margin - 20
    width = 25

    # Linker Balken
    draw.rectangle([start_x, top_y, start_x + width, bottom_y], fill=n_color)
    # Rechter Balken
    draw.rectangle([end_x - width, top_y, end_x, bottom_y], fill=n_color)
    # Diagonale
    draw.polygon([
        (start_x, top_y), 
        (start_x + width, top_y), 
        (end_x, bottom_y), 
        (end_x - width, bottom_y)
    ], fill=n_color)

    # 3. Bluetooth Symbol (klein unten rechts)
    bt_center_x = size[0] - 50
    bt_center_y = size[1] - 50
    bt_size = 30
    
    # Kleiner Kreis für BT
    draw.ellipse(
        [bt_center_x - bt_size, bt_center_y - bt_size, bt_center_x + bt_size, bt_center_y + bt_size],
        fill=white, outline=dark_purple, width=3
    )
    
    # BT Runen-Striche (einfach)
    draw.line((bt_center_x, bt_center_y-15, bt_center_x, bt_center_y+15), fill=dark_purple, width=4)
    draw.line((bt_center_x, bt_center_y-15, bt_center_x+10, bt_center_y-5), fill=dark_purple, width=4)
    draw.line((bt_center_x+10, bt_center_y-5, bt_center_x-10, bt_center_y+5), fill=dark_purple, width=4)
    draw.line((bt_center_x-10, bt_center_y+5, bt_center_x+10, bt_center_y+15), fill=dark_purple, width=4)

    # Speichern als ICO
    # Windows Icons enthalten oft mehrere Größen. Pillow macht das automatisch gut, wenn man es als .ico speichert.
    image.save("icon.ico", format='ICO', sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)])
    print("Icon wurde erstellt: icon.ico")

if __name__ == "__main__":
    create_onenote_remote_icon()