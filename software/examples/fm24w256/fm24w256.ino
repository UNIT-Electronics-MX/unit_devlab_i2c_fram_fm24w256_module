#include <Wire.h>

// ===== CONFIGURA TUS PINES I2C =====
#define SDA_PIN 6
#define SCL_PIN 7
// ===================================

#define EEPROM_BASE 0x50
uint8_t eepromAddr = EEPROM_BASE;

// ---------- Estado detectado ----------
uint8_t  eepromAddrWidth = 2;      // 2 = direccionamiento de 16 bits; 1 = 8 bits (24C16 y menores)
uint32_t eepromSizeBytes = 4096;   // por defecto 32 Kbit (4 KB)
size_t   eepromPageSize  = 32;     // se ajusta tras 'capacity'

// ---------- Utilidades ----------
void eepromWaitReady(uint32_t timeout = 25) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeout) {
    Wire.beginTransmission(eepromAddr);
    if (Wire.endTransmission() == 0) return;
    delay(1);
  }
}

// Dev addr para chips pequeños (24C01–24C16 usan bloques de 256 B mapeados en la dirección del dispositivo)
static inline uint8_t devAddrFor(uint16_t mem) {
  if (eepromAddrWidth == 2) return eepromAddr;
  uint8_t base = eepromAddr & 0xF8;       // mantener A2..A0 base en 0
  uint8_t block = (uint8_t)(mem >> 8);    // cada bloque = 256 bytes
  return (uint8_t)(base | (block & 0x07));
}

bool eepromSetPointer(uint16_t mem) {
  uint8_t dev = devAddrFor(mem);
  Wire.beginTransmission(dev);
  if (eepromAddrWidth == 2) {
    Wire.write(mem >> 8);
    Wire.write(mem & 0xFF);
  } else {
    Wire.write((uint8_t)(mem & 0xFF));
  }
  return (Wire.endTransmission(false) == 0);
}

bool eepromWritePaged(uint16_t mem, const uint8_t *data, size_t len, size_t page) {
  size_t off = 0;
  while (off < len) {
    size_t pageSpace = page - ((mem + off) % page);
    size_t chunk = min(len - off, pageSpace);
    if (eepromAddrWidth == 1) {
      size_t blockSpace = 256 - ((mem + off) & 0xFF);  // no cruzar bloque de 256 B
      if (chunk > blockSpace) chunk = blockSpace;
    }
    uint8_t dev = devAddrFor(mem + off);
    Wire.beginTransmission(dev);
    if (eepromAddrWidth == 2) {
      Wire.write((uint8_t)((mem + off) >> 8));
      Wire.write((uint8_t)((mem + off) & 0xFF));
    } else {
      Wire.write((uint8_t)((mem + off) & 0xFF));
    }
    Wire.write(data + off, chunk);
    if (Wire.endTransmission() != 0) return false;
    eepromWaitReady();
    off += chunk;
  }
  return true;
}

bool eepromReadSeq(uint16_t mem, uint8_t *buf, size_t len) {
  size_t got = 0;
  while (got < len) {
    if (!eepromSetPointer(mem + got)) return false;
    size_t req = min((size_t)32, len - got);
    if (eepromAddrWidth == 1) {
      size_t blockRemaining = 256 - ((mem + got) & 0xFF);
      if (req > blockRemaining) req = blockRemaining;
    }
    uint8_t dev = devAddrFor(mem + got);
    if (Wire.requestFrom((int)dev, (int)req) != (int)req) return false;
    for (size_t i = 0; i < req; i++) buf[got++] = Wire.read();
  }
  return true;
}

void dumpHex(const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (i && i % 16 == 0) Serial.println();
    Serial.printf("%02X ", buf[i]);
  }
  Serial.println();
}

// ---------- Detección ----------
bool probeTwoByteMode() {
  Wire.beginTransmission(eepromAddr);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;
  int r = Wire.requestFrom((int)eepromAddr, 1);
  if (r != 1) return false;
  (void)Wire.read();
  return true;
}

bool probeOneByteMode() {
  uint8_t base = (uint8_t)(eepromAddr & 0xF8);
  Wire.beginTransmission(base);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;
  int r = Wire.requestFrom((int)base, 1);
  if (r != 1) return false;
  (void)Wire.read();
  eepromAddr = base; // normalizar a bloque 0
  return true;
}

uint32_t eepromDetectSize(bool verbose = true) {
  // 1) Modo de direccionamiento
  if (probeTwoByteMode()) eepromAddrWidth = 2;
  else if (probeOneByteMode()) eepromAddrWidth = 1;
  else {
    if (verbose) Serial.println("No se pudo comunicar con la EEPROM.");
    return 0;
  }

  // 2) Conjunto de candidatos (bytes)
  const uint32_t cand2[] = { 4096, 8192, 16384, 32768, 65536 }; // 32/64/128/256/512 Kbit
  const uint32_t cand1[] = { 128, 256, 512, 1024, 2048 };       // 1/2/4/8/16 Kbit
  const uint32_t* arr = (eepromAddrWidth == 2) ? cand2 : cand1;
  const size_t    n   = 5;

  const uint8_t V1 = 0xA5, V2 = 0x5A;
  const uint16_t A = 0x0010; // zona de prueba
  uint8_t origA = 0xFF;
  if (!eepromReadSeq(A, &origA, 1)) { if (verbose) Serial.println("Lectura A falló."); return 0; }

  uint32_t size = 0;
  for (size_t i = 0; i < n; i++) {
    uint32_t S = arr[i];
    uint16_t B = (uint16_t)(A + (S & 0xFFFF)); // nuestro API usa 16 bits
    uint8_t origB = 0xFF;
    eepromReadSeq(B, &origB, 1);

    // Escribir patrones y verificar alias (si S == tamaño real, B envuelve a A)
    eepromWritePaged(A, &V1, 1, eepromPageSize);
    eepromWritePaged(B, &V2, 1, eepromPageSize);
    uint8_t a2 = 0, b2 = 0;
    eepromReadSeq(A, &a2, 1);
    eepromReadSeq(B, &b2, 1);

    // Restaurar antes de continuar (minimiza desgaste/datos sucios)
    eepromWritePaged(A, &origA, 1, eepromPageSize);
    eepromWritePaged(B, &origB, 1, eepromPageSize);
    eepromWaitReady();

    bool alias = (a2 == V2) && (b2 == V2);
    if (alias) { size = S; break; }
  }

  if (size == 0) size = (eepromAddrWidth == 2) ? 65536u : 2048u; // asumir máximo del modo

  eepromSizeBytes = size;

  // 3) Estimar tamaño de página
  if (eepromAddrWidth == 2) {
    if      (size <=  4096) eepromPageSize = 32;   // 24C32
    else if (size <=  8192) eepromPageSize = 32;   // 24C64 (común 32)
    else if (size <= 16384) eepromPageSize = 64;   // 24C128
    else if (size <= 32768) eepromPageSize = 64;   // 24C256
    else                    eepromPageSize = 128;  // 24C512
  } else {
    eepromPageSize = 16; // 24C01–24C16 típicamente 8–16; elegimos 16 seguro
  }

  if (verbose) {
    Serial.printf("Modo: %s-byte\n", (eepromAddrWidth == 2) ? "16(2)" : "8(1)");
    Serial.printf("Capacidad estimada: %lu bytes (≈ %lu Kbit)\n",
                  (unsigned long)eepromSizeBytes, (unsigned long)(eepromSizeBytes * 8 / 1024));
    Serial.printf("Página estimada: %u bytes\n", (unsigned)eepromPageSize);
  }
  return size;
}

void printHumanCapacity() {
  const char* name = "?";
  if (eepromAddrWidth == 2) {
    if      (eepromSizeBytes <=  4096) name = "24C32";
    else if (eepromSizeBytes <=  8192) name = "24C64";
    else if (eepromSizeBytes <= 16384) name = "24C128";
    else if (eepromSizeBytes <= 32768) name = "24C256";
    else                                name = "24C512";
  } else {
    if      (eepromSizeBytes <=  128) name = "24C01";
    else if (eepromSizeBytes <=  256) name = "24C02";
    else if (eepromSizeBytes <=  512) name = "24C04";
    else if (eepromSizeBytes <= 1024) name = "24C08";
    else                               name = "24C16";
  }
  Serial.printf("Identificada como: %s (~%lu Kbit)\n",
                name, (unsigned long)(eepromSizeBytes * 8 / 1024));
}

// ---------- Comandos ----------
void cmdScan() {
  Serial.println("\n[I2C] Escaneando...");
  bool any = false;
  bool picked = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      any = true;
      Serial.printf("  - Dispositivo en 0x%02X", addr);
      if (addr >= 0x50 && addr <= 0x57) {
        Serial.print("  (EEPROM candidata)");
        if (!picked) { eepromAddr = addr; picked = true; }
      }
      Serial.println();
    }
    delay(2);
  }
  if (!any) Serial.println("  (Nada encontrado)");
  if (picked) Serial.printf("Usando EEPROM en 0x%02X\n", eepromAddr);
  else Serial.println("No se detectó EEPROM (0x50–0x57).");
}

void cmdWPCheck(uint16_t memAddr) {
  uint8_t orig = 0xFF, test;
  if (!eepromReadSeq(memAddr, &orig, 1)) { Serial.println("Lectura inicial falló."); return; }

  test = (orig == 0xFF) ? 0x00 : 0xFF;
  if (!eepromWritePaged(memAddr, &test, 1, eepromPageSize)) {
    Serial.println("Escritura NACK → posible WP ACTIVA o bus error.");
    return;
  }
  uint8_t after = 0xFF;
  if (!eepromReadSeq(memAddr, &after, 1)) { Serial.println("Lectura posterior falló."); return; }

  if (after != test) {
    Serial.printf("WP ACTIVA: Byte no cambió (0x%02X -> 0x%02X leído 0x%02X)\n", orig, test, after);
    return;
  }

  if (!eepromWritePaged(memAddr, &orig, 1, eepromPageSize)) {
    Serial.println("¡Atención! No se pudo restaurar el valor original.");
  } else {
    eepromReadSeq(memAddr, &after, 1);
  }
  Serial.printf("WP DESACTIVADA: se pudo escribir y restaurar (final=0x%02X)\n", after);
}

void cmdRead(uint16_t addr, int len) {
  if (len <= 0 || len > 256) { Serial.println("len max 256"); return; }
  uint8_t buf[256];
  if (eepromReadSeq(addr, buf, len)) {
    Serial.println("HEX:"); dumpHex(buf, len);
    Serial.print("ASCII: ");
    for (int i = 0; i < len; i++) Serial.print((buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.');
    Serial.println();
  } else Serial.println("Error de lectura");
}

void cmdWrite(uint16_t addr, const String& text) {
  if (eepromWritePaged(addr, (const uint8_t*)text.c_str(), text.length(), eepromPageSize))
    Serial.printf("Escrito en 0x%04X\n", addr);
  else
    Serial.println("Error de escritura (WP alto, dirección o bus).");
}

void cmdErase() {
  Serial.println("Borrando toda la EEPROM (0xFF)...");
  uint32_t total = eepromSizeBytes;
  uint8_t buf[128];
  memset(buf, 0xFF, sizeof(buf));
  for (uint32_t a = 0; a < total; a += eepromPageSize) {
    size_t chunk = (size_t)min((uint32_t)eepromPageSize, total - a);
    if (!eepromWritePaged((uint16_t)a, buf, chunk, eepromPageSize)) {
      Serial.printf("Fallo en 0x%04lX\n", (unsigned long)a);
      break;
    }
  }
  Serial.println("Borrado completo.");
}

void cmdAddrSet(uint8_t addr) {
  eepromAddr = addr;
  Serial.printf("Dirección EEPROM seleccionada: 0x%02X\n", eepromAddr);
}

void cmdInfo() {
  Serial.printf("EEPROM @0x%02X | modo %s-byte | tamaño %lu bytes (~%lu Kbit) | página %u B\n",
    eepromAddr,
    (eepromAddrWidth == 2) ? "16(2)" : "8(1)",
    (unsigned long)eepromSizeBytes,
    (unsigned long)(eepromSizeBytes * 8 / 1024),
    (unsigned)eepromPageSize
  );
  printHumanCapacity();
}

// ---------- Parser ----------
String input;
void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line == "scan") { cmdScan(); return; }

  if (line == "capacity") { eepromDetectSize(true); printHumanCapacity(); return; }
  if (line == "info") { cmdInfo(); return; }

  if (line.startsWith("pagesize ")) {
    String s = line.substring(9);
    int v = s.toInt();
    if (v <= 0) { Serial.println("Uso: pagesize <N>"); return; }
    eepromPageSize = (size_t)v;
    Serial.printf("Page size forzado a %d bytes\n", v);
    return;
  }

  if (line.startsWith("addr ")) {
    String s = line.substring(5);
    uint8_t v = s.startsWith("0x") ? strtoul(s.c_str(), nullptr, 16) : (uint8_t)s.toInt();
    cmdAddrSet(v); return;
  }

  if (line.startsWith("read ")) {
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) { Serial.println("Uso: read <addr> <len>"); return; }
    String a = line.substring(sp1 + 1, sp2), b = line.substring(sp2 + 1);
    uint16_t addr = a.startsWith("0x") ? strtoul(a.c_str(), nullptr, 16) : (uint16_t)a.toInt();
    int len = b.toInt();
    cmdRead(addr, len); return;
  }

  if (line.startsWith("write ")) {
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) { Serial.println("Uso: write <addr> <texto>"); return; }
    String a = line.substring(sp1 + 1, sp2), text = line.substring(sp2 + 1);
    uint16_t addr = a.startsWith("0x") ? strtoul(a.c_str(), nullptr, 16) : (uint16_t)a.toInt();
    cmdWrite(addr, text); return;
  }

  if (line == "erase") { cmdErase(); return; }

  if (line.startsWith("wpcheck")) {
    uint16_t m = 0x0000;
    int sp = line.indexOf(' ');
    if (sp > 0) {
      String a = line.substring(sp + 1);
      if (a.length()) m = a.startsWith("0x") ? strtoul(a.c_str(), nullptr, 16) : (uint16_t)a.toInt();
    }
    cmdWPCheck(m); return;
  }

  Serial.println("Comandos:");
  Serial.println("  scan");
  Serial.println("  addr <0x50..0x57>");
  Serial.println("  capacity             (auto-detecta tamaño y página)");
  Serial.println("  info                 (muestra configuración actual)");
  Serial.println("  pagesize <N>         (forzar tamaño de página)");
  Serial.println("  read <addr> <len>    (addr admite 0x..)");
  Serial.println("  write <addr> <texto>");
  Serial.println("  erase");
  Serial.println("  wpcheck [addr]       (prueba WP; por defecto 0x0000)");
}

void setup() {
  Serial.begin(115200);
  delay(200);
#if defined(ESP8266) || defined(ESP32)
  Wire.begin(SDA_PIN, SCL_PIN);
#else
  Wire.begin();
#endif
  Wire.setClock(100000);
  Serial.println("Monitor I2C-EEPROM listo. Escribe 'scan' y luego 'capacity'.");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (input.length()) { handleCommand(input); input = ""; } }
    else input += c;
  }
}
