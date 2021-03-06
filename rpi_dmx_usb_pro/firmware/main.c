/**
 * @file main.c
 *
 */
/* Copyright (C) 2016, 2017 by Arjan van Vught mailto:info@raspberrypi-dmx.nl
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <led.h>
#include <stdio.h>
#include <stdint.h>

#include "hardware.h"
#include "usb.h"
#include "util.h"

#include "monitor.h"
#include "oled.h"

#include "dmx.h"

#include "rdm_monitor.h"
#include "rdm_device_info.h"
#include "rdm_device_const.h"

#include "widget_params.h"
#include "widget.h"

void __attribute__((interrupt("FIQ"))) c_fiq_handler(void) {}
void __attribute__((interrupt("IRQ"))) c_irq_handler(void) {}

static char widget_mode_names[4][12] ALIGNED = {"DMX_RDM", "DMX", "RDM" , "RDM_SNIFFER" };

struct _poll {
	void (*f)(void);
}static const poll_table[] ALIGNED = {
		{ widget_receive_data_from_host },
		{ widget_received_dmx_packet },
		{ widget_received_dmx_change_of_state_packet },
		{ widget_received_rdm_packet },
		{ widget_rdm_timeout },
		{ widget_sniffer_rdm },
		{ widget_sniffer_dmx },
		{ led_blink } };

struct _event {
	const uint32_t period;
	void (*f)(void);
}static const events[] ALIGNED = {
		{ 1000000, monitor_update } };

static uint32_t events_elapsed_time[sizeof(events) / sizeof(events[0])];

/**
 * @ingroup main
 *
 */
static void events_init() {
	size_t i;
	const uint32_t mircos_now = hardware_micros();
	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		events_elapsed_time[i] += mircos_now;
	}
}

/**
 * @ingroup main
 *
 */
inline static void events_check() {
	size_t i;
	const uint32_t micros_now = hardware_micros();
	for (i = 0; i < (sizeof(events) / sizeof(events[0])); i++) {
		if (micros_now - events_elapsed_time[i] > events[i].period) {
			events[i].f();
			events_elapsed_time[i] += events[i].period;
			hardware_watchdog_feed();
		}
	}
}

/**
 * @ingroup main
 *
 * @return
 */
int notmain(void) {
	oled_info_t oled_info  = { OLED_128x64_SPI_CS2_DEFAULT };
	bool oled_connected = false;
	int i = 0;
	_widget_mode widget_mode;
	const uint8_t *uid_device;

	oled_connected = oled_start(&oled_info);
	monitor_set_oled(oled_connected ? &oled_info : NULL);

	usb_init();

	dmx_init();
	dmx_set_port_direction(DMX_PORT_DIRECTION_INP, true);

	widget_params_init();
	rdm_device_info_init();

	widget_mode = widget_get_mode();
	uid_device = (const uint8_t *)rdm_device_info_get_uuid();

	printf("[V%s] %s Compiled on %s at %s\n", DEVICE_SOFTWARE_VERSION, hardware_board_get_model(), __DATE__, __TIME__);
	printf("RDM Controller with USB [Compatible with Enttec USB Pro protocol], Widget mode : %d (%s)\n", widget_mode, widget_mode_names[widget_mode]);
	printf("Device UUID : %.2x%.2x:%.2x%.2x%.2x%.2x, Label : ", uid_device[0], uid_device[1], uid_device[2], uid_device[3], uid_device[4], uid_device[5]);
	monitor_print_root_device_label();

	if (oled_connected) {
		oled_set_cursor(&oled_info,0,0);
		(void) oled_printf(&oled_info, "[V%s] RDM Controller", DEVICE_SOFTWARE_VERSION);
		oled_set_cursor(&oled_info,1,0);
		(void) oled_printf(&oled_info,"UUID: %.2x%.2x:%.2x%.2x%.2x%.2x", uid_device[0], uid_device[1], uid_device[2], uid_device[3], uid_device[4], uid_device[5]);
		oled_set_cursor(&oled_info,2,0);
		oled_puts(&oled_info, "L:");
		// 2 and 3 = Device label
		oled_set_cursor(&oled_info,5,0);
		(void) oled_printf(&oled_info,"Mode: %d (%s)", widget_mode, widget_mode_names[widget_mode]);
	}

	hardware_watchdog_init();

	if (widget_get_mode() == MODE_RDM_SNIFFER) {
		widget_sniffer_fill_transmit_buffer();	// Prevent missing first frame
	}

	events_init();

	for (;;) {
		hardware_watchdog_feed();
		for (i = 0; i < sizeof(poll_table) / sizeof(poll_table[0]); i++) {
			poll_table[i].f();
		}

		events_check();
	}

	return 0;
}
