// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020
 * Lutz Harder, SHPI GmbH <lh@shpi.de>
 *
 * LED driver for the WS2812B SPI 
 */

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>


#define WS2812_SYMBOL_LENGTH                     4
#define LED_COLOURS                              3
#define LED_RESET_US                             60
#define LED_BIT_COUNT                            ((NUM_LEDS * LED_COLOURS * 8 * 3) + ((LED_RESET_US * \
                                                  (WS2812_FREQ * WS2812_SYMBOL_LENGTH)) / 1000000))

#define LED_RESET_WAIT_TIME                      300
#define SPI_BYTE_COUNT                         ((((LED_BIT_COUNT >> 3) & ~0x7) + 4) + 4)
#define SYMBOL_HIGH                              0b1100 // 0.6us high 0.6us low
#define SYMBOL_LOW                               0b1000 // 0.3us high, 0.9us low
#define WS2812_FREQ                             800000
#define NUM_LEDS                                1



struct ws2812_led {
	struct led_classdev	ldev;
	int			id;
	char			name[sizeof("red-3")]; //red grn bl
	struct ws2812	*priv;
	};

struct ws2812 {
  struct spi_device	*spi;
  size_t			count;
  struct mutex		mutex;
  struct ws2812_led leds[NUM_LEDS*LED_COLOURS];
  uint8_t rawstream[SPI_BYTE_COUNT];
};




static void  ws2812_render(struct ws2812 *priv)
{
    int i, k, l;
    int bitpos =  7;
    int bytepos = 0;    // SPI
    int z = NUM_LEDS*LED_COLOURS;


    for (i = 0; i < z; i++)                // Leds * Colorchannels
        {

                for (k = 7; k >= 0; k--)                   // Bit
                {

                    uint8_t symbol = SYMBOL_LOW;
                    if ((uint8_t)priv->leds[i].ldev.brightness & (1 << k))
                        symbol = SYMBOL_HIGH;

                    for (l = WS2812_SYMBOL_LENGTH; l > 0; l--)               // Symbol
                    {
                        volatile uint8_t  *byteptr = &priv->rawstream[bytepos];    // SPI

                        *byteptr &= ~(1 << bitpos);
                        if (symbol & (1 << l))
                                *byteptr |= (1 << bitpos);
                        bitpos--;
                        if (bitpos < 0)
                        {
                                bytepos++;
                                bitpos = 7;
                            }
                        }
                    }
                }

}




static int ws2812_set_brightness(struct led_classdev *ldev,
				      enum led_brightness brightness)
{
	struct ws2812_led *led = container_of(ldev, struct ws2812_led,
						  ldev);


	int ret;

	mutex_lock(&led->priv->mutex);
  	ws2812_render(led->priv);
	ret = spi_write(led->priv->spi, (const u8 *)&led->priv->rawstream, SPI_BYTE_COUNT);
	mutex_unlock(&led->priv->mutex);

	return ret;
}

static int ws2812_probe(struct spi_device *spi)
{
	struct ws2812		*priv;
	struct ws2812_led	*led;
	int i, ret;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;


  	spi->mode = SPI_MODE_0;
  	spi->bits_per_word = 8;
  	spi->max_speed_hz = WS2812_FREQ * WS2812_SYMBOL_LENGTH;



	for (i = 0; i < NUM_LEDS*LED_COLOURS; i++) {
		led		= &priv->leds[i];
		led->id		= i;
		led->priv	= priv;
		// we split names later here in red,grn,blu 
		snprintf(led->name, sizeof(led->name), "red-%d", i);
		mutex_init(&led->priv->mutex);
		led->ldev.name = led->name;
		led->ldev.brightness = LED_OFF;
		led->ldev.max_brightness = 0xff;
		led->ldev.brightness_set_blocking = ws2812_set_brightness;
		ret = led_classdev_register(&spi->dev, &led->ldev);
		if (ret < 0)
			goto eledcr;
	}

	spi_set_drvdata(spi, priv);

	return 0;

eledcr:
	while (i--)
		led_classdev_unregister(&priv->leds[i].ldev);

	return ret;
}

static int ws2812_remove(struct spi_device *spi)
{
	struct ws2812	*priv = spi_get_drvdata(spi);
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->leds); i++)
		led_classdev_unregister(&priv->leds[i].ldev);

	return 0;
}

static struct spi_driver ws2812_driver = {
	.probe		= ws2812_probe,
	.remove		= ws2812_remove,
	.driver = {
		.name	= "ws2812",
	},
};

module_spi_driver(ws2812_driver);

MODULE_AUTHOR("Lutz Harder <lh@shpi.de>");
MODULE_DESCRIPTION("WS2812 SPI LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ws2812");
