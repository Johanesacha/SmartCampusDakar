#include "DHT.h"
#include "FS.h"
#include <LittleFS.h>

// === DHT11 ===
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// === CAPTEURS / ACTIONNEURS ===
#define PIR_PIN 13
#define LDR_PIN 36
#define RELAY1 26  // VENTILATEUR
#define RELAY2 27  // LAMPE
#define LED1_R 16
#define LED1_G 17
#define LED2_R 18
#define LED2_G 19
#define BUZZER 25

// === SEUILS ===
#define TEMP_SEUIL_VENTILO 28.0
#define HUMID_SEUIL_VENTILO 75.0
#define LDR_SEUIL_LAMPE 1000

// === LOGGING CONFIG ===
#define FORMAT_LITTLEFS_IF_FAILED true
const char* DATA_PATH = "/data.csv";
String csvBuffer = "";
const int LINES_BEFORE_FLUSH = 10;  // Écriture par lots pour protéger la flash
int bufferedLines = 0;

// === Variables ===
unsigned long lastSensorCheck = 0;
unsigned long lastStatusPrint = 0;
unsigned long pirDetectionStart = 0;
bool ventiloState = false;
bool lampeState = false;
bool pirDetected = false;
bool pirValidating = false;

// === Buzzer ===
enum BuzzerType { BUZZER_STARTUP=0, BUZZER_CHALEUR=1, BUZZER_HUMIDITE=2, BUZZER_MOUVEMENT=3, BUZZER_NUIT=4 };

void buzzerAlert(BuzzerType type) {
  switch(type) {
    case BUZZER_STARTUP:
      for(int i=0; i<3; i++) {
        digitalWrite(BUZZER, HIGH); delay(100);
        digitalWrite(BUZZER, LOW); delay(100);
      }
      break;
    case BUZZER_CHALEUR:
      for(int i=0; i<2; i++) {
        digitalWrite(BUZZER, HIGH); delay(150);
        digitalWrite(BUZZER, LOW); delay(50);
      }
      break;
    case BUZZER_HUMIDITE:
      for(int i=0; i<3; i++) {
        digitalWrite(BUZZER, HIGH); delay(100);
        digitalWrite(BUZZER, LOW); delay(100);
      }
      break;
    case BUZZER_MOUVEMENT:
      digitalWrite(BUZZER, HIGH); delay(200);
      digitalWrite(BUZZER, LOW);
      break;
    case BUZZER_NUIT:
      digitalWrite(BUZZER, HIGH); delay(300);
      digitalWrite(BUZZER, LOW);
      break;
  }
}

void flushBufferToFS() {
  if(csvBuffer.length() == 0) return;
  
  File f = LittleFS.open(DATA_PATH, FILE_APPEND);
  if(!f) {
    Serial.println("❌ Erreur ouverture fichier en append");
    return;
  }
  
  f.print(csvBuffer);
  f.close();
  
  Serial.print("💾 Flush: ");
  Serial.print(bufferedLines);
  Serial.println(" lignes écrites sur flash");
  
  csvBuffer = "";
  bufferedLines = 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🏫 === SmartCampus Dakar - PHASE 3 : DATA LOGGING === 🏫");
  
  // === Init capteurs/actionneurs ===
  dht.begin();
  pinMode(PIR_PIN, INPUT);
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  pinMode(LED1_R, OUTPUT);
  pinMode(LED1_G, OUTPUT);
  pinMode(LED2_R, OUTPUT);
  pinMode(LED2_G, OUTPUT);
  digitalWrite(LED1_R, LOW);
  digitalWrite(LED1_G, HIGH);
  digitalWrite(LED2_R, LOW);
  digitalWrite(LED2_G, HIGH);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);
  
  buzzerAlert(BUZZER_STARTUP);
  
  // === Init LittleFS ===
  Serial.println("\n📁 Initialisation LittleFS...");
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("❌ LittleFS mount FAILED!");
    Serial.println("⚠️ La flash sera formatée au prochain redémarrage");
  } else {
    Serial.println("✅ LittleFS monté avec succès");
    
    // Créer le fichier avec header si inexistant
    if(!LittleFS.exists(DATA_PATH)) {
      File f = LittleFS.open(DATA_PATH, FILE_WRITE);
      if(f) {
        f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");
        f.close();
        Serial.println("✅ Fichier data.csv créé avec header");
      } else {
        Serial.println("❌ Impossible de créer data.csv");
      }
    } else {
      Serial.println("✅ Fichier data.csv existant trouvé");
    }
  }
  
  Serial.println("\n📝 COMMANDES DISPONIBLES:");
  Serial.println("  • dump  → Affiche tout le contenu du CSV");
  Serial.println("  • erase → Efface les données (garde le header)");
  Serial.println("  • info  → Infos sur le fichier");
  Serial.println("\n🚀 Système prêt! Logging activé (buffer: 10 lignes)\n");
}

void loop() {
  unsigned long currentTime = millis();
  
  // === LECTURE CAPTEURS (toutes les 2 secondes) ===
  if(currentTime - lastSensorCheck >= 2000) {
    
    // DHT11
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    if(isnan(h) || isnan(t)) {
      Serial.println("❌ DHT11 lecture échouée");
      t = -999;
      h = -999;
    } else {
      Serial.print("🌡️ Temp: "); Serial.print(t, 1); Serial.print("°C");
      Serial.print(" | 💧 Hum: "); Serial.print(h, 1); Serial.println("%");
    }
    
    // Logique ventilateur
    bool shouldVentilo = (t > TEMP_SEUIL_VENTILO) || (h > HUMID_SEUIL_VENTILO);
    if(shouldVentilo && !ventiloState) {
      digitalWrite(RELAY1, LOW);
      ventiloState = true;
      if(t > TEMP_SEUIL_VENTILO) {
        Serial.println("🌪️ VENTILATEUR ON (Chaleur)");
        buzzerAlert(BUZZER_CHALEUR);
      }
      if(h > HUMID_SEUIL_VENTILO) {
        Serial.println("🌪️ VENTILATEUR ON (Humidité)");
        buzzerAlert(BUZZER_HUMIDITE);
      }
    } else if(!shouldVentilo && ventiloState) {
      bool tempOK = (t <= (TEMP_SEUIL_VENTILO - 2.0));
      bool humidOK = (h <= (HUMID_SEUIL_VENTILO - 5.0));
      if(tempOK && humidOK) {
        digitalWrite(RELAY1, HIGH);
        ventiloState = false;
        Serial.println("❄️ VENTILATEUR OFF");
      }
    }
    
    // LDR
    int ldrValue = analogRead(LDR_PIN);
    Serial.print("🔆 LDR: "); Serial.println(ldrValue);
    
    if(ldrValue < LDR_SEUIL_LAMPE && !lampeState) {
      digitalWrite(RELAY2, LOW);
      lampeState = true;
      digitalWrite(LED2_R, HIGH);
      digitalWrite(LED2_G, LOW);
      Serial.println("🌙 LAMPE ON (Obscurité)");
      buzzerAlert(BUZZER_NUIT);
    } else if(ldrValue >= (LDR_SEUIL_LAMPE + 200) && lampeState) {
      digitalWrite(RELAY2, HIGH);
      lampeState = false;
      digitalWrite(LED2_R, LOW);
      digitalWrite(LED2_G, HIGH);
      Serial.println("☀️ LAMPE OFF (Lumineux)");
    }
    
    // PIR
    int motion = digitalRead(PIR_PIN);
    if(motion == HIGH && !pirDetected && !pirValidating) {
      pirValidating = true;
      pirDetectionStart = currentTime;
    } else if(pirValidating && (currentTime - pirDetectionStart >= 200)) {
      if(digitalRead(PIR_PIN) == HIGH) {
        pirDetected = true;
        pirValidating = false;
        Serial.println("🚶 MOUVEMENT CONFIRMÉ");
        digitalWrite(LED1_R, HIGH);
        digitalWrite(LED1_G, LOW);
        buzzerAlert(BUZZER_MOUVEMENT);
      } else {
        pirValidating = false;
      }
    } else if(motion == LOW && pirDetected && (currentTime - pirDetectionStart > 2000)) {
      pirDetected = false;
      digitalWrite(LED1_R, LOW);
      digitalWrite(LED1_G, HIGH);
      Serial.println("✅ Pas de mouvement");
    }
    
    // === LOGGING CSV ===
    String csvLine = String(currentTime) + "," + 
                     String(t, 1) + "," + 
                     String(h, 1) + "," + 
                     String(ldrValue) + "," + 
                     String(pirDetected ? 1 : 0) + "," + 
                     String(ventiloState ? 1 : 0) + "," + 
                     String(lampeState ? 1 : 0);
    
    csvBuffer += csvLine + "\n";
    bufferedLines++;
    Serial.print("📊 CSV: "); Serial.println(csvLine);
    
    // Flush si buffer plein
    if(bufferedLines >= LINES_BEFORE_FLUSH) {
      flushBufferToFS();
    }
    
    lastSensorCheck = currentTime;
  }
  
  // === STATUT SYSTÈME ===
  if(currentTime - lastStatusPrint >= 10000) {
    Serial.println("\n📊 === STATUT SYSTÈME ===");
    Serial.print("🌪️ Ventilo: "); Serial.println(ventiloState ? "🟢 ON" : "🔴 OFF");
    Serial.print("💡 Lampe: "); Serial.println(lampeState ? "🟢 ON" : "🔴 OFF");
    Serial.print("🚶 PIR: "); Serial.println(pirDetected ? "🟢 MOUVEMENT" : "⚪ CALME");
    Serial.print("💾 Buffer: "); Serial.print(bufferedLines); Serial.println(" lignes");
    Serial.println("=========================\n");
    lastStatusPrint = currentTime;
  }
  
  // === COMMANDES SERIAL ===
  if(Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    if(cmd == "dump") {
      // Flush buffer d'abord
      if(bufferedLines > 0) {
        Serial.println("⚠️ Flush du buffer avant dump...");
        flushBufferToFS();
      }
      
      Serial.println("\n╔════════════════════════════════════════╗");
      Serial.println("║       DUMP COMPLET DE DATA.CSV         ║");
      Serial.println("╚════════════════════════════════════════╝\n");
      
      if(LittleFS.exists(DATA_PATH)) {
        File f = LittleFS.open(DATA_PATH, FILE_READ);
        if(f) {
          while(f.available()) {
            Serial.write(f.read());
          }
          f.close();
          Serial.println("\n\n✅ Dump terminé");
        } else {
          Serial.println("❌ Impossible d'ouvrir le fichier");
        }
      } else {
        Serial.println("❌ Fichier data.csv inexistant");
      }
      
    } else if(cmd == "erase") {
      // Flush buffer d'abord
      if(bufferedLines > 0) flushBufferToFS();
      
      Serial.println("⚠️ Effacement de data.csv...");
      if(LittleFS.exists(DATA_PATH)) {
        LittleFS.remove(DATA_PATH);
      }
      
      File f = LittleFS.open(DATA_PATH, FILE_WRITE);
      if(f) {
        f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");
        f.close();
        Serial.println("✅ data.csv réinitialisé (header restauré)");
      } else {
        Serial.println("❌ Erreur lors de la réinitialisation");
      }
      
    } else if(cmd == "info") {
      if(bufferedLines > 0) flushBufferToFS();
      
      Serial.println("\n📊 === INFORMATIONS FICHIER ===");
      if(LittleFS.exists(DATA_PATH)) {
        File f = LittleFS.open(DATA_PATH, FILE_READ);
        if(f) {
          size_t fileSize = f.size();
          int lineCount = 0;
          while(f.available()) {
            if(f.read() == '\n') lineCount++;
          }
          f.close();
          
          Serial.print("📁 Taille: "); Serial.print(fileSize); Serial.println(" bytes");
          Serial.print("📝 Lignes: "); Serial.println(lineCount);
          Serial.print("💾 Buffer: "); Serial.print(bufferedLines); Serial.println(" lignes en attente");
        }
      } else {
        Serial.println("❌ Fichier inexistant");
      }
      Serial.println("==============================\n");
      
    } else {
      Serial.println("❌ Commande inconnue!");
      Serial.println("Commandes: dump | erase | info");
    }
  }
  
  delay(50);
}