/*
 * ovl_config.h
 *
 *  Created on: 17 марта 2016 г.
 *      Author: PVV
 */

#ifndef _INCLUDE_OVL_CONFIG_H_
#define _INCLUDE_OVL_CONFIG_H_

#define DEFAULT_UDP_PORT 1025

#ifdef USE_OVERLAY

#define drv_host_ip 	cfg_overlay.ip_addr
#define drv_host_port 	cfg_overlay.array16b[0]
#define drv_init_usr	cfg_overlay.array16b[1] // Флаг: =0 - драйвер закрыт, =1 - драйвер установлен и работает
#define drv_error		cfg_overlay.array16b[2] // Ошибки: =0 - нет ошибок

#else

ip_addr_t drv_host_ip;
uint16 drv_host_port;
uint16 drv_init_usr;
uint16 drv_error;

/*
#define drv_host_ip 	((ip_addr_t *)mdb_buf.ubuf)[90>>1]
#define drv_host_port 	mdb_buf.ubuf[92]
#define drv_init_usr	mdb_buf.ubuf[93] // Флаг: =0 - драйвер закрыт, =1 - драйвер установлен и работает
#define drv_error		mdb_buf.ubuf[94] // Ошибки: =0 - нет ошибок
*/
#endif

#endif /* _INCLUDE_OVL_CONFIG_H_ */
