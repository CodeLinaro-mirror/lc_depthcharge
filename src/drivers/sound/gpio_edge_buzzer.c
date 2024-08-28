/*
 * Copyright 2017 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 */

#include <libpayload.h>

#include "base/container_of.h"
#include "drivers/sound/gpio_edge_buzzer.h"

static const uint32_t USEC_PER_SEC = 1000000;
static const uint32_t MAX_BUZZER_FREQUENCY_HZ = 2700;
static const uint32_t MIN_BUZZER_PERIOD_US = (USEC_PER_SEC / MAX_BUZZER_FREQUENCY_HZ);

static int buzzer_play(SoundOps *me, uint32_t msec, uint32_t frequency)
{
	GpioEdgeBuzzer *buzzer = container_of(me, GpioEdgeBuzzer, ops);
	u64 start;
	int period_us;
	int i = 0;

	if (!frequency)
		return -1;

	/* Limiting frequency to 2.7kHz which is peak loudness */
	period_us = USEC_PER_SEC / frequency;
	period_us = (period_us < MIN_BUZZER_PERIOD_US) ? MIN_BUZZER_PERIOD_US : period_us;

	start = timer_us(0);
	while (timer_us(start)/1000 < msec) {
		buzzer->gpio->set(buzzer->gpio, ++i & 1);
		udelay(period_us/2);
	}
	buzzer->gpio->set(buzzer->gpio, 0);

	return 0;
}

GpioEdgeBuzzer *new_gpio_edge_buzzer(GpioOps *gpio)
{
	GpioEdgeBuzzer *buzzer = xzalloc(sizeof(*buzzer));

	buzzer->gpio = gpio;
	buzzer->ops.play = buzzer_play;

	return buzzer;
}
