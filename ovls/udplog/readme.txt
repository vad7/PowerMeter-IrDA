udplog.ovl

Вывод отладочных логов в UDP port, вместо UART1.

Инициализация:

cfg_overlay.ip_addr 	(было mdb[90]) - dword, host ip, по умолчанию 255.255.255.255 (всем)
cfg_overlay.array16b[0] (было mdb[92]) - word, remote/host port, по умолчанию 1025

Отправить буфер сейчас: ovl$=-1

Переменные:
cfg_overlay.array16b[1] (было mdb[93]) - Флаг:	
			=0 - драйвер закрыт
			=1 - драйвер установлен и работает 
cfg_overlay.array16b[2] (было mdb[94]) - Ошибки: =0 - нет ошибок

Запрос / Выходная информация или команда по UDP
'A?' - DRV: set remote ip , remote port
'I?' - UDP, TCP, WEB pcb info
'M?' - System memory info
'W?' - WiFi info
