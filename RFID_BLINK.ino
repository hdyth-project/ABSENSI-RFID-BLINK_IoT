
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Absensi RFID"
#define BLYNK_AUTH_TOKEN    "TOKEN_DEVICE_KAMU"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// =================================================
// 1. WIFI
// =================================================

char ssid[] = "NAMA_WIFI";
char pass[] = "PASSWORD_WIFI";

// =================================================
// 2. PIN RFID RC522
// =================================================

#define SS_PIN   5
#define RST_PIN  2

#define RFID_SCK  18
#define RFID_MISO 19
#define RFID_MOSI 23

MFRC522 rfid(SS_PIN, RST_PIN);

// =================================================
// 3. PIN SERVO DAN LED
// =================================================

#define SERVO_PIN 13
#define LED_HIJAU 26
#define LED_MERAH 25

Servo servoPintu;

const int POSISI_TUTUP = 0;
const int POSISI_BUKA  = 90;

const unsigned long DURASI_BUKA = 3000;

bool pintuTerbuka = false;
unsigned long waktuBuka = 0;

// =================================================
// 4. KEYPAD 4x4
// =================================================

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {27, 14, 16, 17};
byte colPins[COLS] = {32, 33, 12, 4};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// Ganti PIN sesuai keinginan
const String PIN_BENAR = "1234";

String inputPIN = "";

// =================================================
// 5. DHT22
// =================================================

#define DHT_PIN 15
#define DHT_TYPE DHT22

DHT dht(DHT_PIN, DHT_TYPE);

// =================================================
// 6. LCD I2C
// =================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

// =================================================
// 7. NTP CLIENT GMT+7
// =================================================

WiFiUDP ntpUDP;

NTPClient timeClient(
  ntpUDP,
  "pool.ntp.org",
  25200,
  60000
);

// =================================================
// 8. DATABASE RFID SISWA
// =================================================

struct DataSiswa {
  String uid;
  String nama;
};

// UID ditulis huruf kecil dan dua digit per byte
DataSiswa daftarSiswa[] = {
  {"3671c269", "Aisyah"},
  {"f6ddbc69", "Dayat"},

  // Tambahkan siswa lainnya di sini
};

const int JUMLAH_SISWA =
  sizeof(daftarSiswa) / sizeof(daftarSiswa[0]);

// =================================================
// 9. TIMER SISTEM
// =================================================

unsigned long waktuSuhu = 0;
unsigned long waktuCobaBlynk = 0;

const unsigned long INTERVAL_SUHU = 2000;
const unsigned long INTERVAL_BLYNK = 5000;

// =================================================
// 10. CARI SISWA BERDASARKAN UID
// =================================================

int cariSiswa(String uid) {

  for (int i = 0; i < JUMLAH_SISWA; i++) {

    if (uid == daftarSiswa[i].uid) {
      return i;
    }
  }

  return -1;
}

// =================================================
// 11. BUKA PINTU
// =================================================

void bukaPintu() {

  servoPintu.write(POSISI_BUKA);

  digitalWrite(LED_HIJAU, HIGH);
  digitalWrite(LED_MERAH, LOW);

  pintuTerbuka = true;
  waktuBuka = millis();

  Serial.println("AKSES DITERIMA");
  Serial.println("PINTU TERBUKA");
}

// =================================================
// 12. TUTUP PINTU
// =================================================

void tutupPintu() {

  servoPintu.write(POSISI_TUTUP);

  digitalWrite(LED_HIJAU, LOW);

  pintuTerbuka = false;

  Serial.println("PINTU TERKUNCI");
}

// =================================================
// 13. AKSES DITOLAK
// =================================================

void aksesDitolak() {

  Serial.println("AKSES DITOLAK!");

  digitalWrite(LED_HIJAU, LOW);
  digitalWrite(LED_MERAH, HIGH);

  delay(600);

  digitalWrite(LED_MERAH, LOW);
}

// =================================================
// 14. KIRIM NOTIFIKASI BLYNK
// =================================================

void kirimNotifikasi(String nama, String uid) {

  if (!Blynk.connected()) {

    Serial.println("Blynk tidak terhubung.");
    Serial.println("Notifikasi tidak terkirim.");

    return;
  }

  if (!timeClient.isTimeSet()) {

    Serial.println("Waktu belum tersinkronisasi.");
    return;
  }

  String tanggal;
  String waktu;

  waktu = timeClient.getFormattedTime();

  time_t epoch = timeClient.getEpochTime();
  struct tm *infoWaktu = gmtime(&epoch);

  char bufferTanggal[15];

  strftime(
    bufferTanggal,
    sizeof(bufferTanggal),
    "%d-%m-%Y",
    infoWaktu
  );

  tanggal = String(bufferTanggal);

  String pesan =
    "Nama: " + nama +
    " | UID: " + uid +
    " | Tanggal: " + tanggal +
    " | Waktu: " + waktu + " WIB";

  Serial.println("Mengirim event Blynk...");
  Serial.println(pesan);

  Blynk.logEvent("absensi", pesan);

  Serial.println("Event absensi dikirim ke Blynk.");
}

// =================================================
// 15. PROSES RFID
// =================================================

void prosesRFID() {

  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toLowerCase();

  Serial.println();
  Serial.println("========================");
  Serial.println("KARTU RFID TERDETEKSI");
  Serial.println("UID: " + uid);

  int indexSiswa = cariSiswa(uid);

  if (indexSiswa >= 0) {

    String nama = daftarSiswa[indexSiswa].nama;

    Serial.println("Nama: " + nama);

    // Buka pintu
    bukaPintu();

    // Kirim notifikasi Blynk
    kirimNotifikasi(nama, uid);

  } else {

    Serial.println("KARTU TIDAK TERDAFTAR");

    aksesDitolak();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  Serial.println("========================");
}

// =================================================
// 16. PROSES KEYPAD
// =================================================

void prosesKeypad() {

  char key = keypad.getKey();

  if (!key) {
    return;
  }

  Serial.print("KEYPAD: ");
  Serial.println(key);

  // Input angka
  if (key >= '0' && key <= '9') {

    if (inputPIN.length() < 8) {
      inputPIN += key;
    }

    Serial.println("PIN: " + inputPIN);
  }

  // Hapus input
  else if (key == '*') {

    inputPIN = "";

    Serial.println("PIN DIHAPUS");
  }

  // Konfirmasi PIN
  else if (key == '#') {

    if (inputPIN == PIN_BENAR) {

      Serial.println("PIN BENAR");

      // Buka pintu tanpa notifikasi Blynk
      bukaPintu();

    } else {

      Serial.println("PIN SALAH");

      aksesDitolak();
    }

    inputPIN = "";
  }
}

// =================================================
// 17. TAMPILAN SUHU LCD
// =================================================

void tampilkanSuhu() {

  if (millis() - waktuSuhu < INTERVAL_SUHU) {
    return;
  }

  waktuSuhu = millis();

  float suhu = dht.readTemperature();

  lcd.setCursor(0, 0);
  lcd.print("SUHU RUANGAN    ");

  lcd.setCursor(0, 1);

  if (isnan(suhu)) {

    lcd.print("Sensor Error    ");

    Serial.println("DHT22 gagal dibaca.");

  } else {

    lcd.print(suhu, 1);
    lcd.print((char)223);
    lcd.print("C           ");

    Serial.print("Suhu: ");
    Serial.print(suhu);
    Serial.println(" C");
  }
}

// =================================================
// 18. SETUP
// =================================================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("==============================");
  Serial.println("   ABSENSI RFID BLYNK IOT");
  Serial.println("==============================");

  // LED
  pinMode(LED_HIJAU, OUTPUT);
  pinMode(LED_MERAH, OUTPUT);

  digitalWrite(LED_HIJAU, LOW);
  digitalWrite(LED_MERAH, LOW);

  // Servo
  servoPintu.setPeriodHertz(50);
  servoPintu.attach(SERVO_PIN, 500, 2400);
  servoPintu.write(POSISI_TUTUP);

  // LCD
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("ABSENSI RFID");

  lcd.setCursor(0, 1);
  lcd.print("Menghubungkan");

  // DHT22
  dht.begin();

  // RFID
  SPI.begin(
    RFID_SCK,
    RFID_MISO,
    RFID_MOSI,
    SS_PIN
  );

  rfid.PCD_Init();

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.print("Menghubungkan WiFi");

  unsigned long mulaiWiFi = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - mulaiWiFi < 20000
  ) {
    delay(300);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println();
    Serial.println("WiFi Terhubung!");

    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());

    // NTP
    timeClient.begin();

    if (timeClient.forceUpdate()) {
      Serial.println("Waktu berhasil disinkronkan!");
    } else {
      Serial.println("Sinkronisasi waktu gagal.");
    }

    // Blynk
    Blynk.config(BLYNK_AUTH_TOKEN);

    if (Blynk.connect(10000)) {
      Serial.println("Blynk berhasil terhubung!");
    } else {
      Serial.println("Blynk belum terhubung.");
    }

  } else {

    Serial.println();
    Serial.println("WiFi belum terhubung!");
  }

  lcd.clear();

  Serial.println("SISTEM SIAP DIGUNAKAN");
}

// =================================================
// 19. LOOP UTAMA
// =================================================

void loop() {

  // Jalankan komunikasi Blynk
  if (WiFi.status() == WL_CONNECTED) {

    Blynk.run();

    // Coba sambungkan kembali jika terputus
    if (
      !Blynk.connected() &&
      millis() - waktuCobaBlynk >= INTERVAL_BLYNK
    ) {

      waktuCobaBlynk = millis();

      Blynk.connect(100);
    }

    // Update waktu NTP
    timeClient.update();

  } else {

    // Sambungkan kembali WiFi
    WiFi.reconnect();
  }

  // Tampilkan suhu
  tampilkanSuhu();

  // Proses keypad
  prosesKeypad();

  // Proses RFID hanya ketika pintu terkunci
  if (!pintuTerbuka) {
    prosesRFID();
  }

  // Tutup pintu otomatis
  if (
    pintuTerbuka &&
    millis() - waktuBuka >= DURASI_BUKA
  ) {

    tutupPintu();
  }
}
