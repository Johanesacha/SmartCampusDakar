# SmartCampus Dakar

Système IoT intelligent pour l'optimisation de l'environnement éducatif dans les campus universitaires à Dakar.

## 📋 Description

Ce projet vise à concevoir une plateforme intelligente intégrant des capteurs IoT et des algorithmes d'IA pour améliorer l'expérience éducative, la gestion des infrastructures et la sécurité des campus.

## 🎯 Fonctionnalités

### ✅ Implémentées
- 🌡️ Surveillance température et humidité (DHT11)
- 🔆 Détection luminosité ambiante (LDR)
- 🚶 Détection de présence (PIR)
- 🌪️ Contrôle intelligent ventilation
- 💡 Gestion automatique éclairage
- 💾 Stockage local des données (LittleFS)
- 📊 Dashboard web temps réel
- ☁️ Intégration cloud (ThingSpeak)
- 🤖 Détection d'anomalies (IA simple)

## 🛠️ Technologies

- **Hardware** : ESP32, DHT11, PIR HC-SR501, LDR, Relais 2 canaux
- **Software** : Arduino C++, LittleFS, WiFi, WebServer
- **Cloud** : ThingSpeak
- **Frontend** : HTML5, CSS3, JavaScript, Chart.js

## 📸 Captures d'écran

### Dashboard Local
![Dashboard](images/dashboard_local.jpg)

### ThingSpeak Cloud
![ThingSpeak](images/thingspeak_graphs.png)

### Prototype
![Prototype](images/prototype_photo.jpg)

## 🚀 Installation

### Prérequis
- Arduino IDE 2.x
- ESP32 Board Package
- Bibliothèques : DHT sensor library, LittleFS

### Configuration
1. Clonez ce repository
2. Ouvrez le code dans Arduino IDE
3. Modifiez les lignes 27-36 avec vos identifiants
4. Uploadez sur l'ESP32

## 📊 Résultats

- ✅ **Performances** : Temps de réponse < 2s
- ✅ **Fiabilité** : Uptime 24/7
- ✅ **Précision** : DHT11 ±2°C, ±5% humidité
- ✅ **Scalabilité** : Support jusqu'à 4 connexions simultanées

## 👨‍💻 Auteurs

**Johannes ACHA & Gloria DOSSA**- Projet SmartCampus Dakar


## 🙏 Remerciements

- ESMT pour le contexte éducatif
- Communauté Arduino & ESP32
- ThingSpeak pour l'infrastructure cloud gratuite!!!!!!!!!!