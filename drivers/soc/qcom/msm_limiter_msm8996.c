/*
 * MSM CPU Frequency Limiter Driver
 *
 * Copyright (c) 2013-2014, Dorimanx <yuri@bynet.co.il>
 * Copyright (c) 2013-2016, Pranav Vashi <neobuddy89@gmail.com>
 * Copyright (c) 2013-2016, JZ Shminer <a332574643@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/state_notifier.h>

#include <soc/qcom/limiter.h>

/* Try not to change below values. */
#define MSM_LIMITER			"msm_limiter"
#define MSM_LIMITER_MAJOR		1
#define MSM_LIMITER_MINOR		0

/* Recommended to set below values from userspace. */
#define FREQ_CONTROL			1
#define DEBUG_MODE			0

 #define LITTLE_CORE 2
 #define BIG_CORE 2

/*
 * Define SOC freq limits below.
 * NOTE: We do not set min freq on resume because it will
 * conflict CPU boost driver on resume. Changing resume_max_freq
 * will reflect new max freq. Changing suspend_min_freq will reflect
 * new min freq. All frequency changes do require freq_control enabled.
 * Changing scaling_governor will reflect new governor.
 * Passing single value to above parameters will apply that value to 
 * all the CPUs present. Otherwise, you can pass value in token:value
 * pair to apply value individually.
 */

#if defined(CONFIG_ARCH_MSM8996)
#define DEFAULT_SUSP_MAX_FREQUENCY_C0	960000
#define DEFAULT_SUSP_MAX_FREQUENCY_C1	1190400
#endif
#if defined(CONFIG_ARCH_MSM8996)
#define DEFAULT_RESUME_MAX_FREQUENCY_C0 1593600
#define DEFAULT_RESUME_MAX_FREQUENCY_C1 2150400
#endif
#if defined(CONFIG_ARCH_MSM8996)
#define DEFAULT_MIN_FREQUENCY_C0 307200
#define DEFAULT_MIN_FREQUENCY_C1 307200
#endif

static struct notifier_block notif;
static unsigned int freq_control = FREQ_CONTROL;
static unsigned int debug_mask = DEBUG_MODE;
static DEFINE_PER_CPU(struct mutex, msm_limiter_mutex);

struct cpu_limit {
	unsigned int suspend_max_freq_c0;
	unsigned int suspend_max_freq_c1;
	unsigned int resume_max_freq_c0;
	unsigned int resume_max_freq_c1;
	unsigned int suspend_min_freq_c0;
	unsigned int suspend_min_freq_c1;
} limit = {
	.suspend_max_freq_c0 = DEFAULT_SUSP_MAX_FREQUENCY_C0,
	.suspend_max_freq_c1 = DEFAULT_SUSP_MAX_FREQUENCY_C1,
	.resume_max_freq_c0 = DEFAULT_RESUME_MAX_FREQUENCY_C0,
	.resume_max_freq_c1 = DEFAULT_RESUME_MAX_FREQUENCY_C1,
	.suspend_min_freq_c0 = DEFAULT_MIN_FREQUENCY_C0,
	.suspend_min_freq_c1 = DEFAULT_MIN_FREQUENCY_C1,
};

#define dprintk(msg...)		\
do { 				\
	if (debug_mask)		\
		pr_info(msg);	\
} while (0)

static void update_cpu_freq(unsigned int cluster, uint32_t max, uint32_t min)
{
	unsigned int cpu, cnt = 0, ctl_cpu;

	if (!max || !min)
		return;

	if (cluster != 0) {
		cnt = LITTLE_CORE;
		ctl_cpu = NR_CPUS;
	} 
	else
		ctl_cpu = LITTLE_CORE;

	
	dprintk("%s: Setting Max Freq for cluster%u: MAX:%u Hz, MIN:%u Hz\n",
			MSM_LIMITER, cluster, max, min);
	for (cpu = cnt; cpu < ctl_cpu; cpu++){
		mutex_lock(&per_cpu(msm_limiter_mutex, cpu));
		cpufreq_set_freq(max, min, cpu);
		mutex_unlock(&per_cpu(msm_limiter_mutex, cpu));
	}
}

static void msm_limiter_run(void)
{
	uint32_t max_freq_c0, max_freq_c1, min_freq_c0, min_freq_c1; 

	if (state_suspended){
		max_freq_c0 = limit.suspend_max_freq_c0;
		max_freq_c1 = limit.suspend_max_freq_c1;
		min_freq_c0 = DEFAULT_MIN_FREQUENCY_C0;
		min_freq_c1 = DEFAULT_MIN_FREQUENCY_C1;
	} else {
		max_freq_c0 = limit.resume_max_freq_c0;
		max_freq_c1 = limit.resume_max_freq_c1;
		min_freq_c0 = limit.suspend_min_freq_c0;
		min_freq_c1 = limit.suspend_min_freq_c1;
	}
	update_cpu_freq(0, max_freq_c0, min_freq_c0);
	update_cpu_freq(1, max_freq_c1, min_freq_c1);
}

static int state_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	if (!freq_control)
		return NOTIFY_OK;

	switch (event) {
		case STATE_NOTIFIER_ACTIVE:
		case STATE_NOTIFIER_SUSPEND:
			msm_limiter_run();
			break;
		default:
			break;
	}

	return NOTIFY_OK;
}

static int msm_limiter_start(void)
{
	unsigned int cpu = 0;
	int ret = 0;

	notif.notifier_call = state_notifier_callback;
	if (state_register_client(&notif)) {
		pr_err("%s: Failed to register State notifier callback\n",
			MSM_LIMITER);
		goto err_out;
	}

	for_each_possible_cpu(cpu)
		mutex_init(&per_cpu(msm_limiter_mutex, cpu));

	msm_limiter_run();

	return ret;
err_out:
	freq_control = 0;
	return ret;
}

static void msm_limiter_stop(void)
{
	unsigned int cpu = 0;

	for_each_possible_cpu(cpu)	
		mutex_destroy(&per_cpu(msm_limiter_mutex, cpu));

	state_unregister_client(&notif);
	notif.notifier_call = NULL;
}

static ssize_t freq_control_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", freq_control);
}

static ssize_t freq_control_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u\n", &val);
	if (ret != 1 || val < 0 || val > 1)
		return -EINVAL;

	if (val == freq_control)
		return count;

	freq_control = val;

	if (freq_control)
		msm_limiter_start();
	else
		msm_limiter_stop();

	return count;
}

static ssize_t debug_mask_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", debug_mask);
}

static ssize_t debug_mask_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = sscanf(buf, "%u\n", &val);
	if (ret != 1 || val < 0 || val > 1)
		return -EINVAL;

	if (val == debug_mask)
		return count;

	debug_mask = val;

	return count;
}

static ssize_t set_resume_max_freq_c0(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
			return -EINVAL;
	limit.resume_max_freq_c0 =
		max(val, limit.suspend_min_freq_c0);

	if (freq_control)
		msm_limiter_run();
	return count;

}

static ssize_t get_resume_max_freq_c0(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.resume_max_freq_c0);
}

static ssize_t set_resume_max_freq_c1(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
			return -EINVAL;
	limit.resume_max_freq_c1 =
		max(val, limit.suspend_min_freq_c1);

	if (freq_control)
		msm_limiter_run();
	return count;
}

static ssize_t get_resume_max_freq_c1(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.resume_max_freq_c1);
}

static struct kobj_attribute resume_max_freq_c0 =
	__ATTR(resume_max_freq_c0, 0644,
		get_resume_max_freq_c0,
		set_resume_max_freq_c0);

static struct kobj_attribute resume_max_freq_c1 =
	__ATTR(resume_max_freq_c1, 0644,
		get_resume_max_freq_c1,
		set_resume_max_freq_c1);

static ssize_t set_suspend_max_freq_c0(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
			return -EINVAL;
	limit.suspend_max_freq_c0 =
		max(val, limit.suspend_min_freq_c0);

	if (freq_control)
		msm_limiter_run();
	return count;
}

static ssize_t get_suspend_max_freq_c0(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.suspend_max_freq_c0);
}

static ssize_t set_suspend_max_freq_c1(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
			return -EINVAL;
	limit.suspend_max_freq_c1 =
		max(val, limit.suspend_min_freq_c1);

	if (freq_control)
		msm_limiter_run();
	return count;
}

static ssize_t get_suspend_max_freq_c1(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.suspend_max_freq_c1);
}

static struct kobj_attribute suspend_max_freq_c0 =
	__ATTR(suspend_max_freq_c0, 0644,
		get_suspend_max_freq_c0,
		set_suspend_max_freq_c0);

static struct kobj_attribute suspend_max_freq_c1 =
	__ATTR(suspend_max_freq_c1, 0644,
		get_suspend_max_freq_c1,
		set_suspend_max_freq_c1);

static ssize_t set_suspend_min_freq_c0(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
			return -EINVAL;
	limit.suspend_min_freq_c0 =
		min(val, limit.resume_max_freq_c0);

	if (freq_control)
		msm_limiter_run();
	return count;
}

static ssize_t get_suspend_min_freq_c0(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.suspend_min_freq_c0);
}

static ssize_t set_suspend_min_freq_c1(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
			return -EINVAL;
	limit.suspend_min_freq_c1 =
		min(val, limit.resume_max_freq_c1);

	if (freq_control)
		msm_limiter_run();
	return count;
}

static ssize_t get_suspend_min_freq_c1(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", limit.suspend_min_freq_c1);
}

static struct kobj_attribute suspend_min_freq_c0 =
	__ATTR(suspend_min_freq_c0, 0644,
		get_suspend_min_freq_c0,
		set_suspend_min_freq_c0);

static struct kobj_attribute suspend_min_freq_c1 =
	__ATTR(suspend_min_freq_c1, 0644,
		get_suspend_min_freq_c1,
		set_suspend_min_freq_c1);

static ssize_t set_scaling_governor_c0(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int i;
	char val[16];

	if (sscanf(buf, "%s\n", val) != 1)
		return -EINVAL;
		for (i = 0; i < LITTLE_CORE; i++)
		cpufreq_set_gov(val, i);

		return count;
}

static ssize_t get_scaling_governor_c0(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", cpufreq_get_gov(0));
}

static ssize_t set_scaling_governor_c1(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int i;
	char val[16];

	if (sscanf(buf, "%s\n", val) != 1)
		return -EINVAL;

	for (i = LITTLE_CORE; i < NR_CPUS; i++)
		cpufreq_set_gov(val, i);

		return count;
}

static ssize_t get_scaling_governor_c1(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", cpufreq_get_gov(LITTLE_CORE));
}

static struct kobj_attribute scaling_governor_c0 =
	__ATTR(scaling_governor_c0, 0644,
		get_scaling_governor_c0,
		set_scaling_governor_c0);

static struct kobj_attribute scaling_governor_c1 =
	__ATTR(scaling_governor_c1, 0644,
		get_scaling_governor_c1,
		set_scaling_governor_c1);

static ssize_t msm_limiter_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %u.%u\n",
			MSM_LIMITER_MAJOR, MSM_LIMITER_MINOR);
}

static struct kobj_attribute msm_limiter_version =
	__ATTR(msm_limiter_version, 0444,
		msm_limiter_version_show,
		NULL);

static struct kobj_attribute freq_control_attr =
	__ATTR(freq_control, 0644,
		freq_control_show,
		freq_control_store);

static struct kobj_attribute debug_mask_attr =
	__ATTR(debug_mask, 0644,
		debug_mask_show,
		debug_mask_store);

static struct attribute *msm_limiter_attrs[] =
	{
		&freq_control_attr.attr,
		&debug_mask_attr.attr,
		&suspend_max_freq_c0.attr,
		&suspend_max_freq_c1.attr,
		&resume_max_freq_c0.attr,
		&resume_max_freq_c1.attr,
		&suspend_min_freq_c0.attr,
		&suspend_min_freq_c1.attr,
		&scaling_governor_c0.attr,
		&scaling_governor_c1.attr,
		&msm_limiter_version.attr,
		NULL,
	};

static struct attribute_group msm_limiter_attr_group =
	{
		.attrs = msm_limiter_attrs,
	};

static struct kobject *msm_limiter_kobj;

static int msm_limiter_init(void)
{
	int ret;

	msm_limiter_kobj =
		kobject_create_and_add(MSM_LIMITER, kernel_kobj);
	if (!msm_limiter_kobj) {
		pr_err("%s: kobject create failed!\n",
			MSM_LIMITER);
		return -ENOMEM;
        }

	ret = sysfs_create_group(msm_limiter_kobj,
			&msm_limiter_attr_group);
        if (ret) {
		pr_err("%s: sysfs create failed!\n",
			MSM_LIMITER);
		goto err_dev;
	}

	if (freq_control)
		msm_limiter_start();

	return ret;
err_dev:
	if (msm_limiter_kobj != NULL)
		kobject_put(msm_limiter_kobj);
	return ret;
}

static void msm_limiter_exit(void)
{
	if (msm_limiter_kobj != NULL)
		kobject_put(msm_limiter_kobj);

	if (freq_control)
		msm_limiter_stop();

}

late_initcall(msm_limiter_init);
module_exit(msm_limiter_exit);

MODULE_AUTHOR("Dorimanx <yuri@bynet.co.il>");
MODULE_AUTHOR("Pranav Vashi <neobuddy89@gmail.com>");
MODULE_AUTHOR("JZ Shminer <a332574643@gmail.com>");
MODULE_DESCRIPTION("MSM CPU Frequency Limiter Driver");
MODULE_LICENSE("GPL v2");