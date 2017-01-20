# PowerMeter
---

Have the webserver, Over-The-Air firmware updating.

Schematic: 

![SCH](https://github.com/vad7/PowerMeter/blob/master/PowerMeter.jpg)

VCC - 3.3V<br> 
Q1 - Photo transistor<br> 
FM24* - I2C FRAM memory<br> 

SPI overlap FRAM memory (FM25V02) schematic,  
must be switched in user_config.h - set USE_HSPI and remark USE_I2C, set SPI_SPEED?=40 in the makefile:
 
![SCH](https://github.com/vad7/PowerMeter/blob/master/PowerMeter-SPI.jpg)

ESP-01 module: 

![alt tag](https://github.com/vad7/PowerMeter/blob/master/esp-01.jpg)


Based on [esp8266web](https://github.com/pvvx/esp8266web.git)

Доработки и изменения esp8266web:

1. Обновление прошивки по WiFi (firmware.bin). Загружается на место Web диска, затем при загрузке (Rapid_Loader_OTA) копируется на основное место. 
2. Уменьшен до 1 сектора (4096 байт) блок сохранения конфигурации в 0x7B000 (flash_epp), добавлена функция current_cfg_length().  
3. Попытка исправить потерю соединения к некоторым роутерам и отсутствие пере-подключения.
4. Увеличен лимит для размера переменных при сохранении настроек в web_int_vars по submit form (функции web_parse_*).
5. Добавлена функция записи в Web диск - WEBFSUpdateFile. Исправлены ошибки в библиотеке WEBFS (web/webfs.c).   
6. DNS ищет сначала в локальном кэше преждем чем лезть на сервер (sdklib/lwip/core/dns.c), минимальный TTL = 3600 сек.  
7. i2c драйвер с установкой скорости. Работа с FRAM памятью.  	
8. Рисование графиков с зумом с помощью java библиотеки d3.js. 
9. Выкладывание данных в IoT cloud - thingspeak.com через GET запрос (iot_cloud.с).
10. Отладка в RAM память.
11. SPI overlap
12. Другие доработки и исправления.
