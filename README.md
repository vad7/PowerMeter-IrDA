# PowerMeter IrDA interface 
---

Web server based on esp8266 with IrDA interface to power meter (Mercury 230/231 AT).<br>
Over-The-Air firmware updating.
---

Возможности:
Получение данных со многотарифных счетчиков Меркурий 231 АТ (230) через инфракрасный порт.<br>
Отправка данных на IoT сервер.<br>
Автоматическая корректировка времени счетчика.<br>
Графики - по дням, по часам, детально по минутам.<br>
Отправка произвольной команды на счетчик.<br><br>

Использутся i2c FRAM память (30 байт).<br>
Данные для графиков записываются во флеш память модуля esp.<br>
Два циклических буфера - по дням на 7680 дней и детальное потребление до конца памяти (для флеши 4 Мбайта - 2136 дней).<br>

![alt tag](https://github.com/vad7/PowerMeter-IrDA/blob/master/Mercury-231.png)

![alt tag](https://github.com/vad7/PowerMeter-IrDA/blob/master/Web1.jpg)

![alt tag](https://github.com/vad7/PowerMeter-IrDA/blob/master/Web2.jpg)

![alt tag](https://github.com/vad7/PowerMeter-IrDA/blob/master/Web3.jpg)

![alt tag](https://github.com/vad7/PowerMeter-IrDA/blob/master/Web4.jpg)

![alt tag](https://github.com/vad7/PowerMeter-IrDA/blob/master/Web4_2.jpg)

![alt tag](https://github.com/vad7/PowerMeter-IrDA/blob/master/Web5.jpg)

Schematic: 

![SCH](https://github.com/vad7/PowerMeter-IrDA/blob/master/PowerMeter-IrDA.jpg)

VCC - 3.3V<br> 
FM24* - I2C FRAM memory<br> 

