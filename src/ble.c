/*
 * BLE peripheral: connectable advertising, GATT server with the Telink OTA
 * service, and the connection / OTA power handling of
 * pvvx/ATC_MiThermometer (ble.c, app_att.c), MIT-style license:
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software ... to deal in the Software without restriction.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * OTA itself (packet handling, CRC, flash writes, swapping the boot bank
 * and rebooting) is done by the Telink BLE library: otaWrite()/otaRead().
 * The new image is written to the bank that is not running (0x00000 or
 * 0x20000, 128 KiB each), so a failed transfer leaves the old firmware
 * bootable.
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "stack/ble/service/ble_ll_ota.h"
#include "vendor/common/blt_common.h"
#include "app_config.h"
#include "ble.h"
#include "lcd.h"

void bls_set_advertise_prepare(void *p);	/* in the library, no header */

/* Peripheral Preferred Connection Parameters (not in this SDK's headers) */
typedef struct {
	u16 intervalMin;
	u16 intervalMax;
	u16 latency;
	u16 timeout;
} gap_periConnectParams_t;

/* ----- link-layer buffers (required by the library; sizes as pvvx) ----- */
RAM u8 blt_rxfifo_b[64 * 8] = { 0 };
RAM my_fifo_t blt_rxfifo = { 64, 8, 0, 0, blt_rxfifo_b, };
RAM u8 blt_txfifo_b[40 * 16] = { 0 };
RAM my_fifo_t blt_txfifo = { 40, 16, 0, 0, blt_txfifo_b, };

RAM u8 ble_connected;
RAM u8 ota_active;
RAM u8 ota_window;
RAM static u32 window_start;
#define OTA_WINDOW_TICKS	((u32)OTA_WINDOW_S * CLOCK_16M_SYS_TIMER_CLK_1S)

static void close_window(void);
RAM static u8 conn_param_accepted;

RAM static u8 mac_public[6];
static u8 mac_random_static[6];

/* Scan response: complete local name DEV_NAME */
#define NAME_LEN	(sizeof(DEV_NAME) - 1)
RAM static u8 scan_rsp[2 + NAME_LEN];

/* ============================ GATT table ============================ */

enum {
	ATT_H_START = 0,

	GAP_PS_H,		/* 0x1800 Generic Access */
	GAP_DEVNAME_CD_H,
	GAP_DEVNAME_DP_H,
	GAP_APPEAR_CD_H,
	GAP_APPEAR_DP_H,
	GAP_CONNPAR_CD_H,
	GAP_CONNPAR_DP_H,

	GATT_PS_H,		/* 0x1801 Generic Attribute */
	GATT_SC_CD_H,
	GATT_SC_DP_H,
	GATT_SC_CCC_H,

	OTA_PS_H,		/* Telink OTA */
	OTA_CD_H,
	OTA_DP_H,
	OTA_DESC_H,

	ATT_END_H,
};

static const u16 primary_service_uuid = GATT_UUID_PRIMARY_SERVICE;
static const u16 character_uuid = GATT_UUID_CHARACTER;
static const u16 ccc_uuid = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 user_desc_uuid = GATT_UUID_CHAR_USER_DESC;

/* GAP */
static const u16 gap_service_uuid = SERVICE_UUID_GENERIC_ACCESS;
static const u16 devname_uuid = GATT_UUID_DEVICE_NAME;
static const u16 appearance_uuid = GATT_UUID_APPEARANCE;
static const u16 connpar_uuid = GATT_UUID_PERI_CONN_PARAM;
static const u16 appearance = 768;	/* Generic Thermometer */
static const gap_periConnectParams_t periConnParameters = {
	CONN_INTERVAL, CONN_INTERVAL, CONN_LATENCY, CONN_TIMEOUT
};
static const u8 devname_cd[5] = {
	CHAR_PROP_READ,
	U16_LO(GAP_DEVNAME_DP_H), U16_HI(GAP_DEVNAME_DP_H),
	U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)
};
static const u8 appear_cd[5] = {
	CHAR_PROP_READ,
	U16_LO(GAP_APPEAR_DP_H), U16_HI(GAP_APPEAR_DP_H),
	U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)
};
static const u8 connpar_cd[5] = {
	CHAR_PROP_READ,
	U16_LO(GAP_CONNPAR_DP_H), U16_HI(GAP_CONNPAR_DP_H),
	U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)
};

/* GATT */
static const u16 gatt_service_uuid = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const u16 service_change_uuid = GATT_UUID_SERVICE_CHANGE;
static u32 service_change_val;
RAM static u16 service_change_ccc;
static const u8 service_change_cd[5] = {
	CHAR_PROP_INDICATE,
	U16_LO(GATT_SC_DP_H), U16_HI(GATT_SC_DP_H),
	U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)
};

/* Telink OTA */
static const u8 ota_service_uuid[16] = TELINK_OTA_UUID_SERVICE;
static const u8 ota_data_uuid[16] = TELINK_SPP_DATA_OTA;
static u8 ota_data;
static const u8 ota_name[] = { 'O', 'T', 'A' };
static const u8 ota_cd[19] = {
	CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP,
	U16_LO(OTA_DP_H), U16_HI(OTA_DP_H),
	0x12, 0x2B, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
	0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
};


/* Make the library's OTA timeout an inactivity timeout (pvvx otaWritePre):
 * every OTA packet restarts it. */
static int ota_write_pre(void *p)
{
	blt_ota_start_tick = clock_time() | 1;
	return otaWrite(p);
}

/* In RAM, as pvvx: the stack walks this table from interrupt context,
 * which must not depend on flash reads while OTA is erasing/writing it. */
RAM static attribute_t attributes[] = {
	{ ATT_END_H - 1, 0, 0, 0, 0, 0, 0, 0 },	/* total number of attributes */

	{ 7, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&primary_service_uuid, (u8 *)&gap_service_uuid, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(devname_cd), (u8 *)&character_uuid, (u8 *)devname_cd, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, NAME_LEN, (u8 *)&devname_uuid, &scan_rsp[2], 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(appear_cd), (u8 *)&character_uuid, (u8 *)appear_cd, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(appearance), (u8 *)&appearance_uuid, (u8 *)&appearance, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(connpar_cd), (u8 *)&character_uuid, (u8 *)connpar_cd, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(periConnParameters), (u8 *)&connpar_uuid, (u8 *)&periConnParameters, 0, 0 },

	{ 4, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&primary_service_uuid, (u8 *)&gatt_service_uuid, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(service_change_cd), (u8 *)&character_uuid, (u8 *)service_change_cd, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(service_change_val), (u8 *)&service_change_uuid, (u8 *)&service_change_val, 0, 0 },
	{ 0, ATT_PERMISSIONS_RDWR, 2, sizeof(service_change_ccc), (u8 *)&ccc_uuid, (u8 *)&service_change_ccc, 0, 0 },

	{ 4, ATT_PERMISSIONS_READ, 2, 16, (u8 *)&primary_service_uuid, (u8 *)ota_service_uuid, 0, 0 },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(ota_cd), (u8 *)&character_uuid, (u8 *)ota_cd, 0, 0 },
	{ 0, ATT_PERMISSIONS_RDWR, 16, sizeof(ota_data), (u8 *)ota_data_uuid, &ota_data, &ota_write_pre, &otaRead },
	{ 0, ATT_PERMISSIONS_READ, 2, sizeof(ota_name), (u8 *)&user_desc_uuid, (u8 *)ota_name, 0, 0 },

};

/* ======================= connection / OTA events ======================= */

/* OTA start command received: full radio duty until it is done */
static void ota_start_cb(void)
{
	ota_active = 1;
	conn_param_accepted = 0;
	bls_pm_setManualLatency(0);
	bls_ota_setTimeout(OTA_IDLE_TIMEOUT_US);
}

/* After a successful OTA the library reboots into the new image itself;
 * this only matters for failures. */
static void ota_result_cb(int result)
{
	(void)result;
	ota_active = 0;
}

/* Ask for 20 ms interval + latency 49: the device then only has to wake for
 * one connection event per second while idle (pvvx ble_connect_callback). */
static void connect_cb(u8 e, u8 *p, int n)
{
	(void)e; (void)p; (void)n;
	ble_connected = 1;
	conn_param_accepted = 0;
	bls_l2cap_setMinimalUpdateReqSendingTime_after_connCreate(1000);
	bls_l2cap_requestConnParamUpdate(CONN_INTERVAL, CONN_INTERVAL,
					 CONN_LATENCY, CONN_TIMEOUT);
	lcd_show_ble(1);
}

static void disconnect_cb(u8 e, u8 *p, int n)
{
	(void)e; (void)p; (void)n;
	ble_connected = 0;
	ota_active = 0;
	lcd_show_ble(ota_window);
	/* the stack resumes advertising by itself; ble_task() switches it to
	 * non-connectable if the window has expired meanwhile */
}

static int conn_param_rsp_cb(u8 id, u16 result)
{
	(void)id;
	if (result == CONN_PARAM_UPDATE_ACCEPT) {
		conn_param_accepted = 1;
	} else if (result == CONN_PARAM_UPDATE_REJECT) {
		bls_l2cap_requestConnParamUpdate(CONN_INTERVAL, CONN_INTERVAL,
						 CONN_LATENCY, CONN_TIMEOUT);
	}
	return 0;
}

/* Suspend loses the RF power setting (pvvx suspend_exit_cb) */
_attribute_ram_code_
static void suspend_exit_cb(u8 e, u8 *p, int n)
{
	(void)e; (void)p; (void)n;
	rf_set_power_level_index(RF_TX_POWER);
}

/* ============================== init ============================== */

void ble_init(const u8 *adv, int adv_len, int (*adv_prepare)(void *),
	      int open_window)
{
	/* Factory public address (A4:C1:38:...) from the stock flash layout */
	blc_initMacAddress(CFG_ADR_MAC, mac_public, mac_random_static);

	memcpy(&scan_rsp[2], DEV_NAME, NAME_LEN);
	scan_rsp[0] = NAME_LEN + 1;
	scan_rsp[1] = GAP_ADTYPE_LOCAL_NAME_COMPLETE;

	/* Controller */
	blc_ll_initStandby_module(mac_public);
	blc_ll_initAdvertising_module(mac_public);
	blc_ll_initConnection_module();
	blc_ll_initSlaveRole_module();

	/* Host */
	blc_gap_peripheral_init();
	bls_att_setAttributeTable((u8 *)attributes);
	blc_l2cap_register_handler(blc_l2cap_packet_receive);
	blc_smp_setSecurityLevel(No_Security);

	rf_set_power_level_index(RF_TX_POWER);
	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &connect_cb);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &disconnect_cb);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &suspend_exit_cb);
	blc_l2cap_registerConnUpdateRspCb(conn_param_rsp_cb);

	/* OTA: erase the inactive bank if it still holds an old image */
	bls_ota_clearNewFwDataArea();
	bls_ota_registerStartCmdCb(ota_start_cb);
	bls_ota_registerResultIndicateCb(ota_result_cb);

	/* Advertising */
	bls_ll_setAdvData((u8 *)adv, adv_len);
	bls_ll_setScanRspData(scan_rsp, sizeof(scan_rsp));
	bls_set_advertise_prepare((void *)adv_prepare);
	if (open_window) {
		/* 1 s interval so the flashing page finds and connects quickly */
		ota_window = 1;
		window_start = clock_time();
		bls_ll_setAdvParam(OTA_WINDOW_ADV_INTERVAL,
				   OTA_WINDOW_ADV_INTERVAL + ADV_INTERVAL_DELAY,
				   ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC,
				   0, NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
		bls_ll_setAdvEnable(BLC_ADV_ENABLE);
		lcd_show_ble(1);	/* BLE symbol = OTA window open */
	} else {
		close_window();
	}

	/* Power management (pvvx init_ble) */
	blc_ll_initPowerManagement_module();
	blc_pm_setDeepsleepRetentionThreshold(40, 18);
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(240);
	blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW32K);
	bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV |
			      SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	bls_pm_setManualLatency(0);
}

/* Called every main-loop pass. During OTA: answer every connection event
 * (no slave latency) and never use deep-sleep retention, which would drop
 * the transfer state. Otherwise allow the deepest sleep everywhere. */
/* Window over: non-connectable advertising at the normal interval. In
 * this mode the radio does not listen after each packet. */
static void close_window(void)
{
	ota_window = 0;
	bls_ll_setAdvParam(ADV_INTERVAL, ADV_INTERVAL + ADV_INTERVAL_DELAY,
			   ADV_TYPE_NONCONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC,
			   0, NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);
	lcd_show_ble(0);
}

_attribute_ram_code_
int ble_task(void)
{
	/* An upload that started inside the window may finish after it; any
	 * other connection is dropped when the window ends. */
	if (ota_window && !ota_active &&
	    clock_time() - window_start > OTA_WINDOW_TICKS) {
		if (ble_connected) {
			bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN);
		} else {
			close_window();
		}
	}
	if (ota_active) {
		if (!conn_param_accepted) {
			bls_pm_setManualLatency(0);
		}
		bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
		return 1;
	}
	bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV |
			      SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	return 0;
}

_attribute_ram_code_
void ble_init_deep_retention(void)
{
	rf_set_power_level_index(RF_TX_POWER);
	blc_ll_recoverDeepRetention();
	bls_ota_registerStartCmdCb(ota_start_cb);	/* as pvvx */
}
