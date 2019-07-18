#! /vendor/bin/sh

exec > /dev/kmsg 2>&1

if [ ! -f /sbin/recovery ]; then
  # Hook up to existing init.qcom.post_boot.sh
  while [ ! -f /vendor/bin/init.qcom.post_boot.sh ]; do
    sleep 1
  done
  if ! mount | grep -q /vendor/bin/init.qcom.post_boot.sh; then
    # Replace msm_irqbalance.conf
    echo "PRIO=1,1,1,1,0,0,0,0
# arch_timer,arch_mem_timer,arm-pmu,kgsl-3d0,glink_lpass
IGNORED_IRQ=19,38,21,332,188" > /dev/msm_irqbalance.conf
    chmod 644 /dev/msm_irqbalance.conf
    mount --bind /dev/msm_irqbalance.conf /vendor/etc/msm_irqbalance.conf
    chcon "u:object_r:vendor_configs_file:s0" /vendor/etc/msm_irqbalance.conf
    killall msm_irqbalance

    mount --bind "$0" /vendor/bin/init.qcom.post_boot.sh
    chcon "u:object_r:qti_init_shell_exec:s0" /vendor/bin/init.qcom.post_boot.sh
    exit
  fi
fi

# Setup readahead
find /sys/devices -name read_ahead_kb | while read node; do echo 128 > $node; done

# Core control parameters for gold
echo 2 > /sys/devices/system/cpu/cpu4/core_ctl/min_cpus
echo 60 > /sys/devices/system/cpu/cpu4/core_ctl/busy_up_thres
echo 30 > /sys/devices/system/cpu/cpu4/core_ctl/busy_down_thres
echo 100 > /sys/devices/system/cpu/cpu4/core_ctl/offline_delay_ms
echo 3 > /sys/devices/system/cpu/cpu4/core_ctl/task_thres

# Core control parameters for gold+
echo 0 > /sys/devices/system/cpu/cpu7/core_ctl/min_cpus
echo 60 > /sys/devices/system/cpu/cpu7/core_ctl/busy_up_thres
echo 30 > /sys/devices/system/cpu/cpu7/core_ctl/busy_down_thres
echo 100 > /sys/devices/system/cpu/cpu7/core_ctl/offline_delay_ms
echo 1 > /sys/devices/system/cpu/cpu7/core_ctl/task_thres
# Controls how many more tasks should be eligible to run on gold CPUs
# w.r.t number of gold CPUs available to trigger assist (max number of
# tasks eligible to run on previous cluster minus number of CPUs in
# the previous cluster).
#
# Setting to 1 by default which means there should be at least
# 4 tasks eligible to run on gold cluster (tasks running on gold cores
# plus misfit tasks on silver cores) to trigger assitance from gold+.
echo 1 > /sys/devices/system/cpu/cpu7/core_ctl/nr_prev_assist_thresh

# Disable Core control on silver
echo 0 > /sys/devices/system/cpu/cpu0/core_ctl/enable

# Setting b.L scheduler parameters
echo 95 95 > /proc/sys/kernel/sched_upmigrate
echo 85 85 > /proc/sys/kernel/sched_downmigrate
echo 100 > /proc/sys/kernel/sched_group_upmigrate
echo 10 > /proc/sys/kernel/sched_group_downmigrate
echo 1 > /proc/sys/kernel/sched_walt_rotate_big_tasks

# cpuset parameters
echo 0-3 > /dev/cpuset/background/cpus
echo 0-3 > /dev/cpuset/system-background/cpus

# configure governor settings for silver cluster
echo "schedutil" > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor
echo 0 > /sys/devices/system/cpu/cpufreq/policy0/schedutil/up_rate_limit_us
echo 0 > /sys/devices/system/cpu/cpufreq/policy0/schedutil/down_rate_limit_us
echo 1209600 > /sys/devices/system/cpu/cpufreq/policy0/schedutil/hispeed_freq
echo 576000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq
echo 1 > /sys/devices/system/cpu/cpufreq/policy0/schedutil/pl

# configure governor settings for gold cluster
echo "schedutil" > /sys/devices/system/cpu/cpufreq/policy4/scaling_governor
echo 0 > /sys/devices/system/cpu/cpufreq/policy4/schedutil/up_rate_limit_us
echo 0 > /sys/devices/system/cpu/cpufreq/policy4/schedutil/down_rate_limit_us
echo 1612800 > /sys/devices/system/cpu/cpufreq/policy4/schedutil/hispeed_freq
echo 1 > /sys/devices/system/cpu/cpufreq/policy4/schedutil/pl

# configure governor settings for gold+ cluster
echo "schedutil" > /sys/devices/system/cpu/cpufreq/policy7/scaling_governor
echo 0 > /sys/devices/system/cpu/cpufreq/policy7/schedutil/up_rate_limit_us
echo 0 > /sys/devices/system/cpu/cpufreq/policy7/schedutil/down_rate_limit_us
echo 1612800 > /sys/devices/system/cpu/cpufreq/policy7/schedutil/hispeed_freq
echo 1 > /sys/devices/system/cpu/cpufreq/policy7/schedutil/pl

# configure input boost settings
echo "0:1324800" > /sys/module/cpu_boost/parameters/input_boost_freq
echo 120 > /sys/module/cpu_boost/parameters/input_boost_ms

# Disable wsf, beacause we are using efk.
# wsf Range : 1..1000 So set to bare minimum value 1.
echo 1 > /proc/sys/vm/watermark_scale_factor

echo 0-3 > /dev/cpuset/background/cpus
echo 0-3 > /dev/cpuset/system-background/cpus

# Enable bus-dcvs
for device in /sys/devices/platform/soc
do
    for cpubw in $device/*cpu-cpu-llcc-bw/devfreq/*cpu-cpu-llcc-bw
    do
	echo "bw_hwmon" > $cpubw/governor
	echo 40 > $cpubw/polling_interval
	echo "2288 4577 7110 9155 12298 14236 15258" > $cpubw/bw_hwmon/mbps_zones
	echo 4 > $cpubw/bw_hwmon/sample_ms
	echo 50 > $cpubw/bw_hwmon/io_percent
	echo 20 > $cpubw/bw_hwmon/hist_memory
	echo 10 > $cpubw/bw_hwmon/hyst_length
	echo 30 > $cpubw/bw_hwmon/down_thres
	echo 0 > $cpubw/bw_hwmon/guard_band_mbps
	echo 250 > $cpubw/bw_hwmon/up_scale
	echo 1600 > $cpubw/bw_hwmon/idle_mbps
	echo 14236 > $cpubw/max_freq
    done

    for llccbw in $device/*cpu-llcc-ddr-bw/devfreq/*cpu-llcc-ddr-bw
    do
	echo "bw_hwmon" > $llccbw/governor
	echo 40 > $llccbw/polling_interval
	echo "1720 2929 3879 5931 6881 7980" > $llccbw/bw_hwmon/mbps_zones
	echo 4 > $llccbw/bw_hwmon/sample_ms
	echo 80 > $llccbw/bw_hwmon/io_percent
	echo 20 > $llccbw/bw_hwmon/hist_memory
	echo 10 > $llccbw/bw_hwmon/hyst_length
	echo 30 > $llccbw/bw_hwmon/down_thres
	echo 0 > $llccbw/bw_hwmon/guard_band_mbps
	echo 250 > $llccbw/bw_hwmon/up_scale
	echo 1600 > $llccbw/bw_hwmon/idle_mbps
	echo 6881 > $llccbw/max_freq
    done

    for npubw in $device/*npu-npu-ddr-bw/devfreq/*npu-npu-ddr-bw
    do
	echo 1 > /sys/devices/virtual/npu/msm_npu/pwr
	echo "bw_hwmon" > $npubw/governor
	echo 40 > $npubw/polling_interval
	echo "1720 2929 3879 5931 6881 7980" > $npubw/bw_hwmon/mbps_zones
	echo 4 > $npubw/bw_hwmon/sample_ms
	echo 80 > $npubw/bw_hwmon/io_percent
	echo 20 > $npubw/bw_hwmon/hist_memory
	echo 6  > $npubw/bw_hwmon/hyst_length
	echo 30 > $npubw/bw_hwmon/down_thres
	echo 0 > $npubw/bw_hwmon/guard_band_mbps
	echo 250 > $npubw/bw_hwmon/up_scale
	echo 0 > $npubw/bw_hwmon/idle_mbps
	echo 0 > /sys/devices/virtual/npu/msm_npu/pwr
    done

    #Enable mem_latency governor for L3, LLCC, and DDR scaling
    for memlat in $device/*cpu*-lat/devfreq/*cpu*-lat
    do
	echo "mem_latency" > $memlat/governor
	echo 10 > $memlat/polling_interval
	echo 400 > $memlat/mem_latency/ratio_ceil
    done

    #Enable userspace governor for L3 cdsp nodes
    for l3cdsp in $device/*cdsp-cdsp-l3-lat/devfreq/*cdsp-cdsp-l3-lat
    do
	echo "cdspl3" > $l3cdsp/governor
    done

    #Enable compute governor for gold latfloor
    for latfloor in $device/*cpu-ddr-latfloor*/devfreq/*cpu-ddr-latfloor*
    do
	echo "compute" > $latfloor/governor
	echo 10 > $latfloor/polling_interval
    done

    #Gold L3 ratio ceil
    for l3gold in $device/*cpu4-cpu-l3-lat/devfreq/*cpu4-cpu-l3-lat
    do
	echo 4000 > $l3gold/mem_latency/ratio_ceil
    done

    #Prime L3 ratio ceil
    for l3prime in $device/*cpu7-cpu-l3-lat/devfreq/*cpu7-cpu-l3-lat
    do
	echo 20000 > $l3prime/mem_latency/ratio_ceil
    done
done

# Turn off scheduler boost at the end
echo 0 > /proc/sys/kernel/sched_boost

echo 0 > /sys/module/lpm_levels/parameters/sleep_disabled

# Post-setup services
setprop vendor.post_boot.parsed 1

# Let kernel know our image version/variant/crm_version
if [ -f /sys/devices/soc0/select_image ]; then
    image_version="10:"
    image_version+=`getprop ro.build.id`
    image_version+=":"
    image_version+=`getprop ro.build.version.incremental`
    image_variant=`getprop ro.product.name`
    image_variant+="-"
    image_variant+=`getprop ro.build.type`
    oem_version=`getprop ro.build.version.codename`
    echo 10 > /sys/devices/soc0/select_image
    echo $image_version > /sys/devices/soc0/image_version
    echo $image_variant > /sys/devices/soc0/image_variant
    echo $oem_version > /sys/devices/soc0/image_crm_version
fi

# Parse misc partition path and set property
misc_link=$(ls -l /dev/block/bootdevice/by-name/misc)
real_path=${misc_link##*>}
setprop persist.vendor.mmi.misc_dev_path $real_path
