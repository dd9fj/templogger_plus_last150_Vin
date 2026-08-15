Dieser Logger entstand aus der Problemstellung eines (möglicherweise) defekten Gefrierschrankes ;-)

Ich habe in meinem Bastelbestand meist kleine Microcontroller wie Arduino oder ESP32, ein paar 
Temperaturfühler wie ds18b20 ebenso. 

..verwendet wurde ein ESP32 Dev1 mit 38 Pin's

Also mal flott einen ESP32 mit einem ONEWIRE Sensor verbunden, an eine Powerbank gesteckt und den 
Gefrierschrank über 24 Stunden geloggt..

Das kann man mit diesem Code sofort machen, außer einem ONEWIRE Sensor (DS18B20) einem Vorwiderstand und einem ESP32 
wird nichts benötigt. (Stromversorgung mal außen vor..)

Ich habe dann später eine Spannungsanzeige und das loggen der Versorgungsspannung hinzugenommen, das ist aber optional und nicht zwingend.
Es können bis zu 8 Sensoren parallel angeschlossen werden, diese können zur besseren Unterscheidung auch umbenannt werden.

Angeschlossen wird der Sensor an 3,3v des ESP und GND, 
der Datenanschluß des Sensors wird mit 4k7* Widerstand gegen 3,3V "hochgezogen"und an GPIO 4 angeschlossen.
*kann bei langen Zuleitungen und mehreren Sensoren auf 2k7 verringert werden


Um die Versorgungsspannung zu messen, wird GPIO 35 mit einem Spannungsteiler, bestehend aus zwei 100kOhm Widerständen einer zu Vin, der Andere zu GND bestückt. 
(die Widerstände sollten möglichst identisch sein - ausmessen hilft. Man kann aber auch einen Kalibrierungswert im Sketch anpassen)
