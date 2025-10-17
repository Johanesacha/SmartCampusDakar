#include "DHT.h"

// === DHT11 ===
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// === PIR ===
#define PIR_PIN 13

// === LDR ===
#define LDR_PIN 36 // ADC

// === Relais ===
#define RELAY1 26  // VENTILATEUR
#define RELAY2 27  // LAMPE

// === LEDs bicolores KY-011 ===
#define LED1_R 16  // PIR Status (Rouge = mouvement)
#define LED1_G 17  // PIR Status (Vert = pas mouvement) - HS pour l'instant
#define LED2_R 18  // Système Status (Rouge = mode nuit)
#define LED2_G 19  // Système Status (Vert = mode jour)

// === Buzzer ===
#define BUZZER 25

// === SEUILS INTELLIGENTS ===
#define TEMP_SEUIL_VENTILO 32    // °C - Ventilo ON si temp > 28°C
#define HUMID_SEUIL_VENTILO 97.0   // % - Ventilo ON si humidité > 75%
#define LDR_SEUIL_LAMPE 1000       // Lampe ON si LDR < 1000 (obscurité)

// === Variables de timing ===
unsigned long lastSensorCheck = 0;
unsigned long lastStatusPrint = 0;
unsigned long pirDetectionStart = 0;
bool ventiloState = false;
bool lampeState = false;
bool pirDetected = false;
bool pirValidating = false;

// === Types de buzzer ===
enum BuzzerType {
  BUZZER_STARTUP = 0,
  BUZZER_CHALEUR = 1,
  BUZZER_HUMIDITE = 2,
  BUZZER_MOUVEMENT = 3,
  BUZZER_NUIT = 4
};

void buzzerAlert(BuzzerType type) {
  switch(type) {
    case BUZZER_STARTUP:
      for(int i = 0; i < 3; i++) {
        digitalWrite(BUZZER, HIGH); delay(100);
        digitalWrite(BUZZER, LOW); delay(100);
      }
      break;
      
    case BUZZER_CHALEUR:
      for(int i = 0; i < 2; i++) {
        digitalWrite(BUZZER, HIGH); delay(150);
        digitalWrite(BUZZER, LOW); delay(50);
      }
      break;
      
    case BUZZER_HUMIDITE:
      for(int i = 0; i < 3; i++) {
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🏫 === SmartCampus Dakar - PHASE 2 OPTIMISÉE === 🏫");
  Serial.println("🔧 Ventilateur: RELAY1 (GPIO26) | 💡 Lampe: RELAY2 (GPIO27)");
  Serial.println("🌡️ Ventilo ON si T>28°C OU Hum>75% | 💡 Lampe ON si obscurité<1000");
  
  // DHT11 init
  dht.begin();
  Serial.println("✅ DHT11 initialisé");
  
  // PIR
  pinMode(PIR_PIN, INPUT);
  Serial.println("✅ PIR initialisé (avec validation anti-parasites)");
  
  // Relais (IMPORTANT: Active LOW)
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY1, HIGH); // VENTILO OFF (Active LOW → HIGH = OFF)
  digitalWrite(RELAY2, HIGH); // LAMPE OFF
  Serial.println("✅ Relais initialisés - VENTILO: OFF, LAMPE: OFF");
  
  // LEDs
  pinMode(LED1_R, OUTPUT);
  pinMode(LED1_G, OUTPUT);  // HS mais on garde le code
  pinMode(LED2_R, OUTPUT);
  pinMode(LED2_G, OUTPUT);
  
  // État initial LEDs
  digitalWrite(LED1_R, LOW);   // PIR: rouge OFF
  digitalWrite(LED1_G, HIGH);  // PIR: vert ON (même si HS)
  digitalWrite(LED2_R, LOW);   // Système: rouge OFF
  digitalWrite(LED2_G, HIGH);  // Système: vert ON (mode jour par défaut)
  Serial.println("✅ LEDs initialisées (LED1_G défectueuse notée)");
  
  // Buzzer
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);
  Serial.println("✅ Buzzer initialisé");
  
  // Bip de démarrage
  buzzerAlert(BUZZER_STARTUP);
  
  Serial.println("🚀 PHASE 2 OPTIMISÉE PRÊTE ! Système intelligent activé !");
  Serial.println("==========================================\n");
}

void loop() {
  unsigned long currentTime = millis();
  
  // === LECTURE CAPTEURS (toutes les 2 secondes) ===
  if (currentTime - lastSensorCheck >= 2000) {
    
    // === DHT11 - Température & Humidité ===
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    if (isnan(h) || isnan(t)) {
      Serial.println("❌ ERREUR: DHT11 non lu");
    } else {
      Serial.print("🌡️ Temp: "); Serial.print(t, 1); Serial.print("°C");
      Serial.print(" | 💧 Hum: "); Serial.print(h, 1); Serial.println("%");
      
      // 🔥 LOGIQUE VENTILATEUR AMÉLIORÉE (Température OU Humidité)
      bool shouldVentiloBeOn = (t > TEMP_SEUIL_VENTILO) || (h > HUMID_SEUIL_VENTILO);
      
      if (shouldVentiloBeOn && !ventiloState) {
        // Conditions critiques → VENTILO ON
        digitalWrite(RELAY1, LOW);  // Active LOW → LOW = ON
        ventiloState = true;
        
        if (t > TEMP_SEUIL_VENTILO) {
          Serial.println("🌪️ VENTILATEUR ACTIVÉ ! (Température > 28°C)");
          buzzerAlert(BUZZER_CHALEUR);
        }
        if (h > HUMID_SEUIL_VENTILO) {
          Serial.println("🌪️ VENTILATEUR ACTIVÉ ! (Humidité > 75%)");
          buzzerAlert(BUZZER_HUMIDITE);
        }
        
      } else if (!shouldVentiloBeOn && ventiloState) {
        // Conditions normales (avec hystérésis) → VENTILO OFF
        bool tempOK = (t <= (TEMP_SEUIL_VENTILO - 2.0));
        bool humidOK = (h <= (HUMID_SEUIL_VENTILO - 5.0));
        
        if (tempOK && humidOK) {
          digitalWrite(RELAY1, HIGH); // Active LOW → HIGH = OFF
          ventiloState = false;
          Serial.println("❄️ VENTILATEUR ÉTEINT (Conditions normales)");
        }
      }
    }
    
    // === LDR - Gestion lumière ambiante ===
    int ldrValue = analogRead(LDR_PIN);
    Serial.print("🔆 Lumière ADC: "); Serial.println(ldrValue);
    
    if (ldrValue < LDR_SEUIL_LAMPE && !lampeState) {
      // Obscurité détectée → LAMPE ON
      digitalWrite(RELAY2, LOW);   // Active LOW → LOW = ON
      lampeState = true;
      digitalWrite(LED2_R, HIGH);  // LED système rouge (mode nuit)
      digitalWrite(LED2_G, LOW);
      Serial.println("🌙 LAMPE ACTIVÉE ! (Mode NUIT - Obscurité détectée)");
      buzzerAlert(BUZZER_NUIT);
      
    } else if (ldrValue >= (LDR_SEUIL_LAMPE + 200) && lampeState) {
      // Luminosité suffisante (hystérésis de 200) → LAMPE OFF
      digitalWrite(RELAY2, HIGH);  // Active LOW → HIGH = OFF
      lampeState = false;
      digitalWrite(LED2_R, LOW);   // LED système verte (mode jour)
      digitalWrite(LED2_G, HIGH);
      Serial.println("☀️ LAMPE ÉTEINTE (Mode JOUR - Luminosité suffisante)");
    }
    
    lastSensorCheck = currentTime;
  }
  
  // === PIR - Détection mouvement ANTI-PARASITES ===
  int motion = digitalRead(PIR_PIN);
  
  if (motion == HIGH && !pirDetected && !pirValidating) {
    // Début de validation du mouvement
    pirValidating = true;
    pirDetectionStart = currentTime;
    
  } else if (pirValidating && (currentTime - pirDetectionStart >= 200)) {
    // Validation après 200ms
    if (digitalRead(PIR_PIN) == HIGH) {
      // Mouvement confirmé !
      pirDetected = true;
      pirValidating = false;
      Serial.println("🚶 MOUVEMENT CONFIRMÉ !");
      digitalWrite(LED1_R, HIGH);  // LED PIR rouge ON
      digitalWrite(LED1_G, LOW);   // LED PIR verte OFF (même si HS)
      buzzerAlert(BUZZER_MOUVEMENT);
    } else {
      // Fausse alerte, on annule
      pirValidating = false;
    }
    
  } else if (motion == LOW && pirDetected && (currentTime - pirDetectionStart > 2000)) {
    // Plus de mouvement depuis 2 secondes (augmenté pour plus de stabilité)
    pirDetected = false;
    Serial.println("✅ Pas de mouvement");
    digitalWrite(LED1_R, LOW);   // LED PIR rouge OFF
    digitalWrite(LED1_G, HIGH);  // LED PIR verte ON
  }
  
  // === AFFICHAGE STATUT SYSTÈME (toutes les 10 secondes) ===
  if (currentTime - lastStatusPrint >= 10000) {
    Serial.println("\n📊 === STATUT SYSTÈME OPTIMISÉ ===");
    Serial.print("🌪️ Ventilateur: "); Serial.println(ventiloState ? "🟢 ON" : "🔴 OFF");
    Serial.print("💡 Lampe: "); Serial.println(lampeState ? "🟢 ON" : "🔴 OFF");
    Serial.print("🚶 PIR: "); Serial.println(pirDetected ? "🟢 MOUVEMENT" : "⚪ CALME");
    
    // Lecture DHT pour statut
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      Serial.print("🌡️ Conditions: "); Serial.print(t, 1); Serial.print("°C, ");
      Serial.print(h, 1); Serial.println("% Hum");
    }
    Serial.print("🔆 LDR: "); Serial.println(analogRead(LDR_PIN));
    Serial.println("===============================\n");
    
    lastStatusPrint = currentTime;
  }
  
  delay(50); // Rythme optimisé pour PIR anti-parasites
}