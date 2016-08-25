/*
 * Copyright 2015 franciscofranco
 * Copyright 2016 Guneet Atwal
 * Copyright 2016 JZshminer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/device.h>

#include <linux/miscdevice.h>
#include <linux/sound_control.h>
#include <linux/mfd/wcd9335/registers.h>

 #define HEADPHONE_BOOST_L_REG      WCD9335_CDC_RX1_RX_VOL_CTL
 #define HEADPHONE_BOOST_MIX_L_REG      WCD9335_CDC_RX1_RX_VOL_MIX_CTL
 #define HEADPHONE_BOOST_R_REG      WCD9335_CDC_RX2_RX_VOL_CTL
 #define HEADPHONE_BOOST_MIX_R_REG      WCD9335_CDC_RX2_RX_VOL_MIX_CTL
 #define SPEAKER_REG      WCD9335_CDC_RX7_RX_VOL_CTL
 #define MIC_REG      WCD9335_CDC_RX0_RX_VOL_CTL
 #define MIC_MIX_REG      WCD9335_CDC_RX0_RX_VOL_CTL

 #define HEADPHONE_BOOST_LIMIT 20
 #define SPEAKER_BOOST_LIMIT 10
 #define MIC_BOOST_LIMIT 10

#define dprintk(msg...)		\
do {				\
		pr_info("[ sound_control ] BOOST STATE: "msg);	\
} while (0)

//Headphones
int headphones_boost_l = 0;
int headphones_boost_l_ori = 0;
int headphones_boost_r = 0;
int headphones_boost_r_ori = 0;
int headphones_boost_limit = HEADPHONE_BOOST_LIMIT;

//Speakers
int speaker_boost = 0;
int speaker_boost_ori = 0;
int speaker_boost_limit = SPEAKER_BOOST_LIMIT;

//Micrphone/Earpiece
int mic_boost = 0;
int mic_boost_ori = 0;
int mic_boost_limit = MIC_BOOST_LIMIT;

static void update_headphones_vol(int l, int r)
{
	int val_l, val_r;

	sound_control_write(HEADPHONE_BOOST_L_REG, -headphones_boost_l_ori);
	sound_control_write(HEADPHONE_BOOST_MIX_L_REG, -headphones_boost_l_ori);
	sound_control_write(HEADPHONE_BOOST_R_REG, -headphones_boost_r_ori);
	sound_control_write(HEADPHONE_BOOST_MIX_R_REG, -headphones_boost_r_ori);

	val_l = sound_control_write(HEADPHONE_BOOST_L_REG, l);
	sound_control_write(HEADPHONE_BOOST_MIX_L_REG, l);
	val_r = sound_control_write(HEADPHONE_BOOST_R_REG, r);
	sound_control_write(HEADPHONE_BOOST_MIX_R_REG, r);

	headphones_boost_l_ori = l;
	headphones_boost_r_ori = r;

	dprintk("HEADPHONES L: [%d] R: [%d]  Volume: L: [%d] R: [%d] \n", l, r, val_l, val_r);
}

static void update_speaker_vol(int vol)
{
	int ret;

	sound_control_write(SPEAKER_REG, -speaker_boost_ori);

	ret = sound_control_write(SPEAKER_REG, vol);

	speaker_boost_ori = vol;

	dprintk("SPEAKER MONO: [%d] \
		Volume: MONO: [%d] \n", vol, ret);
}

static void update_mic_vol(int vol)
{
	int ret;

	sound_control_write(MIC_REG, -mic_boost_ori);
	sound_control_write(MIC_MIX_REG, -mic_boost_ori);

	ret = sound_control_write(MIC_REG, vol);
	sound_control_write(MIC_MIX_REG, vol);

	mic_boost_ori = vol;

	dprintk("MIC MONO: [%d] \
		Volume: MONO: [%d] \n", vol, ret);
}

/* misc sysfs*/
static ssize_t headphones_boost_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d\n", 
		headphones_boost_l, headphones_boost_r );
}

static ssize_t headphones_boost_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int val_l, val_r;

	sscanf(buf, "%d %d", &val_l, &val_r);

	if (val_l != headphones_boost_l || val_r != headphones_boost_r) {
		if (val_l >= headphones_boost_limit)
			val_l = headphones_boost_limit;
		if (val_r >= headphones_boost_limit)
			val_r = headphones_boost_limit;

		headphones_boost_l = val_l;
		headphones_boost_r = val_r;
		update_headphones_vol(val_l, val_r);
	}

	return size;
}

static ssize_t speaker_boost_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", speaker_boost);
}

static ssize_t speaker_boost_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != speaker_boost) {
		if (new_val >= speaker_boost_limit)
			new_val = speaker_boost_limit;

		speaker_boost = new_val;
		update_speaker_vol(new_val);
	}

	return size;
}

static ssize_t mic_boost_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mic_boost);
}

static ssize_t mic_boost_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int new_val;

	sscanf(buf, "%d", &new_val);

	if (new_val != mic_boost) {
		if (new_val >= mic_boost_limit)
			new_val = mic_boost_limit;

		speaker_boost = new_val;
		update_mic_vol(new_val);
	}

	return size;
}

static DEVICE_ATTR(volume_boost, 0664, headphones_boost_show,headphones_boost_store);
static DEVICE_ATTR(speaker_boost, 0664, speaker_boost_show, speaker_boost_store);
static DEVICE_ATTR(mic_boost, 0664, mic_boost_show, mic_boost_store);


static struct attribute *soundcontrol_attributes[] =
{
	&dev_attr_volume_boost.attr,
	&dev_attr_speaker_boost.attr,
	&dev_attr_mic_boost.attr,
	NULL
};

static struct attribute_group soundcontrol_group =
{
	.attrs  = soundcontrol_attributes,
};

static struct miscdevice soundcontrol_device =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = "soundcontrol",
};

static int __init soundcontrol_init(void)
{
	int ret;

	pr_info("%s misc_register(%s)\n", __FUNCTION__,
		soundcontrol_device.name);

	ret = misc_register(&soundcontrol_device);

	if (ret) {
		pr_err("%s misc_register(%s) fail\n", __FUNCTION__,
			soundcontrol_device.name);
		return -EINVAL;
	}

	if (sysfs_create_group(&soundcontrol_device.this_device->kobj,
			&soundcontrol_group) < 0) {
		pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
		pr_err("Failed to create sysfs group for device (%s)!\n",
			soundcontrol_device.name);
	}

	return 0;
}
late_initcall(soundcontrol_init);
