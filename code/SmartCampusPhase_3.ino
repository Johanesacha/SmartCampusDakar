#include "DHT.h"
#include "FS.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>

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
const int LINES_BEFORE_FLUSH = 10;
int bufferedLines = 0;

// === WiFi AP CONFIG ===
const char* AP_SSID = "SmartCampus-Dakar";
const char* AP_PASSWORD = "smartcampus2025";
WebServer server(80);

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
  Serial.println(" lignes écrites");
  
  csvBuffer = "";
  bufferedLines = 0;
}

// === WEB HANDLERS ===
void handleRoot() {
  // Flush buffer avant d'afficher les infos
  if(bufferedLines > 0) flushBufferToFS();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>SmartCampus Dakar</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: rgba(255,255,255,0.1); backdrop-filter: blur(10px); border-radius: 20px; padding: 30px; box-shadow: 0 8px 32px rgba(0,0,0,0.3); }";
  html += "h1 { text-align: center; margin-bottom: 30px; font-size: 2em; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); }";
  html += ".card { background: rgba(255,255,255,0.2); border-radius: 15px; padding: 20px; margin: 15px 0; }";
  html += ".btn { display: block; width: 100%; padding: 15px; margin: 10px 0; border: none; border-radius: 10px; font-size: 16px; font-weight: bold; cursor: pointer; transition: all 0.3s; text-decoration: none; text-align: center; color: white; }";
  html += ".btn-primary { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); }";
  html += ".btn-success { background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%); }";
  html += ".btn-danger { background: linear-gradient(135deg, #eb3349 0%, #f45c43 100%); }";
  html += ".btn-info { background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%); }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(0,0,0,0.3); }";
  html += ".stat { display: flex; justify-content: space-between; margin: 10px 0; }";
  html += ".stat-label { font-weight: bold; }";
  html += ".stat-value { background: rgba(255,255,255,0.3); padding: 5px 15px; border-radius: 20px; }";
  html += ".emoji { font-size: 1.5em; margin-right: 10px; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>🏫 SmartCampus Dakar</h1>";
  
  // État du système
  html += "<div class='card'>";
  html += "<h2><span class='emoji'>📊</span>État Temps Réel</h2>";
  
  // Lecture DHT
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int ldr = analogRead(LDR_PIN);
  
  if(!isnan(t)) {
    html += "<div class='stat'><span class='stat-label'>🌡️ Température</span><span class='stat-value'>" + String(t, 1) + " °C</span></div>";
  }
  if(!isnan(h)) {
    html += "<div class='stat'><span class='stat-label'>💧 Humidité</span><span class='stat-value'>" + String(h, 1) + " %</span></div>";
  }
  html += "<div class='stat'><span class='stat-label'>🔆 Lumière</span><span class='stat-value'>" + String(ldr) + "</span></div>";
  html += "<div class='stat'><span class='stat-label'>🚶 Mouvement</span><span class='stat-value'>" + String(pirDetected ? "Détecté" : "Calme") + "</span></div>";
  html += "<div class='stat'><span class='stat-label'>🌪️ Ventilateur</span><span class='stat-value'>" + String(ventiloState ? "🟢 ON" : "🔴 OFF") + "</span></div>";
  html += "<div class='stat'><span class='stat-label'>💡 Lampe</span><span class='stat-value'>" + String(lampeState ? "🟢 ON" : "🔴 OFF") + "</span></div>";
  html += "</div>";
  
  // Infos fichier
  html += "<div class='card'>";
  html += "<h2><span class='emoji'>💾</span>Fichier de Données</h2>";
  
  if(LittleFS.exists(DATA_PATH)) {
    File f = LittleFS.open(DATA_PATH, FILE_READ);
    if(f) {
      size_t fileSize = f.size();
      int lineCount = 0;
      while(f.available()) {
        if(f.read() == '\n') lineCount++;
      }
      f.close();
      
      html += "<div class='stat'><span class='stat-label'>📁 Taille</span><span class='stat-value'>" + String(fileSize) + " bytes</span></div>";
      html += "<div class='stat'><span class='stat-label'>📝 Lignes</span><span class='stat-value'>" + String(lineCount) + "</span></div>";
      html += "<div class='stat'><span class='stat-label'>⏳ Buffer</span><span class='stat-value'>" + String(bufferedLines) + " lignes</span></div>";
    }
  } else {
    html += "<p style='text-align:center;'>❌ Aucun fichier trouvé</p>";
  }
  html += "</div>";
  
  // Actions
  html += "<div class='card'>";
  html += "<h2><span class='emoji'>⚡</span>Actions Rapides</h2>";
  html += "<a href='/download' class='btn btn-success' download>📥 Télécharger CSV</a>";
  html += "<a href='/view' class='btn btn-info'>👁️ Voir les Données</a>";
  html += "<a href='/erase' class='btn btn-danger' onclick='return confirm(\"Effacer toutes les données ?\")'>🗑️ Effacer les Données</a>";
  html += "<a href='/' class='btn btn-primary'>🔄 Rafraîchir</a>";
  html += "</div>";
  
  html += "<p style='text-align:center; margin-top:20px; opacity:0.7;'>SmartCampus Dakar © 2025</p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleDownload() {
  // Flush buffer avant téléchargement
  if(bufferedLines > 0) flushBufferToFS();
  
  if(!LittleFS.exists(DATA_PATH)) {
    server.send(404, "text/plain", "Fichier data.csv introuvable");
    return;
  }
  
  File f = LittleFS.open(DATA_PATH, FILE_READ);
  if(!f) {
    server.send(500, "text/plain", "Erreur ouverture fichier");
    return;
  }
  
  server.streamFile(f, "text/csv");
  f.close();
  Serial.println("📥 Fichier CSV téléchargé via web");
}

void handleView() {
  // Flush buffer
  if(bufferedLines > 0) flushBufferToFS();
  
  if(!LittleFS.exists(DATA_PATH)) {
    server.send(404, "text/plain", "Fichier data.csv introuvable");
    return;
  }
  
  File f = LittleFS.open(DATA_PATH, FILE_READ);
  if(!f) {
    server.send(500, "text/plain", "Erreur ouverture fichier");
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Données CSV - SmartCampus</title>";
  html += "<style>";
  html += "body { font-family: 'Courier New', monospace; margin: 20px; background: #1e1e1e; color: #fff; }";
  html += "pre { background: #2d2d2d; padding: 20px; border-radius: 10px; overflow-x: auto; white-space: pre-wrap; word-wrap: break-word; }";
  html += ".header { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); padding: 20px; border-radius: 10px; margin-bottom: 20px; text-align: center; }";
  html += "a { color: #667eea; text-decoration: none; }";
  html += "</style></head><body>";
  html += "<div class='header'><h1>📊 Données CSV</h1><p><a href='/'>← Retour au Dashboard</a></p></div>";
  html += "<pre>";
  
  while(f.available()) {
    html += (char)f.read();
  }
  f.close();
  
  html += "</pre></body></html>";
  server.send(200, "text/html", html);
}

void handleErase() {
  // Flush buffer
  if(bufferedLines > 0) flushBufferToFS();
  
  if(LittleFS.exists(DATA_PATH)) {
    LittleFS.remove(DATA_PATH);
  }
  
  File f = LittleFS.open(DATA_PATH, FILE_WRITE);
  if(f) {
    f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");
    f.close();
  }
  
  Serial.println("🗑️ Fichier CSV réinitialisé via web");
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body{font-family:Arial;text-align:center;padding:50px;background:#1e1e1e;color:#fff;}</style>";
  html += "</head><body><h1>✅ Données effacées</h1><p>Redirection...</p></body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🏫 === SmartCampus Dakar - PHASE 3B : WiFi AP === 🏫");
  
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
  } else {
    Serial.println("✅ LittleFS monté avec succès");
    
    if(!LittleFS.exists(DATA_PATH)) {
      File f = LittleFS.open(DATA_PATH, FILE_WRITE);
      if(f) {
        f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");
        f.close();
        Serial.println("✅ Fichier data.csv créé");
      }
    } else {
      Serial.println("✅ Fichier data.csv existant trouvé");
    }
  }
  
  // === Init WiFi AP ===
  Serial.println("\n📶 Démarrage WiFi Access Point...");
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  
  Serial.println("✅ WiFi AP démarré !");
  Serial.print("📱 SSID: ");
  Serial.println(AP_SSID);
  Serial.print("🔐 Mot de passe: ");
  Serial.println(AP_PASSWORD);
  Serial.print("🌐 Adresse IP: ");
  Serial.println(ip);
  Serial.println("👉 Ouvre http://" + ip.toString() + " dans ton navigateur");
  
  // === Init Web Server ===
  server.on("/", handleRoot);
  server.on("/download", handleDownload);
  server.on("/view", handleView);
  server.on("/erase", handleErase);
  server.begin();
  
  Serial.println("✅ Serveur web démarré");
  Serial.println("\n📝 Commandes Serial toujours disponibles: dump | erase | info");
  Serial.println("\n🚀 Système prêt!\n");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Gestion des requêtes web
  server.handleClient();
  
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
      Serial.print("🌡️ "); Serial.print(t, 1); Serial.print("°C | 💧 "); Serial.print(h, 1); Serial.println("%");
    }
    
    // Logique ventilateur
    bool shouldVentilo = (t > TEMP_SEUIL_VENTILO) || (h > HUMID_SEUIL_VENTILO);
    if(shouldVentilo && !ventiloState) {
      digitalWrite(RELAY1, LOW);
      ventiloState = true;
      if(t > TEMP_SEUIL_VENTILO) buzzerAlert(BUZZER_CHALEUR);
      if(h > HUMID_SEUIL_VENTILO) buzzerAlert(BUZZER_HUMIDITE);
    } else if(!shouldVentilo && ventiloState) {
      bool tempOK = (t <= (TEMP_SEUIL_VENTILO - 2.0));
      bool humidOK = (h <= (HUMID_SEUIL_VENTILO - 5.0));
      if(tempOK && humidOK) {
        digitalWrite(RELAY1, HIGH);
        ventiloState = false;
      }
    }
    
    // LDR
    int ldrValue = analogRead(LDR_PIN);
    
    if(ldrValue < LDR_SEUIL_LAMPE && !lampeState) {
      digitalWrite(RELAY2, LOW);
      lampeState = true;
      digitalWrite(LED2_R, HIGH);
      digitalWrite(LED2_G, LOW);
      buzzerAlert(BUZZER_NUIT);
    } else if(ldrValue >= (LDR_SEUIL_LAMPE + 200) && lampeState) {
      digitalWrite(RELAY2, HIGH);
      lampeState = false;
      digitalWrite(LED2_R, LOW);
      digitalWrite(LED2_G, HIGH);
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
    
    if(bufferedLines >= LINES_BEFORE_FLUSH) {
      flushBufferToFS();
    }
    
    lastSensorCheck = currentTime;
  }
  
  // === STATUT SYSTÈME ===
  if(currentTime - lastStatusPrint >= 10000) {
    Serial.println("\n📊 Ventilo:" + String(ventiloState?"ON":"OFF") + " | Lampe:" + String(lampeState?"ON":"OFF") + " | PIR:" + String(pirDetected?"MOUV":"CALM") + " | Buffer:" + String(bufferedLines));
    lastStatusPrint = currentTime;
  }
  
  // === COMMANDES SERIAL ===
  if(Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    if(cmd == "dump") {
      if(bufferedLines > 0) flushBufferToFS();
      
      Serial.println("\n=== DUMP CSV ===");
      if(LittleFS.exists(DATA_PATH)) {
        File f = LittleFS.open(DATA_PATH, FILE_READ);
        if(f) {
          while(f.available()) Serial.write(f.read());
          f.close();
          Serial.println("\n✅ Dump terminé");
        }
      }
      
    } else if(cmd == "erase") {
      if(bufferedLines > 0) flushBufferToFS();
      if(LittleFS.exists(DATA_PATH)) LittleFS.remove(DATA_PATH);
      File f = LittleFS.open(DATA_PATH, FILE_WRITE);
      if(f) {
        f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");
        f.close();
        Serial.println("✅ CSV réinitialisé");
      }
      
    } else if(cmd == "info") {
      if(bufferedLines > 0) flushBufferToFS();
      Serial.println("\n📊 === INFOS ===");
      if(LittleFS.exists(DATA_PATH)) {
        File f = LittleFS.open(DATA_PATH, FILE_READ);
        if(f) {
          Serial.print("Taille: "); Serial.print(f.size()); Serial.println(" bytes");
          int lines = 0;
          while(f.available()) if(f.read() == '\n') lines++;
          Serial.print("Lignes: "); Serial.println(lines);
          f.close();
        }
      }
      Serial.print("Buffer: "); Serial.println(bufferedLines);
    }
  }
  
  delay(50);
}