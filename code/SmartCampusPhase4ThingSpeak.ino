#include "DHT.h"
#include "FS.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define PIR_PIN 13
#define LDR_PIN 36
#define RELAY1 26
#define RELAY2 27
#define LED1_R 16
#define LED1_G 17
#define LED2_R 18
#define LED2_G 19
#define BUZZER 25

#define TEMP_SEUIL_VENTILO 28.0
#define HUMID_SEUIL_VENTILO 75.0
#define LDR_SEUIL_LAMPE 1000

// === THINGSPEAK CONFIG ===
const char* THINGSPEAK_API_KEY = "P3KX7MW1ZH0HUQV7";
const char* THINGSPEAK_SERVER = "http://api.thingspeak.com/update";
unsigned long lastThingSpeakUpdate = 0;
const unsigned long THINGSPEAK_INTERVAL = 20000; // 20 secondes (limite ThingSpeak: 15s)
bool thingspeakEnabled = true;

// === WIFI STATION (pour Internet) ===
// ⚠️ REMPLACE PAR TES IDENTIFIANTS WIFI ⚠️
const char* WIFI_SSID = "Latracabilite";
const char* WIFI_PASSWORD = "***********";
bool wifiStationConnected = false;

#define FORMAT_LITTLEFS_IF_FAILED true
const char* DATA_PATH = "/data.csv";
String csvBuffer = "";
const int LINES_BEFORE_FLUSH = 10;
int bufferedLines = 0;

String cachedJSON = "";
unsigned long lastCacheUpdate = 0;
const unsigned long CACHE_DURATION = 10000;

bool tempAnomaly = false;
bool humidAnomaly = false;
bool ldrAnomaly = false;

const char* AP_SSID = "SmartCampus-Dakar";
const char* AP_PASSWORD = "smartcampus2025";
WebServer server(80);

unsigned long lastSensorCheck = 0;
unsigned long lastStatusPrint = 0;
unsigned long pirDetectionStart = 0;
bool ventiloState = false;
bool lampeState = false;
bool pirDetected = false;
bool pirValidating = false;

float tempMin = 999, tempMax = -999;
float humidMin = 999, humidMax = -999;
int ldrMin = 9999, ldrMax = -1;
unsigned long uptimeStart = 0;

int thingspeakSuccessCount = 0;
int thingspeakFailCount = 0;

enum BuzzerType { BUZZER_STARTUP=0, BUZZER_CHALEUR=1, BUZZER_HUMIDITE=2, BUZZER_MOUVEMENT=3, BUZZER_NUIT=4 };

void buzzerAlert(BuzzerType type) {
  switch(type) {
    case BUZZER_STARTUP: for(int i=0;i<3;i++){digitalWrite(BUZZER,HIGH);delay(100);digitalWrite(BUZZER,LOW);delay(100);} break;
    case BUZZER_CHALEUR: for(int i=0;i<2;i++){digitalWrite(BUZZER,HIGH);delay(150);digitalWrite(BUZZER,LOW);delay(50);} break;
    case BUZZER_HUMIDITE: for(int i=0;i<3;i++){digitalWrite(BUZZER,HIGH);delay(100);digitalWrite(BUZZER,LOW);delay(100);} break;
    case BUZZER_MOUVEMENT: digitalWrite(BUZZER,HIGH);delay(200);digitalWrite(BUZZER,LOW); break;
    case BUZZER_NUIT: digitalWrite(BUZZER,HIGH);delay(300);digitalWrite(BUZZER,LOW); break;
  }
}

void connectWiFiStation() {
  if(String(WIFI_SSID) == "TON_WIFI_SSID") {
    Serial.println("⚠️ WiFi Station non configuré (voir lignes 27-26)");
    wifiStationConnected = false;
    return;
  }
  
  Serial.print("📶 Connexion WiFi Station: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while(WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Station connecté!");
    Serial.print("🌐 IP: ");
    Serial.println(WiFi.localIP());
    wifiStationConnected = true;
  } else {
    Serial.println("\n❌ Échec connexion WiFi Station");
    Serial.println("💡 Le système fonctionne en mode local uniquement");
    wifiStationConnected = false;
  }
}

void sendToThingSpeak(float temp, float humid, int ldr, int motion, int ventilo, int lampe) {
  if(!thingspeakEnabled || !wifiStationConnected) return;
  
  if(String(THINGSPEAK_API_KEY) == "TON_API_KEY_ICI") {
    Serial.println("⚠️ ThingSpeak API Key non configurée");
    return;
  }
  
  HTTPClient http;
  
  String url = String(THINGSPEAK_SERVER);
  url += "?api_key=" + String(THINGSPEAK_API_KEY);
  url += "&field1=" + String(temp, 1);
  url += "&field2=" + String(humid, 1);
  url += "&field3=" + String(ldr);
  url += "&field4=" + String(motion);
  url += "&field5=" + String(ventilo);
  url += "&field6=" + String(lampe);
  
  http.begin(url);
  int httpCode = http.GET();
  
  if(httpCode > 0) {
    String response = http.getString();
    if(response.toInt() > 0) {
      thingspeakSuccessCount++;
      Serial.print("☁️ ThingSpeak OK (");
      Serial.print(thingspeakSuccessCount);
      Serial.println(" envois)");
    } else {
      thingspeakFailCount++;
      Serial.print("⚠️ ThingSpeak erreur: ");
      Serial.println(response);
    }
  } else {
    thingspeakFailCount++;
    Serial.print("❌ ThingSpeak HTTP error: ");
    Serial.println(httpCode);
  }
  
  http.end();
}

void flushBufferToFS() {
  if(csvBuffer.length()==0) return;
  File f = LittleFS.open(DATA_PATH, FILE_APPEND);
  if(!f){Serial.println("❌ Erreur");return;}
  f.print(csvBuffer);
  f.close();
  Serial.print("💾 ");Serial.print(bufferedLines);Serial.println(" lignes");
  csvBuffer="";
  bufferedLines=0;
  cachedJSON="";
}

void handleAPI() {
  if(cachedJSON.length()>0 && (millis()-lastCacheUpdate<CACHE_DURATION)){
    server.send(200,"application/json",cachedJSON);
    return;
  }
  
  if(bufferedLines>0) flushBufferToFS();
  
  String json="{\"data\":[";
  
  if(LittleFS.exists(DATA_PATH)){
    File f=LittleFS.open(DATA_PATH,FILE_READ);
    if(f){
      int totalLines=0;
      while(f.available()){if(f.read()=='\n')totalLines++;}
      
      f.seek(0);
      f.readStringUntil('\n');
      
      int startLine=max(0,totalLines-51);
      int currentLine=0;
      
      while(currentLine<startLine && f.available()){
        f.readStringUntil('\n');
        currentLine++;
      }
      
      int count=0;
      while(f.available() && count<50){
        String line=f.readStringUntil('\n');
        line.trim();
        
        if(line.length()>5){
          if(count>0)json+=",";
          
          int idx1=line.indexOf(',');
          int idx2=line.indexOf(',',idx1+1);
          int idx3=line.indexOf(',',idx2+1);
          
          String timestamp=line.substring(0,idx1);
          String temp=line.substring(idx1+1,idx2);
          String humid=line.substring(idx2+1,idx3);
          String ldr=line.substring(idx3+1,line.indexOf(',',idx3+1));
          
          json+="{\"t\":"+timestamp+",\"temp\":"+temp+",\"humid\":"+humid+",\"ldr\":"+ldr+"}";
          count++;
        }
      }
      f.close();
    }
  }
  
  json+="]}";
  cachedJSON=json;
  lastCacheUpdate=millis();
  server.send(200,"application/json",json);
}

void handleRoot() {
  if(bufferedLines>0) flushBufferToFS();
  
  float h=dht.readHumidity();
  float t=dht.readTemperature();
  int ldr=analogRead(LDR_PIN);
  
  if(!isnan(t)){if(t<tempMin)tempMin=t;if(t>tempMax)tempMax=t;}
  if(!isnan(h)){if(h<humidMin)humidMin=h;if(h>humidMax)humidMax=h;}
  if(ldr<ldrMin)ldrMin=ldr;
  if(ldr>ldrMax)ldrMax=ldr;
  
  tempAnomaly=(!isnan(t)&&(t>35||t<15));
  humidAnomaly=(!isnan(h)&&(h>85||h<20));
  ldrAnomaly=(ldr<100||ldr>4000);
  
  int totalLines=0;
  if(LittleFS.exists(DATA_PATH)){
    File f=LittleFS.open(DATA_PATH,FILE_READ);
    if(f){while(f.available()){if(f.read()=='\n')totalLines++;}f.close();}
  }
  
  String html="<!DOCTYPE html><html><head>";
  html+="<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html+="<title>SmartCampus Cloud</title>";
  html+="<script src='https://cdnjs.cloudflare.com/ajax/libs/Chart.js/3.9.1/chart.min.js'></script>";
  html+="<style>";
  html+="*{margin:0;padding:0;box-sizing:border-box;}";
  html+="body{font-family:'Segoe UI',Arial;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:#fff;padding:15px;}";
  html+=".container{max-width:1200px;margin:0 auto;}";
  html+="h1{text-align:center;font-size:2em;margin-bottom:5px;text-shadow:3px 3px 6px rgba(0,0,0,0.3);}";
  html+=".subtitle{text-align:center;opacity:0.9;margin-bottom:20px;font-size:1em;}";
  
  // Cloud status badge
  html+=".cloud-badge{text-align:center;margin-bottom:20px;}";
  html+=".badge{display:inline-block;padding:8px 20px;border-radius:20px;font-size:0.9em;font-weight:bold;margin:5px;}";
  html+=".badge-success{background:#10b981;}";
  html+=".badge-warning{background:#f59e0b;}";
  html+=".badge-info{background:#3b82f6;}";
  
  html+=".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:15px;margin-bottom:15px;}";
  html+=".card{background:rgba(255,255,255,0.15);backdrop-filter:blur(10px);border-radius:15px;padding:20px;box-shadow:0 8px 32px rgba(0,0,0,0.2);transition:transform 0.3s;}";
  html+=".card:hover{transform:translateY(-3px);}";
  html+=".card-title{font-size:0.8em;opacity:0.8;margin-bottom:8px;text-transform:uppercase;}";
  html+=".card-value{font-size:2em;font-weight:bold;margin:5px 0;}";
  html+=".card-unit{font-size:0.6em;opacity:0.7;}";
  html+=".status{display:inline-block;padding:4px 12px;border-radius:15px;font-size:0.75em;font-weight:bold;margin-top:8px;}";
  html+=".status-on{background:#10b981;}";
  html+=".status-off{background:#ef4444;}";
  html+=".status-alert{background:#f59e0b;animation:pulse 1.5s infinite;}";
  html+="@keyframes pulse{0%,100%{opacity:1;}50%{opacity:0.6;}}";
  html+=".chart-container{background:rgba(255,255,255,0.15);backdrop-filter:blur(10px);border-radius:15px;padding:20px;margin:15px 0;box-shadow:0 8px 32px rgba(0,0,0,0.2);}";
  html+=".chart-title{font-size:1.1em;margin-bottom:15px;text-align:center;}";
  html+=".stats-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;margin:15px 0;}";
  html+=".stat-box{background:rgba(255,255,255,0.1);padding:12px;border-radius:12px;text-align:center;}";
  html+=".stat-label{font-size:0.75em;opacity:0.8;margin-bottom:5px;}";
  html+=".stat-value{font-size:1.3em;font-weight:bold;}";
  html+=".btn{display:inline-block;padding:10px 25px;margin:8px 3px;border:none;border-radius:20px;font-weight:bold;cursor:pointer;text-decoration:none;color:#fff;transition:all 0.3s;}";
  html+=".btn-success{background:linear-gradient(135deg,#10b981 0%,#059669 100%);}";
  html+=".btn-danger{background:linear-gradient(135deg,#ef4444 0%,#dc2626 100%);}";
  html+=".btn-primary{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);}";
  html+=".btn-cloud{background:linear-gradient(135deg,#3b82f6 0%,#2563eb 100%);}";
  html+=".btn:hover{transform:scale(1.05);box-shadow:0 5px 20px rgba(0,0,0,0.3);}";
  html+=".actions{text-align:center;margin:20px 0;}";
  html+=".info{text-align:center;opacity:0.7;margin:15px 0;font-size:0.9em;}";
  html+=".loading{text-align:center;padding:20px;font-size:1.2em;}";
  html+="</style></head><body>";
  
  html+="<div class='container'>";
  html+="<h1>🏫 SmartCampus Dakar</h1>";
  html+="<div class='subtitle'>Système Intelligent Cloud + Local</div>";
  
  // Cloud status
  html+="<div class='cloud-badge'>";
  if(wifiStationConnected) {
    html+="<span class='badge badge-success'>☁️ Cloud ACTIF</span>";
    html+="<span class='badge badge-info'>📡 "+String(thingspeakSuccessCount)+" envois</span>";
    if(thingspeakFailCount > 0) {
      html+="<span class='badge badge-warning'>⚠️ "+String(thingspeakFailCount)+" échecs</span>";
    }
  } else {
    html+="<span class='badge badge-warning'>📍 Mode LOCAL uniquement</span>";
  }
  html+="</div>";
  
  html+="<div class='grid'>";
  
  html+="<div class='card'>";
  html+="<div class='card-title'>🌡️ TEMPÉRATURE</div>";
  html+="<div class='card-value'>"+String(isnan(t)?0:t,1)+"<span class='card-unit'>°C</span></div>";
  if(tempAnomaly)html+="<div class='status status-alert'>⚠️ ANOMALIE</div>";
  else html+="<div class='status "+String(t>TEMP_SEUIL_VENTILO?"status-alert":"status-on")+"'>"+String(t>TEMP_SEUIL_VENTILO?"🔥 Chaud":"✅ Normal")+"</div>";
  html+="</div>";
  
  html+="<div class='card'>";
  html+="<div class='card-title'>💧 HUMIDITÉ</div>";
  html+="<div class='card-value'>"+String(isnan(h)?0:h,1)+"<span class='card-unit'>%</span></div>";
  if(humidAnomaly)html+="<div class='status status-alert'>⚠️ ANOMALIE</div>";
  else html+="<div class='status "+String(h>HUMID_SEUIL_VENTILO?"status-alert":"status-on")+"'>"+String(h>HUMID_SEUIL_VENTILO?"💦 Humide":"✅ Normal")+"</div>";
  html+="</div>";
  
  html+="<div class='card'>";
  html+="<div class='card-title'>🔆 LUMIÈRE</div>";
  html+="<div class='card-value'>"+String(ldr)+"</div>";
  if(ldrAnomaly)html+="<div class='status status-alert'>⚠️ ANOMALIE</div>";
  else html+="<div class='status "+String(ldr<LDR_SEUIL_LAMPE?"status-alert":"status-on")+"'>"+String(ldr<LDR_SEUIL_LAMPE?"🌙 Sombre":"☀️ Clair")+"</div>";
  html+="</div>";
  
  html+="<div class='card'>";
  html+="<div class='card-title'>🚶 DÉTECTION</div>";
  html+="<div class='card-value'>"+String(pirDetected?"OUI":"NON")+"</div>";
  html+="<div class='status "+String(pirDetected?"status-alert":"status-on")+"'>"+String(pirDetected?"👤 Présence":"⚪ Vide")+"</div>";
  html+="</div>";
  
  html+="</div>";
  
  html+="<div class='grid'>";
  html+="<div class='card'><div class='card-title'>🌪️ VENTILATEUR</div><div class='card-value'>"+String(ventiloState?"ON":"OFF")+"</div>";
  html+="<div class='status "+String(ventiloState?"status-on":"status-off")+"'>"+String(ventiloState?"🟢 Actif":"🔴 Inactif")+"</div></div>";
  
  html+="<div class='card'><div class='card-title'>💡 ÉCLAIRAGE</div><div class='card-value'>"+String(lampeState?"ON":"OFF")+"</div>";
  html+="<div class='status "+String(lampeState?"status-on":"status-off")+"'>"+String(lampeState?"🟢 Allumé":"🔴 Éteint")+"</div></div>";
  html+="</div>";
  
  html+="<div class='chart-container'>";
  html+="<div class='chart-title'>📊 Statistiques Session</div>";
  html+="<div class='stats-grid'>";
  html+="<div class='stat-box'><div class='stat-label'>Temp Min</div><div class='stat-value'>"+String(tempMin==999?0:tempMin,1)+"°</div></div>";
  html+="<div class='stat-box'><div class='stat-label'>Temp Max</div><div class='stat-value'>"+String(tempMax==-999?0:tempMax,1)+"°</div></div>";
  html+="<div class='stat-box'><div class='stat-label'>Hum Min</div><div class='stat-value'>"+String(humidMin==999?0:humidMin,1)+"%</div></div>";
  html+="<div class='stat-box'><div class='stat-label'>Hum Max</div><div class='stat-value'>"+String(humidMax==-999?0:humidMax,1)+"%</div></div>";
  html+="</div></div>";
  
  html+="<div class='chart-container'>";
  html+="<div class='chart-title'>📈 Historique Local (50 dernières mesures)</div>";
  html+="<div id='loading' class='loading'>⏳ Chargement...</div>";
  html+="<div id='charts' style='display:none;'>";
  html+="<canvas id='chartTemp' height='60'></canvas><br>";
  html+="<canvas id='chartHumid' height='60'></canvas><br>";
  html+="<canvas id='chartLDR' height='60'></canvas>";
  html+="</div></div>";
  
  html+="<div class='actions'>";
  html+="<a href='/download' class='btn btn-success'>📥 CSV Local</a>";
  if(wifiStationConnected) {
    html+="<a href='https://thingspeak.com/channels/YOUR_CHANNEL_ID' target='_blank' class='btn btn-cloud'>☁️ Voir ThingSpeak</a>";
  }
  html+="<a href='/erase' class='btn btn-danger' onclick='return confirm(\"Effacer?\")'>🗑️ Effacer</a>";
  html+="<a href='/' class='btn btn-primary'>🔄 Rafraîchir</a>";
  html+="</div>";
  
  unsigned long uptime=millis()/1000;
  html+="<div class='info'>⏱️ Uptime: "+String(uptime/3600)+"h "+String((uptime%3600)/60)+"m | ";
  html+="📝 "+String(totalLines)+" lignes | ";
  html+="💾 Buffer: "+String(bufferedLines);
  if(wifiStationConnected) {
    html+=" | ☁️ ThingSpeak: "+String(thingspeakSuccessCount)+"/"+String(thingspeakSuccessCount+thingspeakFailCount);
  }
  html+="</div>";
  
  html+="</div>";
  
  html+="<script>";
  html+="fetch('/api').then(r=>r.json()).then(d=>{";
  html+="document.getElementById('loading').style.display='none';";
  html+="document.getElementById('charts').style.display='block';";
  html+="let labels=d.data.map((x,i)=>i);";
  html+="let temps=d.data.map(x=>x.temp);";
  html+="let humids=d.data.map(x=>x.humid);";
  html+="let ldrs=d.data.map(x=>x.ldr);";
  
  html+="new Chart(document.getElementById('chartTemp'),{type:'line',data:{labels:labels,datasets:[{label:'Temp',data:temps,borderColor:'rgb(239,68,68)',backgroundColor:'rgba(239,68,68,0.1)',tension:0.4,fill:true}]},options:{responsive:true,plugins:{legend:{labels:{color:'#fff'}}},scales:{x:{ticks:{color:'#fff'}},y:{ticks:{color:'#fff'}}}}});";
  
  html+="new Chart(document.getElementById('chartHumid'),{type:'line',data:{labels:labels,datasets:[{label:'Humid',data:humids,borderColor:'rgb(59,130,246)',backgroundColor:'rgba(59,130,246,0.1)',tension:0.4,fill:true}]},options:{responsive:true,plugins:{legend:{labels:{color:'#fff'}}},scales:{x:{ticks:{color:'#fff'}},y:{ticks:{color:'#fff'}}}}});";
  
  html+="new Chart(document.getElementById('chartLDR'),{type:'line',data:{labels:labels,datasets:[{label:'Light',data:ldrs,borderColor:'rgb(245,158,11)',backgroundColor:'rgba(245,158,11,0.1)',tension:0.4,fill:true}]},options:{responsive:true,plugins:{legend:{labels:{color:'#fff'}}},scales:{x:{ticks:{color:'#fff'}},y:{ticks:{color:'#fff'}}}}});";
  
  html+="}).catch(e=>{document.getElementById('loading').innerHTML='❌ Erreur';});";
  html+="setTimeout(()=>location.reload(),60000);";
  html+="</script>";
  
  html+="</body></html>";
  
  server.send(200,"text/html",html);
}

void handleDownload() {
  if(bufferedLines>0)flushBufferToFS();
  if(!LittleFS.exists(DATA_PATH)){server.send(404,"text/plain","Not found");return;}
  File f=LittleFS.open(DATA_PATH,FILE_READ);
  if(!f){server.send(500,"text/plain","Error");return;}
  server.streamFile(f,"text/csv");
  f.close();
}

void handleErase() {
  if(bufferedLines>0)flushBufferToFS();
  if(LittleFS.exists(DATA_PATH))LittleFS.remove(DATA_PATH);
  File f=LittleFS.open(DATA_PATH,FILE_WRITE);
  if(f){f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");f.close();}
  cachedJSON="";
  String html="<!DOCTYPE html><html><head><meta http-equiv='refresh' content='2;url=/'/><style>body{text-align:center;padding:50px;background:#1e1e1e;color:#fff;font-family:Arial;}</style></head><body><h1>✅ Effacé</h1></body></html>";
  server.send(200,"text/html",html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🏫 SmartCampus Phase B - CLOUD + LOCAL");
  
  uptimeStart=millis();
  
  dht.begin();
  pinMode(PIR_PIN,INPUT);
  pinMode(RELAY1,OUTPUT);pinMode(RELAY2,OUTPUT);
  digitalWrite(RELAY1,HIGH);digitalWrite(RELAY2,HIGH);
  pinMode(LED1_R,OUTPUT);pinMode(LED1_G,OUTPUT);pinMode(LED2_R,OUTPUT);pinMode(LED2_G,OUTPUT);
  digitalWrite(LED1_R,LOW);digitalWrite(LED1_G,HIGH);digitalWrite(LED2_R,LOW);digitalWrite(LED2_G,HIGH);
  pinMode(BUZZER,OUTPUT);digitalWrite(BUZZER,LOW);
  buzzerAlert(BUZZER_STARTUP);
  
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
    Serial.println("❌ LittleFS failed");
  }else{
    Serial.println("✅ LittleFS OK");
    if(!LittleFS.exists(DATA_PATH)){
      File f=LittleFS.open(DATA_PATH,FILE_WRITE);
      if(f){f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");f.close();}
    }
  }
  
  // Démarrer WiFi AP (local)
  WiFi.softAP(AP_SSID,AP_PASSWORD);
  Serial.print("📶 WiFi AP: ");Serial.println(WiFi.softAPIP());
  
  // Tenter connexion WiFi Station (Internet)
  connectWiFiStation();
  
  server.on("/",handleRoot);
  server.on("/api",handleAPI);
  server.on("/download",handleDownload);
  server.on("/erase",handleErase);
  server.begin();
  Serial.println("🚀 Dashboard CLOUD activé!\n");
  
  if(wifiStationConnected) {
    Serial.println("☁️ ThingSpeak: ACTIF (envoi toutes les 20s)");
  } else {
    Serial.println("📍 Mode LOCAL uniquement (pas d'Internet)");
  }
}

void loop() {
  unsigned long currentTime=millis();
  server.handleClient();
  
  if(currentTime-lastSensorCheck>=2000){
    float h=dht.readHumidity();
    float t=dht.readTemperature();
    if(isnan(h)||isnan(t)){t=-999;h=-999;}
    
    bool shouldVentilo=(t>TEMP_SEUIL_VENTILO)||(h>HUMID_SEUIL_VENTILO);
    if(shouldVentilo&&!ventiloState){
      digitalWrite(RELAY1,LOW);ventiloState=true;
      if(t>TEMP_SEUIL_VENTILO)buzzerAlert(BUZZER_CHALEUR);
      if(h>HUMID_SEUIL_VENTILO)buzzerAlert(BUZZER_HUMIDITE);
    }else if(!shouldVentilo&&ventiloState){
      bool tempOK=(t<=(TEMP_SEUIL_VENTILO-2.0));
      bool humidOK=(h<=(HUMID_SEUIL_VENTILO-5.0));
      if(tempOK&&humidOK){digitalWrite(RELAY1,HIGH);ventiloState=false;}
    }
    
    int ldrValue=analogRead(LDR_PIN);
    if(ldrValue<LDR_SEUIL_LAMPE&&!lampeState){
      digitalWrite(RELAY2,LOW);lampeState=true;
      digitalWrite(LED2_R,HIGH);digitalWrite(LED2_G,LOW);
      buzzerAlert(BUZZER_NUIT);
    }else if(ldrValue>=(LDR_SEUIL_LAMPE+200)&&lampeState){
      digitalWrite(RELAY2,HIGH);lampeState=false;
      digitalWrite(LED2_R,LOW);digitalWrite(LED2_G,HIGH);
    }
    
    int motion=digitalRead(PIR_PIN);
    if(motion==HIGH&&!pirDetected&&!pirValidating){
      pirValidating=true;pirDetectionStart=currentTime;
    }else if(pirValidating&&(currentTime-pirDetectionStart>=200)){
      if(digitalRead(PIR_PIN)==HIGH){
        pirDetected=true;pirValidating=false;
        digitalWrite(LED1_R,HIGH);digitalWrite(LED1_G,LOW);
        buzzerAlert(BUZZER_MOUVEMENT);
      }else pirValidating=false;
    }else if(motion==LOW&&pirDetected&&(currentTime-pirDetectionStart>2000)){
      pirDetected=false;
      digitalWrite(LED1_R,LOW);digitalWrite(LED1_G,HIGH);
    }
    
    // Envoi ThingSpeak
    if(wifiStationConnected && thingspeakEnabled && (currentTime-lastThingSpeakUpdate>=THINGSPEAK_INTERVAL)){
      sendToThingSpeak(t, h, ldrValue, pirDetected?1:0, ventiloState?1:0, lampeState?1:0);
      lastThingSpeakUpdate = currentTime;
    }
    
    String csvLine=String(currentTime)+","+String(t,1)+","+String(h,1)+","+String(ldrValue)+","+String(pirDetected?1:0)+","+String(ventiloState?1:0)+","+String(lampeState?1:0);
    csvBuffer+=csvLine+"\n";
    bufferedLines++;
    
    if(bufferedLines>=LINES_BEFORE_FLUSH)flushBufferToFS();
    lastSensorCheck=currentTime;
  }
  
  if(Serial.available()){
    String cmd=Serial.readStringUntil('\n');cmd.trim();cmd.toLowerCase();
    if(cmd=="dump"){
      if(bufferedLines>0)flushBufferToFS();
      if(LittleFS.exists(DATA_PATH)){
        File f=LittleFS.open(DATA_PATH,FILE_READ);
        if(f){while(f.available())Serial.write(f.read());f.close();}
      }
    }else if(cmd=="erase"){
      if(bufferedLines>0)flushBufferToFS();
      if(LittleFS.exists(DATA_PATH))LittleFS.remove(DATA_PATH);
      File f=LittleFS.open(DATA_PATH,FILE_WRITE);
      if(f){f.println("timestamp_ms,temperature_c,humidity_pct,ldr_adc,motion,ventilo,lampe");f.close();}
      cachedJSON="";
      Serial.println("✅ Donnees Effacees");
    }else if(cmd=="cache"){
      Serial.print("Cache: ");Serial.print(cachedJSON.length());Serial.println(" bytes");
      Serial.print("Dernière MAJ: ");Serial.print((millis()-lastCacheUpdate)/1000);Serial.println("s");
    }else if(cmd=="cloud"){
      Serial.println("\n☁️ === STATUT CLOUD ===");
      Serial.print("WiFi Station: ");Serial.println(wifiStationConnected?"✅ Connecté":"❌ Déconnecté");
      Serial.print("ThingSpeak: ");Serial.println(thingspeakEnabled?"✅ Actif":"❌ Désactivé");
      Serial.print("Envois réussis: ");Serial.println(thingspeakSuccessCount);
      Serial.print("Envois échoués: ");Serial.println(thingspeakFailCount);
      Serial.println("========================\n");
    }
  }
  
  delay(50);
}