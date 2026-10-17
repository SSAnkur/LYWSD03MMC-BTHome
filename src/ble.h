#pragma once
#include "tl_common.h"

/* Connectable advertising + GATT server with the Telink OTA service
 * (00010203-0405-0607-0809-0a0b0c0d1912), structured like
 * pvvx/ATC_MiThermometer ble.c / app_att.c. */

extern u8 ble_connected;	/* a central is connected */
extern u8 ota_active;		/* OTA transfer in progress */
extern u8 ota_window;		/* connectable OTA window open */

/* Called once from user_init_normal(). open_window: battery was just
 * inserted, accept connections for OTA_WINDOW_S; otherwise go straight to
 * non-connectable advertising. adv_prepare is called by the stack
 * right before every advertising event (pvvx app_advertise_prepare_handler):
 * the application piggybacks its periodic work on those wake-ups. */
void ble_init(const u8 *adv, int adv_len, int (*adv_prepare)(void *),
	      int open_window);

/* Every main-loop pass: sets the sleep mode; returns 1 while OTA runs */
int ble_task(void);

/* Restore what deep-sleep retention loses (from user_init_deepRetn) */
void ble_init_deep_retention(void);
