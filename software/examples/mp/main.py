import machine
import time
import sys

# ===== CONFIGURA TUS PINES I2C =====
SDA_PIN = 6
SCL_PIN = 7
# ===================================

FRAM_BASE = 0x50
fram_addr = FRAM_BASE

i2c = machine.I2C(0, scl=machine.Pin(SCL_PIN), sda=machine.Pin(SDA_PIN), freq=100_000)

def fram_write(mem, data):
    data = bytes(data)
    buf = bytes([(mem >> 8) & 0xFF, mem & 0xFF]) + data
    try:
        i2c.writeto(fram_addr, buf)
        return True
    except OSError:
        return False

def fram_read(mem, length):
    try:
        i2c.writeto(fram_addr, bytes([(mem >> 8) & 0xFF, mem & 0xFF]), False)
        buf = i2c.readfrom(fram_addr, length)
        return buf
    except OSError:
        return None

def dump_hex(buf):
    for i, b in enumerate(buf):
        if i and i % 16 == 0:
            print()
        print("{:02X} ".format(b), end='')
    print()

def cmd_scan():
    print("\n[I2C] Escaneando...")
    any_found = False
    picked = False
    global fram_addr
    for addr in range(1, 127):
        try:
            i2c.writeto(addr, b'')
            any_found = True
            print("  - Dispositivo en 0x{:02X}".format(addr), end='')
            if 0x50 <= addr <= 0x57:
                print("  (FRAM candidata)", end='')
                if not picked:
                    fram_addr = addr
                    picked = True
            print()
        except OSError:
            pass
        time.sleep_ms(2)
    if not any_found:
        print("  (Nada encontrado)")
    if picked:
        print("Usando FRAM en 0x{:02X}".format(fram_addr))
    else:
        print("No se detectó FRAM (0x50–0x57).")

def cmd_wpcheck(mem_addr):
    orig = fram_read(mem_addr, 1)
    if orig is None:
        print("Lectura inicial falló.")
        return
    orig = orig[0]
    test = 0x00 if orig == 0xFF else 0xFF
    if not fram_write(mem_addr, bytes([test])):
        print("Escritura NACK → posible WP ACTIVA o bus error.")
        return
    after = fram_read(mem_addr, 1)
    if after is None:
        print("Lectura posterior falló.")
        return
    after = after[0]
    if after != test:
        print("WP ACTIVA: Byte no cambió (0x{:02X} -> 0x{:02X} leído 0x{:02X})".format(orig, test, after))
        return
    # Restaurar valor original
    if not fram_write(mem_addr, bytes([orig])):
        print("¡Atención! No se pudo restaurar el valor original.")
    else:
        after = fram_read(mem_addr, 1)[0]
    print("WP DESACTIVADA: se pudo escribir y restaurar (final=0x{:02X})".format(after))

def cmd_read(addr, length):
    if length <= 0 or length > 256:
        print("len max 256")
        return
    buf = fram_read(addr, length)
    if buf is not None:
        print("HEX:")
        dump_hex(buf)
        print("ASCII: ", end='')
        for b in buf:
            print(chr(b) if 32 <= b < 127 else '.', end='')
        print()
    else:
        print("Error de lectura")

def cmd_write(addr, text):
    data = text.encode()
    if fram_write(addr, data):
        print("Escrito en 0x{:04X}".format(addr))
    else:
        print("Error de escritura (WP alto, dirección o bus).")

def cmd_erase():
    print("Borrando toda la FRAM (0xFF)...")
    total = 32768  # 256 Kbit = 32 KB
    ff = bytes([0xFF] * 32)
    for a in range(0, total, 32):
        if not fram_write(a, ff):
            print("Fallo en 0x{:04X}".format(a))
            break
    print("Borrado completo.")

def cmd_addr_set(addr):
    global fram_addr
    fram_addr = addr
    print("Dirección FRAM seleccionada: 0x{:02X}".format(fram_addr))

def handle_command(line):
    line = line.strip()
    if not line:
        return
    if line == "scan":
        cmd_scan()
        return
    if line.startswith("addr "):
        s = line[5:]
        v = int(s, 16) if s.startswith("0x") else int(s)
        cmd_addr_set(v)
        return
    if line.startswith("read "):
        parts = line.split()
        if len(parts) < 3:
            print("Uso: read <addr> <len>")
            return
        a, b = parts[1], parts[2]
        addr = int(a, 16) if a.startswith("0x") else int(a)
        length = int(b)
        cmd_read(addr, length)
        return
    if line.startswith("write "):
        parts = line.split(' ', 2)
        if len(parts) < 3:
            print("Uso: write <addr> <texto>")
            return
        a, text = parts[1], parts[2]
        addr = int(a, 16) if a.startswith("0x") else int(a)
        cmd_write(addr, text)
        return
    if line == "erase":
        cmd_erase()
        return
    if line.startswith("wpcheck"):
        parts = line.split()
        m = 0x0000
        if len(parts) > 1:
            a = parts[1]
            m = int(a, 16) if a.startswith("0x") else int(a)
        cmd_wpcheck(m)
        return
    print("Comandos:")
    print("  scan")
    print("  addr <0x50..0x57>")
    print("  read <addr> <len>     (addr admite 0x..)")
    print("  write <addr> <texto>")
    print("  erase")
    print("  wpcheck [addr]        (prueba WP; por defecto 0x0000)")

def main_loop():
    print("Monitor I2C-FRAM listo. Escribe 'scan' para detectar la FRAM.")
    while True:
        try:
            line = input('> ')
            handle_command(line)
        except KeyboardInterrupt:
            print("\nSaliendo...")
            break

if __name__ == "__main__":
    main_loop()
