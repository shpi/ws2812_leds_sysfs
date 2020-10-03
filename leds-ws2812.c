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
#include <linux/of.h>


#define WS2812_SYMBOL_LENGTH                     4
#define LED_COLOURS                              3
#define LED_RESET_US                             60
#define LED_RESET_WAIT_TIME                      300
#define SYMBOL_HIGH                              0b1100 // 0.6us high 0.6us low
#define SYMBOL_LOW                               0b1000 // 0.3us high, 0.9us low
#define WS2812_FREQ                             800000



struct ws2812_led {
	struct led_classdev	ldev;
	int			id;
	char			name[sizeof("ws2812-red-3")]; //red grn bl
	struct ws2812		*priv;
	u8			brightness;
	};


struct ws2812 {
  struct device		*dev;
  struct spi_device	*spi;
  struct mutex		mutex;
  u8                    *rawstream;
  int                   num_leds;
  int			spi_byte_count;
  struct ws2812_led 	leds[];
};




static int  ws2812_render(struct ws2812 *priv)
{
    volatile u8	*rawstream = priv->rawstream;
    int i, k, l;
    int bitpos =  7;
    int bytepos = 0;    // SPI
    int z = priv->num_leds*LED_COLOURS;


    for (i = 0; i < z; i++)                // Leds * Colorchannels
        {

                for (k = 7; k >= 0; k--)                   // Bit
                {

                    uint8_t symbol = SYMBOL_LOW;
                    if (priv->leds[i].brightness & (1 << k))
                        symbol = SYMBOL_HIGH;

                    for (l = WS2812_SYMBOL_LENGTH; l > 0; l--)               // Symbol
                    {
                        volatile u8  *byteptr = &rawstream[bytepos];    // SPI

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

	return spi_write(priv->spi, priv->rawstream, priv->spi_byte_count);
}




static int ws2812_set_brightness(struct led_classdev *ldev,
				      enum led_brightness brightness)
{
	struct ws2812_led *led = container_of(ldev, struct ws2812_led,
						  ldev);


	int ret;

	mutex_lock(&led->priv->mutex);
	led->brightness = (u8)brightness;
  	ret = ws2812_render(led->priv);
	mutex_unlock(&led->priv->mutex);

	return ret;
}

static int ws2812_probe(struct spi_device *spi)
{
	struct ws2812		*priv;
	struct ws2812_led	*led;
	const char		*color_order; //RGB,  GRB, ...
	int i, ret, spi_byte_count;
	int	num_leds;
	long led_bit_count;



	ret = device_property_read_u32(&spi->dev, "num-leds", &num_leds);
	if (ret < 0)
		num_leds = 1;

	dev_err(&spi->dev,"Number of WS2812 to be registered:%d\n",num_leds);


        ret = device_property_read_string(&spi->dev, "color-order", &color_order);
        if (ret < 0)
		color_order = "GRB";
		//strncpy(color_order, "GRB", 3);


	led_bit_count = ((num_leds * LED_COLOURS * 8 * 3) + ((LED_RESET_US * \
                                                  (WS2812_FREQ * WS2812_SYMBOL_LENGTH)) / 1000000));

	spi_byte_count = ((((led_bit_count >> 3) & ~0x7) + 4) + 4);


	priv = devm_kzalloc(&spi->dev, struct_size(priv, leds, num_leds*LED_COLOURS), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	priv->rawstream = devm_kzalloc(&spi->dev, spi_byte_count, GFP_KERNEL);

	if (!priv->rawstream)
		return -ENOMEM;

  	spi->mode = SPI_MODE_0;
  	spi->bits_per_word = 8;
  	spi->max_speed_hz = WS2812_FREQ * WS2812_SYMBOL_LENGTH;

	priv->dev = &spi->dev;
	priv->spi = spi;
	
	priv->num_leds = num_leds; 
	//priv->led_bit_count = led_bit_count;
	priv->spi_byte_count = spi_byte_count;
	
	for (i = 0; i < num_leds*LED_COLOURS; i++) {

		led		= &priv->leds[i];
		led->id		= i;
		led->priv	= priv;
		// we split names later here in red,grn,blu

		if (color_order[i % 3] == 'G') snprintf(led->name, sizeof(led->name), "ws2812-grn-%d", (i/3));
		if (color_order[i % 3] == 'R') snprintf(led->name, sizeof(led->name), "ws2812-red-%d", (i/3));
		if (color_order[i % 3] == 'B') snprintf(led->name, sizeof(led->name), "ws2812-blu-%d", (i/3));

		mutex_init(&led->priv->mutex);
		led->ldev.name = led->name;
		led->ldev.brightness = LED_OFF;
		led->brightness = 0;
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

	for (i = 0; i < (priv->num_leds*LED_COLOURS); i++)
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
