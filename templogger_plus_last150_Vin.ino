#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <time.h>

#define ONE_WIRE_BUS 4     // GPIO für DS18B20 DATA Pin (D4)
#define BOOT_BUTTON_PIN 0  // BOOT-Taste (GPIO 0)
#define BATT_ADC_PIN 35    // GPIO 35 für Spannungsteiler (Input Only, WiFi-sicher)
#define FILE_PATH "/messungen.csv"
#define TEMP_FILE_PATH "/messungen_korrigiert.csv"
#define MAX_SENSORS 8

// Kalibrierungsfaktor für den Spannungsteiler (R1=100k, R2=100k -> Idealwert = 2.0)
const float ADC_CALIB_FACTOR = 2.0;

const char* ap_ssid = "Temperaturlogger";
const char* ap_password = "12345678";

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);

// --- Datenstrukturen im RTC Memory ---
RTC_DATA_ATTR bool isRunning = false;
RTC_DATA_ATTR int intervalMin = 5;
RTC_DATA_ATTR long targetSamples = 0;
RTC_DATA_ATTR long sampleCount = 0;
RTC_DATA_ATTR time_t startUnixTime = 0;
RTC_DATA_ATTR int deviceCount = 0;
RTC_DATA_ATTR char sensorNames[MAX_SENSORS][20];

String formatDateTime(time_t t) {
  struct tm* timeinfo = localtime(&t);
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
           timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  return String(buffer);
}

// Hilfsfunktion: Liest die Akkuspannung mit Mittelwertbildung (10 Messungen)
float readBatteryVoltage() {
  long sumMilliVolts = 0;
  for (int i = 0; i < 10; i++) {
    sumMilliVolts += analogReadMilliVolts(BATT_ADC_PIN);
    delay(2);
  }
  float avgMilliVolts = sumMilliVolts / 10.0;
  return (avgMilliVolts * ADC_CALIB_FACTOR) / 1000.0;
}

void takeMeasurement() {
  sensors.requestTemperatures();
  float battVolts = readBatteryVoltage();

  deviceCount = sensors.getDeviceCount();
  if (deviceCount > MAX_SENSORS) deviceCount = MAX_SENSORS;

  if (deviceCount > 0) {
    sampleCount++;
    time_t currentSampleTime = startUnixTime + ((sampleCount - 1) * intervalMin * 60);

    File file = LittleFS.open(FILE_PATH, FILE_APPEND);
    if (file) {
      file.print(sampleCount);
      file.print(",");
      file.print(formatDateTime(currentSampleTime));
      file.print(",");
      file.print(battVolts, 2);  // Spannung mit 2 Nachkommastellen (z. B. 4.12)

      for (int i = 0; i < deviceCount; i++) {
        float tempC = sensors.getTempCByIndex(i);
        file.print(",");
        if (tempC != -127.0 && tempC != 85.0) {
          file.print(tempC, 2);
        } else {
          file.print("ERR");
        }
      }
      file.println();
      file.close();
    }
  }
}

void correctTimeStamps(time_t endUnixTime) {
  if (sampleCount <= 1 || !LittleFS.exists(FILE_PATH)) return;

  double realIntervalSec = (double)(endUnixTime - startUnixTime) / (sampleCount - 1);

  File inputFile = LittleFS.open(FILE_PATH, FILE_READ);
  File outputFile = LittleFS.open(TEMP_FILE_PATH, FILE_WRITE);

  if (inputFile && outputFile) {
    if (inputFile.available()) {
      String header = inputFile.readStringUntil('\n');
      outputFile.println(header);
    }

    long currentIdx = 0;
    while (inputFile.available()) {
      String line = inputFile.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;

      int firstComma = line.indexOf(',');
      int secondComma = line.indexOf(',', firstComma + 1);

      if (firstComma != -1 && secondComma != -1) {
        String nrStr = line.substring(0, firstComma);
        String restOfLine = line.substring(secondComma);  // Behält V_Akku und Temperaturen unverändert

        time_t correctedTime = startUnixTime + (time_t)(currentIdx * realIntervalSec);

        outputFile.print(nrStr);
        outputFile.print(",");
        outputFile.print(formatDateTime(correctedTime));
        outputFile.println(restOfLine);

        currentIdx++;
      }
    }
    inputFile.close();
    outputFile.close();

    LittleFS.remove(FILE_PATH);
    LittleFS.rename(TEMP_FILE_PATH, FILE_PATH);
  }
}

String buildHtmlPage() {
  // 1. ADC kurz "aufwecken" und dann die echte Spannung messen
  analogReadMilliVolts(BATT_ADC_PIN); 
  delay(10);
  float liveBatt = readBatteryVoltage();

  String page = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<title>Temperaturlogger</title>";
  page += "<style>";
  page += "body{font-family:Arial,sans-serif;margin:20px;background:#f4f4f9;color:#333;} ";
  page += "table{border-collapse:collapse;width:100%;max-width:850px;margin-top:15px;} ";
  page += "th,td{border:1px solid #ccc;padding:8px;text-align:left;} ";
  page += "th{background:#007bff;color:white;} ";
  page += ".card{background:white;padding:20px;border-radius:8px;max-width:580px;box-shadow:0 2px 5px rgba(0,0,0,0.1);margin-bottom:20px;} ";
  page += "label{display:block;margin-top:10px;font-weight:bold;} ";
  page += "select,input[type=text],input[type=datetime-local]{width:100%;padding:8px;margin-top:5px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;} ";
  page += ".btn{display:inline-block;padding:10px 15px;background:#28a745;color:white;text-decoration:none;border-radius:4px;margin-top:15px;border:none;cursor:pointer;} ";
  page += ".btn-danger{background:#dc3545;} ";
  page += ".btn-warning{background:#ffc107;color:#000;} ";
  page += ".btn-info{background:#17a2b8;} ";
  page += ".sensor-box{background:#e9ecef;padding:10px;margin-top:8px;border-radius:5px;} ";
  // 2. Deutlich sichtbares CSS für das Akku-Badge:
  page += ".batt-badge{display:inline-block;padding:10px 18px;color:white;border-radius:6px;font-weight:bold;font-size:1.1em;margin-bottom:15px;box-shadow:0 2px 4px rgba(0,0,0,0.15);} ";
  page += "</style>";

  page += "<script>";
  page += "function setNowLocal(elementId) {";
  page += "  var now = new Date();";
  page += "  var year = now.getFullYear();";
  page += "  var month = String(now.getMonth() + 1).padStart(2, '0');";
  page += "  var day = String(now.getDate()).padStart(2, '0');";
  page += "  var hours = String(now.getHours()).padStart(2, '0');";
  page += "  var minutes = String(now.getMinutes()).padStart(2, '0');";
  page += "  var localISO = year + '-' + month + '-' + day + 'T' + hours + ':' + minutes;";
  page += "  var el = document.getElementById(elementId);";
  page += "  if(el && !el.value) el.value = localISO;";
  page += "}";
  page += "window.onload = function() { setNowLocal('starttime'); setNowLocal('endtime'); };";
  page += "</script>";

  page += "</head><body>";
  page += "<h2>DD9FJ: Temperaturlogger</h2>";

  // 3. Farblogik für das Badge (inklusive Grau für 0V/Kein Akku angeschlossen)
  String battColor = "#6c757d"; // Grau als Standard (falls z. B. 0V am Pin anliegen)
  if (liveBatt >= 3.85) {
    battColor = "#28a745"; // Grün (Voll / Sicherer Bereich)
  } else if (liveBatt >= 3.65) {
    battColor = "#ffc107"; // Gelb (Warnbereich)
  } else if (liveBatt > 0.5) {
    battColor = "#dc3545"; // Rot (Kritisch leer!)
  }

  // Hier wird das Badge jetzt garantiert und deutlich sichtbar als erstes Element eingefügt:
  page += "<div class='batt-badge' style='background:" + battColor + ";'>&#128267; Akku-Spannung: " + String(liveBatt, 2) + " V</div>";

  sensors.requestTemperatures();
  int currentDevCount = sensors.getDeviceCount();
  if (currentDevCount > MAX_SENSORS) currentDevCount = MAX_SENSORS;

  if (isRunning) {
    page += "<div class='card' style='border-left: 5px solid #17a2b8;'>";
    page += "<h3>Zwischenstopp / Auslesen (Messung AKTIV)</h3>";
    page += "<p><b>Startzeit (Lokal):</b> " + formatDateTime(startUnixTime) + "</p>";
    page += "<p><b>Messfortschritt:</b> " + String(sampleCount) + " / " + String(targetSamples) + " Werten</p>";
    
    page += "<a href='/download' class='btn'>Bisherige CSV Herunterladen</a>";
    page += "<a href='/resume' class='btn btn-info' style='margin-left:8px;'>Weiter messen</a>";
    
    page += "<form action='/finish' method='POST' style='margin-top:15px;border-top:1px solid #ddd;padding-top:10px;'>";
    page += "<label for='endtime'>Messung JETZT beenden & Zeitstempel driftausgleichen:</label>";
    page += "<input type='datetime-local' name='endtime' id='endtime' required>";
    page += "<input type='submit' class='btn btn-warning' value='Messung Beenden & Zeit Korrigieren'>";
    page += "</form>";

    page += "<a href='/clear' class='btn btn-danger' style='margin-top:15px;'>Messung abbrechen & Löschen</a>";
    page += "</div>";
  }
  else {
    page += "<div class='card'>";
    if (sampleCount > 0) {
      page += "<h3>Messung beendet / Auswertung</h3>";
      page += "<p><b>Startzeit (Lokal):</b> " + formatDateTime(startUnixTime) + "</p>";
      page += "<p><b>Erfasste Messwerte:</b> " + String(sampleCount) + " / " + String(targetSamples) + "</p>";

      page += "<a href='/download' class='btn'>Vollständige CSV Herunterladen</a>";

      page += "<form action='/finish' method='POST' style='margin-top:15px;border-top:1px solid #ddd;padding-top:10px;'>";
      page += "<label for='endtime'>Tatsächliche Endzeit (Lokal) für Driftausgleich anpassen:</label>";
      page += "<input type='datetime-local' name='endtime' id='endtime' required>";
      page += "<input type='submit' class='btn btn-warning' value='Nachträglich Zeit Korrigieren'>";
      page += "</form>";

      page += "<a href='/clear' class='btn btn-danger' style='margin-top:15px;'>Speicher löschen & Neu konfigurieren</a>";
    } else {
      page += "<h3>Neue Messung konfigurieren</h3>";
      page += "<form action='/start' method='POST'>";
      
      page += "<label for='starttime'>Startzeitpunkt (Lokalzeit):</label>";
      page += "<input type='datetime-local' name='starttime' id='starttime' required>";

      page += "<label>Erkannte Sensoren & Namen vergeben:</label>";
      for (int i = 0; i < currentDevCount; i++) {
        float liveTemp = sensors.getTempCByIndex(i);
        page += "<div class='sensor-box'>";
        page += "<b>Sensor " + String(i + 1) + "</b> (Aktuelle Temp: " + String(liveTemp, 2) + " &deg;C):";
        page += "<input type='text' name='sname_" + String(i) + "' value='Sensor_" + String(i + 1) + "' maxlength='18' required>";
        page += "</div>";
      }

      page += "<label for='interval'>Messintervall:</label>";
      page += "<select name='interval' id='interval'>";
      for (int i = 1; i <= 10; i++) {
        page += "<option value='" + String(i) + "'" + (i == 5 ? " selected" : "") + ">" + String(i) + " Minute(n)</option>";
      }
      for (int i = 15; i <= 360; i += 15) {
        page += "<option value='" + String(i) + "'>" + String(i) + " Min (" + String(i/60.0, 2) + " Std)</option>";
      }
      page += "</select>";

      page += "<label for='duration'>Geplante Gesamtlaufzeit:</label>";
      page += "<select name='duration' id='duration'>";
      page += "<optgroup label='Stunden'>";
      for (int h = 1; h <= 24; h++) {
        page += "<option value='h_" + String(h) + "'>" + String(h) + " Stunde(n)</option>";
      }
      page += "</optgroup><optgroup label='Tage / Monate'>";
      for (int d = 2; d <= 365; d++) {
        page += "<option value='d_" + String(d) + "'" + (d == 2 ? " selected" : "") + ">" + String(d) + " Tage</option>";
      }
      page += "</optgroup>";
      page += "</select>";

      page += "<input type='submit' class='btn' value='Messreihe Starten'>";
      page += "</form>";
    }
    page += "</div>";
  }

  // Vorschau-Tabelle (Zeigt jetzt die LETZTEN 150 Werte)
  if (LittleFS.exists(FILE_PATH) && sampleCount > 0) {
    page += "<h3>Messwerte Vorschau</h3>";
    File file = LittleFS.open(FILE_PATH, FILE_READ);
    if (file) {
      page += "<table>";
      int maxPreview = 150;

      if (file.available()) {
        String headerLine = file.readStringUntil('\n');
        headerLine.trim();
        page += "<tr>";
        int startPos = 0;
        int commaPos = headerLine.indexOf(',');
        while (commaPos != -1) {
          page += "<th>" + headerLine.substring(startPos, commaPos) + "</th>";
          startPos = commaPos + 1;
          commaPos = headerLine.indexOf(',', startPos);
        }
        page += "<th>" + headerLine.substring(startPos) + "</th></tr>";
      }

      long linesToSkip = 0;
      if (sampleCount > maxPreview) {
        linesToSkip = sampleCount - maxPreview;
      }
      
      long skipped = 0;
      while (file.available() && skipped < linesToSkip) {
        if (file.read() == '\n') {
          skipped++;
        }
      }

      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
          page += "<tr>";
          int startPos = 0;
          int commaPos = line.indexOf(',');
          while (commaPos != -1) {
            page += "<td>" + line.substring(startPos, commaPos) + "</td>";
            startPos = commaPos + 1;
            commaPos = line.indexOf(',', startPos);
          }
          page += "<td>" + line.substring(startPos) + "</td></tr>";
        }
      }
      page += "</table>";

      if (sampleCount > maxPreview) {
        page += "<p><i>Hinweis: Es werden die <b>letzten " + String(maxPreview) + "</b> von insgesamt " + String(sampleCount) + " Messwerten angezeigt. Lade die CSV herunter, um alle Daten zu sehen.</i></p>";
      } else {
        page += "<p><i>Hinweis: Es werden alle bisherigen " + String(sampleCount) + " Messwerte angezeigt.</i></p>";
      }

      file.close();
    }
  }

  page += "</body></html>";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", buildHtmlPage());
}

void handleStart() {
  if (server.hasArg("interval") && server.hasArg("duration") && server.hasArg("starttime")) {
    intervalMin = server.arg("interval").toInt();
    String durationStr = server.arg("duration");
    String starttimeStr = server.arg("starttime");

    long totalMinutes = 0;
    if (durationStr.startsWith("h_")) {
      totalMinutes = durationStr.substring(2).toInt() * 60L;
    } else if (durationStr.startsWith("d_")) {
      totalMinutes = durationStr.substring(2).toInt() * 1440L;
    }

    targetSamples = totalMinutes / intervalMin;
    if (targetSamples < 1) targetSamples = 1;

    struct tm tm_time;
    memset(&tm_time, 0, sizeof(tm_time));
    sscanf(starttimeStr.c_str(), "%d-%d-%dT%d:%d",
           &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
           &tm_time.tm_hour, &tm_time.tm_min);

    tm_time.tm_year -= 1900;
    tm_time.tm_mon -= 1;
    tm_time.tm_isdst = -1;
    startUnixTime = mktime(&tm_time);

    sensors.begin();
    sensors.setResolution(12);  // Zwingt DS18B20 auf 12-Bit Auflösung (0.0625 °C)

    deviceCount = sensors.getDeviceCount();
    if (deviceCount > MAX_SENSORS) deviceCount = MAX_SENSORS;

    for (int i = 0; i < deviceCount; i++) {
      String argName = "sname_" + String(i);
      if (server.hasArg(argName)) {
        strncpy(sensorNames[i], server.arg(argName).c_str(), 19);
        sensorNames[i][19] = '\0';
      } else {
        snprintf(sensorNames[i], 20, "Sensor_%d", i + 1);
      }
    }

    File file = LittleFS.open(FILE_PATH, FILE_WRITE);
    if (file) {
      file.print("Messung,Datum_Uhrzeit_Lokal,V_Akku");  // Header um V_Akku erweitert
      for (int i = 0; i < deviceCount; i++) {
        file.print(",");
        file.print(sensorNames[i]);
      }
      file.println();
      file.close();
    }

    sampleCount = 0;
    isRunning = true;

    String response = "<html><body><h2>Messung gestartet!</h2>";
    response += "<p>Der ESP32 schaltet das WLAN ab und geht in den Schlafmodus.</p>";
    response += "</body></html>";

    server.send(200, "text/html", response);
    delay(2000);

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_sleep_enable_timer_wakeup((uint64_t)intervalMin * 60 * 1000000ULL);
    esp_deep_sleep_start();
  } else {
    server.send(400, "text/plain", "Fehlerhafte Parameter!");
  }
}

void handleFinish() {
  if (server.hasArg("endtime")) {
    String endtimeStr = server.arg("endtime");

    struct tm tm_time;
    memset(&tm_time, 0, sizeof(tm_time));
    sscanf(endtimeStr.c_str(), "%d-%d-%dT%d:%d",
           &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
           &tm_time.tm_hour, &tm_time.tm_min);

    tm_time.tm_year -= 1900;
    tm_time.tm_mon -= 1;
    tm_time.tm_isdst = -1;
    time_t endUnixTime = mktime(&tm_time);

    correctTimeStamps(endUnixTime);
    isRunning = false;

    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='2;url=/'></head><body><h2>Zeitstempel erfolgreich korrigiert! Lade Seite neu...</h2></body></html>");
  } else {
    server.send(400, "text/plain", "Keine Endzeit angegeben.");
  }
}

void handleDownload() {
  if (LittleFS.exists(FILE_PATH)) {
    File file = LittleFS.open(FILE_PATH, FILE_READ);
    server.sendHeader("Content-Disposition", "attachment; filename=temperaturlogger.csv");
    server.streamFile(file, "text/csv");
    file.close();
  } else {
    server.send(404, "text/plain", "Keine Messdaten gefunden.");
  }
}

void handleResume() {
  server.send(200, "text/html", "<html><body><h3>Messung wird fortgesetzt...</h3></body></html>");
  delay(1500);
  WiFi.softAPdisconnect(true);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_sleep_enable_timer_wakeup((uint64_t)intervalMin * 60 * 1000000ULL);
  esp_deep_sleep_start();
}

void handleClear() {
  LittleFS.remove(FILE_PATH);
  sampleCount = 0;
  isRunning = false;
  server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='0;url=/'></head></html>");
}

void runAPSession(int timeoutSeconds) {
  WiFi.softAP(ap_ssid, ap_password);

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/finish", handleFinish);
  server.on("/download", handleDownload);
  server.on("/resume", handleResume);
  server.on("/clear", handleClear);
  server.begin();

  unsigned long startTime = millis();
  unsigned long timeoutMs = timeoutSeconds * 1000UL;

  while (true) {
    server.handleClient();
    delay(10);

    if (isRunning && timeoutSeconds > 0) {
      if (millis() - startTime >= timeoutMs) {
        break;
      }
    }
  }

  WiFi.softAPdisconnect(true);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_sleep_enable_timer_wakeup((uint64_t)intervalMin * 60 * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BATT_ADC_PIN, INPUT);  // GPIO 35 als reinen Analogeingang initialisieren

  if (!LittleFS.begin(true)) {
    // LittleFS init
  }

  sensors.begin();
  sensors.setResolution(12);  // Zwingt alle Sensoren am Bus auf 12-Bit Auflösung

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool buttonPressed = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) || (digitalRead(BOOT_BUTTON_PIN) == LOW);

  if (isRunning) {
    if (buttonPressed) {
      runAPSession(180);
    } else {
      takeMeasurement();

      if (sampleCount >= targetSamples) {
        isRunning = false;
        runAPSession(0);
      } else {
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
        esp_sleep_enable_timer_wakeup((uint64_t)intervalMin * 60 * 1000000ULL);
        esp_deep_sleep_start();
      }
    }
  } else {
    runAPSession(0);
  }
}

void loop() {
  // Unerreicht
}