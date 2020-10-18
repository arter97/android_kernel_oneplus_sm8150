#! /vendor/bin/sh

exec > /dev/kmsg 2>&1

if ! grep -v '#' /vendor/etc/fstab.qcom | grep -q f2fs; then
  # ECD18g== is the f2fs magic code under little-endian
  if [[ $(dd if=/dev/block/platform/soc/1d84000.ufshc/by-name/userdata bs=4 skip=256 count=1 2>/dev/null | base64) == "ECD18g==" ]]; then
    # fstab is missing entry for f2fs, add one
    sed -e "s@/dev/block/bootdevice/by-name/userdata.*@$(cat /vendor/etc/fstab.qcom | grep ext4 | grep /data | grep -v '#' | while read a b c d e; do echo $a $b f2fs noatime,nosuid,nodev,discard,fsync_mode=nobarrier latemount,wait,check,encryptable=ice,wrappedkey,keydirectory=/metadata/vold/metadata_encryption,quota,formattable,reservedsize=128M; done)@g" /vendor/etc/fstab.qcom | uniq > /dev/fstab.qcom
    chmod 644 /dev/fstab.qcom
    mount --bind /dev/fstab.qcom /vendor/etc/fstab.qcom
    chcon u:object_r:vendor_configs_file:s0 /vendor/etc/fstab.qcom
    cat /dev/fstab.qcom | while read a; do echo $a; done
    echo "Patched /vendor/etc/fstab.qcom for f2fs"
  fi
fi

if ! mount | grep -q /vendor/bin/init.qcom.post_boot.sh && [ ! -f /dev/ep/execprog ]; then
  # Run under a new tmpfs to avoid /dev selabel
  mkdir /dev/ep
  mount -t tmpfs nodev /dev/ep
  cp -p "$0" /dev/ep/execprog
  rm "$0"
  chown root:shell /dev/ep/execprog
  exec /dev/ep/execprog
fi

if ! mount | grep -q /vendor/bin/init.qcom.post_boot.sh && [ ! -f /sbin/recovery ] && [ ! -f /dev/ep/.post_boot ]; then
  # Run once
  touch /dev/ep/.post_boot

  # Disable Houston and cc_ctl
  mount --bind /dev/ep/.post_boot /system/priv-app/Houston/Houston.apk
  mount --bind /dev/ep/.post_boot /system/priv-app/OPAppCategoryProvider/OPAppCategoryProvider.apk

  # Setup binaries
  RESETPROPSIZE=47297
  tail -c $RESETPROPSIZE "$0" > /dev/ep/resetprop
  echo SIZE: $(($(stat -c%s "$0") - $RESETPROPSIZE))
  head -c $(($(stat -c%s "$0") - $RESETPROPSIZE)) "$0" >> "$0".tmp
  mv "$0".tmp "$0"
  chmod 755 "$0"
  chmod 755 /dev/ep/resetprop

  # Setup swap
  while [ ! -e /dev/block/vbswap0 ]; do
    sleep 1
  done
  if ! grep -q vbswap /proc/swaps; then
    # 4GB
    echo 4294967296 > /sys/devices/virtual/block/vbswap0/disksize

    # Set swappiness reflecting the device's RAM size
    RamStr=$(cat /proc/meminfo | grep MemTotal)
    RamMB=$((${RamStr:16:8} / 1024))
    if [ $RamMB -le 6144 ]; then
        echo 190 > /proc/sys/vm/rswappiness
    elif [ $RamMB -le 8192 ]; then
        echo 160 > /proc/sys/vm/rswappiness
    else
        echo 130 > /proc/sys/vm/rswappiness
    fi

    mkswap /dev/block/vbswap0
    swapon /dev/block/vbswap0
  fi

  # Disable OP_SLA network boosts
  /dev/ep/resetprop persist.dynamic.OP_FEATURE_OPSLA 0

  # Google Camera AUX mod
  /dev/ep/resetprop vendor.camera.aux.packagelist com.google.android.GoogleCamera,org.codeaurora.snapcam,com.oneplus.camera

  rm /dev/ep/resetprop

  # Hook up to existing init.qcom.post_boot.sh
  # Kill OnePlus brain service by replacing it with ill-labeled file
  mount --bind /dev/ep/.post_boot /vendor/bin/hw/vendor.oneplus.hardware.brain@1.0-service
  killall -9 vendor.oneplus.hardware.brain@1.0-service
  # Replace msm_irqbalance.conf
  echo "PRIO=1,1,1,1,0,0,0,0
# arch_timer,arch_mem_timer,arm-pmu,kgsl-3d0,glink_lpass
IGNORED_IRQ=19,38,21,332,188" > /dev/ep/msm_irqbalance.conf
  chmod 644 /dev/ep/msm_irqbalance.conf
  mount --bind /dev/ep/msm_irqbalance.conf /vendor/etc/msm_irqbalance.conf
  chcon "u:object_r:vendor_configs_file:s0" /vendor/etc/msm_irqbalance.conf
  killall msm_irqbalance

  mount --bind "$0" /vendor/bin/init.qcom.post_boot.sh
  chcon "u:object_r:qti_init_shell_exec:s0" /vendor/bin/init.qcom.post_boot.sh

  # lazy unmount /dev/ep for invisibility
  umount -l /dev/ep

  echo "97" > /sys/fs/selinux/enforce

  exit
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
echo 0-6 > /dev/cpuset/foreground/cpus
echo 0-3 > /dev/cpuset/display/cpus

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

# Stock LMK settings
echo "18432,23040,27648,51256,150296,200640" > /sys/module/lowmemorykiller/parameters/minfree

# Remove unused swapfile
rm -f /data/vendor/swap/swapfile 2>/dev/null
sync

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

exit 0

# Binary will be appended afterwards
ELF          ·           @       @²          @ 8 	 @         @       @       @       ø      ø                   8      8      8                                                         <¦      <¦                   Xª      Xª     Xª     ¸      x                   Ð«      Ð«     Ð«                                P      P      P      ˜       ˜              Påtd   l‰      l‰      l‰      T      T             Qåtd                                                  Råtd   Xª      Xª     Xª     ¨      ¨             /system/bin/linker64       „      Android    r20                                                             5594570                                                            <         ±D € ¨A@@<   B   G   (ñ© ¯4èBEÕìºã’|šdþ×_W¾ÚWÒ©,rØqXjš|µÓþvT<”¤·f›Yï—y                                                       ˆª                                  |                     ‚                     a                     c                      Ê                     ×                     9                     Â                     Õ                      ó                      u                     —                     !                       "                   Ð                     ¡                     É                                          &                     Ü                     /  "                                        ®                     ò  "                   Î                                           h                     '                     º                     ³                                           ‹                                           ã                      o                     å                     W                     Þ                     *                      Û                      D  "                                         "                     ë                                                                                     .                     j                      S                      ¨                     ¶                      }                     3                     ú                      Š                          «     P       g   ñÿÐ¶             E   ñÿ°             z   ñÿÐ¶             ¿    Xª            Ñ    xª            f   ñÿÐ¶             “    hª            L   ñÿ°             º    4      ô       à    ˆª     P       X   ñÿ°             ¢    x´            r   ñÿÐ¶              libc++.so        _Znwm __cxa_begin_catch _ZTVN10__cxxabiv120__si_class_type_infoE _ZSt9terminatev _ZdlPv _ZTVN10__cxxabiv117__class_type_infoE __gxx_personality_v0 libm.so libdl.so dl_iterate_phdr libc.so fwrite mkdir __errno strndup strlcpy strcmp strdup socket mmap connect pthread_once recv isspace pthread_mutex_unlock pthread_mutex_lock fsetxattr writev strcpy abort malloc fopen getline fstat memcpy ftruncate access atoll poll memset syscall strchr fclose strlen free snprintf __cxa_atexit pthread_create stderr munmap __register_atfork send strncmp puts prctl __libc_init _edata __bss_start __bss_start__ __bss_end__ __end__ _end _ZTV13ContextsSplit __INIT_ARRAY__ _ZN9prop_area8pa_size_E main __PREINIT_ARRAY__ __FINI_ARRAY__ _ZTV18ContextsSerialized LIBC                                                                                             ­          c    ù        Æ          c    ù      ª           Øª     ˜ª            +       ª           $+      ¨ª            (      °ª           )      ¸ª           H+      Àª           ˆ)      Èª           *      Ðª           \*      àª           _‡      èª           ðª     øª           t‡      «           P«     «           ô2      «           ø2       «           ˜0      («           l1      0«           ü2      8«           à1      @«           42      H«           d2      X«           ˆ      `«           ðª     p«           ¸«     x«           ô2      €«           ÜM      ˆ«           àM      «           N      ˜«           N       «           $N      ¨«           ,N      °«           0N      À«           žˆ      È«           ðª     ¸¯            «     È¯           hª     Ð¯           x´     Ø¯           4      à¯           Xª     è¯           xª     ø¯           ˆª     Øª       *          P«       *          ¸«       *          ðª       4          À¯                  ð¯       1            °       "           ®                  ®                  ®                   ®                  (®                  0®                  8®       	           @®       
           H®                  P®                  X®                  `®                  h®                  p®                  x®                  €®                  ˆ®                  ®                  ˜®                   ®                  ¨®                  °®                  ¸®                  À®                  È®                  Ð®                  Ø®                  à®                  è®                   ð®       !           ø®       "            ¯       #           ¯       $           ¯       %           ¯       &            ¯       '           (¯       (           0¯       )           8¯       +           @¯       ,           H¯       -           P¯       .           X¯       /           `¯       0           h¯       2           p¯       3           x¯       5           €¯       6           ˆ¯       7           ¯       8           ˜¯       9            ¯       :           ¨¯       ;           ð{¿©Ð  °Gù8‘ Ö Õ Õ ÕÐ  °Gù"8‘ ÖÐ  °
GùB8‘ ÖÐ  °Gùb8‘ ÖÐ  °Gù‚8‘ ÖÐ  °Gù¢8‘ ÖÐ  °GùÂ8‘ ÖÐ  °Gùâ8‘ ÖÐ  °"Gù9‘ ÖÐ  °&Gù"9‘ ÖÐ  °*GùB9‘ ÖÐ  °.Gùb9‘ ÖÐ  °2Gù‚9‘ ÖÐ  °6Gù¢9‘ ÖÐ  °:GùÂ9‘ ÖÐ  °>Gùâ9‘ ÖÐ  °BGù:‘ ÖÐ  °FGù":‘ ÖÐ  °JGùB:‘ ÖÐ  °NGùb:‘ ÖÐ  °RGù‚:‘ ÖÐ  °VGù¢:‘ ÖÐ  °ZGùÂ:‘ ÖÐ  °^Gùâ:‘ ÖÐ  °bGù;‘ ÖÐ  °fGù";‘ ÖÐ  °jGùB;‘ ÖÐ  °nGùb;‘ ÖÐ  °rGù‚;‘ ÖÐ  °vGù¢;‘ ÖÐ  °zGùÂ;‘ ÖÐ  °~Gùâ;‘ ÖÐ  °‚Gù<‘ ÖÐ  °†Gù"<‘ ÖÐ  °ŠGùB<‘ ÖÐ  °ŽGùb<‘ ÖÐ  °’Gù‚<‘ ÖÐ  °–Gù¢<‘ ÖÐ  °šGùÂ<‘ ÖÐ  °žGùâ<‘ ÖÐ  °¢Gù=‘ ÖÐ  °¦Gù"=‘ ÖÐ  °ªGùB=‘ ÖÐ  °®Gùb=‘ ÖÐ  °²Gù‚=‘ ÖÐ  °¶Gù¢=‘ ÖÐ  °ºGùÂ=‘ ÖÐ  °¾Gùâ=‘ ÖÐ  °ÂGù>‘ ÖÐ  °ÆGù">‘ ÖÐ  °ÊGùB>‘ ÖÐ  °ÎGùb>‘ ÖÐ  °ÒGù‚>‘ ÖÐ  °ÖGù¢>‘ Öà ‘  ÿÃ Ñý{©È  °ñGùÉ  °)åGùÊ  °JõGù gž Nà€=ê ùÂ  °BìGùã ‘áªýƒ ‘5ÿÿ—@  ´  ÖÀ_Ö  A-‘Â  ÐB@ ‘á ªàª›ÿÿÃ  Ðc@ ‘¸ÿÿý{¿©ý ‘À  Ð!  ð € ‘!`‘!
 ”è *  ý{Á¨À_Ö  €À_Öÿƒ Ñý{©ýC ‘À  Ð!  ð € ‘!`‘¢ Ñ¿Ã8i
 ”€  6¨Ã_8      €ý{A©ÿƒ ‘À_ÖÀ  Ð € ‘…
 È  Ð ‘á ªàª
 È  Ð ‘ãªâªá ªàª›
 È  Ð ‘ãªâªá ªàªÚ
 È  Ð ‘âªá ªàª' È  Ð ‘ã*âªá ªàª2 È  Ð ‘ä*ãªâ*á ªàªo È  Ð ‘á ªàª° È  Ð ‘á ªàª
 È  Ð ‘á *àªÖ È  Ð ‘äªãªâ*á ªàªÚ È  Ð ‘á *àª	 È  Ð ‘âªá ªàª õøôO©ý{©ýƒ ‘ôªÿÿ—  5“Ò@©àª­ÿÿ—  ´õ ªàªÎþÿ—â ªàªáª¾ÿÿ—     ð P‘áªÿÿ—àªÃþÿ—õ ªàªÀþÿ—ã ªàªá*âª¶ÿÿ—õ *   ð#  ð ¸‘cL‘áªâªÿÿ—•  4   ð À‘'ÿÿ—ó*à*ý{B©ôOA©õCøÀ_ÖÈ  °ùGù   ð °‘á€R@ùâ 2ó 2Äþÿ—òÿÿÿÑöW©ôO©ý{©ýÃ‘à	 ´Õ  ÐµÂ‘¨þßˆ)  ð)µ‘?  ñô ª3šˆ 5   ð 4‘á# ‘zÿÿ—ö 2   4à# ‘ÿÿ— qÖ––¶þŸˆ¨þßˆ qÁ Tàª€þÿ—| ñˆ Tàª|þÿ—lñ Tõ# ‘ ä oè 2 ~²â2áªàƒ‡<àÃ†<àÃ…<àÃ„<àÃƒ<àÃ‚<àÃ<àÃ€<è ¹Öþÿ— ’ ‘‚€RáªÒþÿ— £ Ñd  ”³ƒ]¸ 1à Tá# ‘â2à*ã*Tþÿ— ±A Tµþÿ— @¹ qÀþÿT2  àªSþÿ—pñ# T!  ð!$‘â 2àª\þÿ—`  4 €)   £ Ñµ£ ÑF  ”¨ƒ]¸ 1  Tè# ‘* €Ré 2J  rë2A‘à# ‘áªé_ ùê› ¹õ£ ©ë ùéK ùg  ”áªe  ”€  ”` 6 €¨¸ £ Ñ¡Ã Ñ–  ”   6¨]¸ qóŸZ  ³Ã]¸þÿ—  ¹ € £ Ñ¯  ”à*ý{O©ôON©öWM©ÿ‘À_Ö³Ã]¸÷ÿÿ qþÿT³¸¿Cx Ã Ñá 2B€R1þÿ— 1¡  Tjþÿ— @¹ qàþÿTó*çÿÿ      ó ª £ Ñ‘  ”àªŒ ”ÿCÑôO©ý{©ý‘! €Ró ª ¹à 2 râ*ô 2iþÿ— 1` ¹  Tè ‘!  Ð ä o ²!¤‘‚€Ràã…<à­à­à ­Sþÿ—ô y`@¹á ‘â 2þýÿ— 1Á T;þÿ— @¹ qàþÿT`@¹h ¹±ýÿ— €h ¹  1þÿ— @¹h ¹ý{H©ôOG©ÿC‘À_ÖôO¾©ó ªàªý{©ýC ‘ôªÈýÿ—hZ@ùë2j
‹ ‘hZ ù@	¸hF@ùl‹	 ‘Š­ ©iF ùÀ  4i	‹
|@’	 ‘4© ©hF ùý{A©àªôOÂ¨À_Öóøý{©ýC ‘ @ùó ª @¹ 1@ TbŠ@¹a" ‘xýÿ— ±à  Tà 2Z ùF ù	  à*  ûýÿ—è ª@¹i@ùà*( ¹ý{A©óBøÀ_ÖôO¾©ý{©ýC ‘ôªó ª`@¹â2ã2áªõýÿ— ±Á  Tæýÿ— @¹ qÀþÿT   1À  T qA Tà 2 ¹
  Úýÿ— @¹ qh ¹àŸ   €à*h ¹ý{A©ôOÂ¨À_Öý{¿©ý ‘  @¹ 1@  TEýÿ—ý{Á¨À_Ö  ”þø¬ýÿ—ãýÿ—üW½©ôO©ý{©ýƒ ‘ÿÑôªõ*ó ªy  ”h
@ù¨  ´àª¥  ”à 2  c@ùd@ù"  ÐB‘à ‘á2eýÿ— q£  Tàª˜  ”à*  Õ  6a@ùà ‘âªß ”  à ‘X ”`
 ùàª‹  ”h
@ù ñàŸÿ‘ý{B©ôOA©üWÃ¨À_Öóøý{©ýC ‘@ùó ª¨ µhb@9h 5àª  ”À  6àªá*âªÃÿÿ—`  7è 2hb 9h
@ùý{A© ñàŸóBøÀ_Öüøý{©ýC ‘ÿÑ@ù@ù"  ÐB‘à ‘á2)ýÿ—üqi  Tà*  à ‘á2ýÿ—  qàŸÿ‘ý{A©üBøÀ_Öóøý{©ýC ‘ó ªäÿÿ—`  6è*	  `
@ùÀ  ´È  éGù@ùýÿ—
 ùè 2hb 9ý{A©óBøÀ_Öóøý{©ýC ‘ó ª @ùÀ  ´È  éGù@ùöüÿ—
 ùý{A©óBøÀ_Öø_¼©öW©ôO©ý{©ýÃ ‘ó ªè 2iþ_ˆ‰  5h~	ˆ©ÿÿ5   _?Õè2iþ_ˆh~
ˆÊÿÿ5I 4/ýÿ—ô ª5€R6€R÷2  ˜ ¹  h@9˜@¹@€Rã2 qÂ• €áªäªåªõüÿ— 1@þÿThþ_ˆw~	ˆÉÿÿ5þÿ5ý{C©ôOB©öWA©ø_Ä¨À_ÖöW½©ôO©ý{©ýƒ ‘ó ªh~_ˆþ	ˆÉÿÿ5	 qA Th@9 q(€RŸýÿ— @¹ô ª@€Rã 2áªâ*äªåªæ*Ñüÿ— 1   Tý{B©ôOA©öWÃ¨À_Ö– ¹ûÿÿöW½©ôO©ý{©ýƒ ‘@ùó ªâ 2C€R	@¹ €àªåªii¸È
‹ñ}Óáª¬üÿ— ±  Tô ª$  Ð ¨‰R„0‘Àjªrá*âªãªýÿ—tZ©u ùö 4Êö~ÓèªéªJ Ñ  t@ù) ‘¡ ‘m®@©Ž‹_	ël@¹Œ l‹Œii¸Í ùßa 9ß ¹k‹Ëý ©ß 9þÿTà 2  à*ý{B©ôOA©öWÃ¨À_ÖüW½©ôO©ý{©ýƒ ‘ÿÑ@ùôª"  Ðó ªõ*B¨‘à ‘á2_üÿ— qƒ  Tà* ù  õ  6!  Ð!ü‘à ‘âªÙ ”  à ‘R ”  ñ` ùàŸÿ‘ý{B©ôOA©üWÃ¨À_Öóøý{©ýC ‘ó ª @ ‘” ”@ 6àª˜ÿÿ—`  6à 2  h@ùàª@ù ?Öà*ý{A©óBøÀ_Ö÷øöW©ôO©ý{©ýÃ ‘ôªõ*ó ª ùãÿÿ—` 6õ 6`@ù!9€Rúûÿ—T  ´Ÿ 9h@ù( ´õª÷ªö*h@ùá 2âª ‹ þÿ—h@ù	  R÷ ‘Ö	*ÿëµ¢ ‘£þÿTö 7á 2àªâªõ 2¡ÿÿ—  6  àªá*âª›ÿÿ—`  6õ 2  h@ùàª@ù ?Öõ*à*ý{C©ôOB©öWA©÷DøÀ_Öóøý{©ýC ‘ó ª @ù¢ Ñãª§ ”¨Ã_¸ 1  Ti@ù?ë© Ti@ù
€R%
›`Aø  µ
€R %
›á*âªgþÿ—`@ù  àªý{A©óBøÀ_Ö÷øöW©ôO©ý{©ýÃ ‘@ù¨ ´óªô ªõªöª÷ªˆ@ù ‹þÿ—à  6ˆ@ùáªâª‹ 	@ù¢ ”ˆ@ù÷ ‘Ö¢ ‘ÿëCþÿTý{C©ôOB©öWA©÷DøÀ_ÖõøôO©ý{©ýƒ ‘@ùˆ ´ó ªôªõªh@ù ‹”þÿ—h@ùµ ‘”¢ ‘¿ë#ÿÿTý{B©ôOA©õCøÀ_ÖõøôO©ý{©ýƒ ‘ó ª @ ‘- ”`@ù  ´h@ù ´“þÿ—h@ù	 ñc Tô 2€Rh@ù ‹‹þÿ—h@ù” ‘µ¢ ‘Ÿë#ÿÿT`@ùa@ù‚ûÿ— ù`@ùÀ  ´È  éGù@ù{ûÿ— ù ùý{B©ôOA©õCøÀ_Öý{¿©ý ‘È  a*‘ø ”ý{Á¨À_Ö÷ýÿ—óøý{©ýC ‘ó ªóÿÿ—ý{A©àªóBø+ûÿ @ùÀ_ÖüW½©ôO©ý{©ýƒ ‘ÿÑ@ùôª"  Ðó ªõ*B¨‘à ‘á2[ûÿ— qƒ  Tà* ù  õ  6!  Ð!ü‘à ‘âªÕ ”  à ‘N ”  ñ` ùàŸÿ‘ý{B©ôOA©üWÃ¨À_ÖÿÃÑúg©ø_©öW©ôO©ý{©ýƒ‘èª!  Ðô ª!´‘àªìúÿ— 	 ´ó ªàc ‘áC ‘âªÿ ùÿ ©èúÿ—è@ù ñË T6  Ð•" ‘Öú‘â# ‘ã ‘àªC  ” qË T÷@ùa  Tàª)  â2àªáªûÿ—` 4š
@ùø@ù ´ùª @ùáªìúÿ—  49@ùyÿÿµ™@ùà2^ûÿ—÷ ªàª‡ûÿ—àþ ©ÿb 9ÿ ¹ÿ 9ùj©—
 ùá@ùàªâª  àª  àªáªâª€  ”à@ùÁúÿ—à@ù¿úÿ—àc ‘áC ‘âª«úÿ—è@ù  ñìøÿTàª¶úÿ—àªàúÿ—à 2ý{F©ôOE©öWD©ø_C©úgB©ÿÃ‘À_ÖÿƒÑüg©ø_©öW©ôO©ý{©ýC‘ó ªæ­ä­â­à ­æ
©ä	©â©Àúÿ—è`² ‹ý`“ijh8ö ª?) qa  Tj(8  Ö ‘t Ñ—@8à*âúÿ— ÿÿ5õ* 4ÿŽ qÀ Tè€’ðßòê ‘ì`²©C ‘ë‘¨ƒøH‘Š‹õ*©øiÁ ‘Xý`“©£:©¨ƒ›¸¨ ø6	! ? q©ƒ¸Í T¨Zø	! ‘©ø‰Ë	ë€ T@ù? ù–@9  –@8à*¼úÿ—V  4€ÿÿ5à*¸úÿ—6 4  5öªˆ‹@9à*±úÿ—Ö ‘W  4@ÿÿ4àªáªÃúÿ—  ù  ´”‹µ ¿
 qëúÿT  ©ƒZø(‹Üÿÿ €à*ý{Q©ôOP©öWO©ø_N©ügM©ÿƒ‘À_Ö÷øöW©ôO©ý{©ýÃ ‘õ ªàªóªôª^úÿ—¨@ù( ´	@ù? ëÃ Töªõª¨†Aø@9© q  T¨@ùÈ  ´	@ùöª? ë¢þÿT  öªà2­úÿ—×@ùõ ªàªÕúÿ—  ùàªBúÿ— Î ©· ùÕ ùý{C©ôOB©öWA©÷DøÀ_Öóøý{©ýC ‘!  Ð!‘ó ªÿÿ—€ 7   Ð X‘á22úÿ— 1à T!  Ð!X‘àªøþÿ—  6   ° ‘á2'úÿ— 1€ T!  °!‘  !  °!¤‘àªêþÿ—@ 6   ° P‘á2úÿ— 1  T!  °!P‘  à*	  !  °!¸‘  !  °! ‘àª×þÿ—à 2ý{A©óBøÀ_ÖöW½©ôO©ý{©ýƒ ‘ôªõ*ó ª ùÁÿÿ—À 6U 6`@ù!9€RÝùÿ—T  ´Ÿ 9u
@ù• ´ö*á 2àªâª†üÿ—µ@ù  rÖŸ5ÿÿµß rá Tá 2àªâªõ 2þÿ—  6  àªá*âª‡þÿ—`  6õ 2  h@ùàª@ù ?Öõ*ý{B©ôOA©à*öWÃ¨À_ÖôO¾©ý{©ýC ‘@ù” ´óª€@ù @9© q  T‚@ùáªÑùÿ—   4”@ùôþÿµàª	  “
@ù`
@ùÀ  µàªá*âªOüÿ—`
@ùý{A©ôOÂ¨À_ÖõøôO©ý{©ýƒ ‘@ù” ´óªõªàªpüÿ—   6€
@ùáªâª“ ””@ùÿÿµý{B©ôOA©õCøÀ_Öóøý{©ýC ‘@ù³  ´àªŽüÿ—s@ù³ÿÿµý{A©óBøÀ_ÖôO¾©ý{©ýC ‘@ùó ªT ´ˆ@ùh ù€@ùbùÿ—àªXùÿ—t@ù4ÿÿµ	  ˆ@ùh
 ù€@ùYùÿ—àªˆüÿ—àªMùÿ—t
@ùôþÿµ`@ùÀ  ´¨  ðéGù@ù}ùÿ— ùý{A©ôOÂ¨À_Öüÿ—À_Ö>ùÿ @ùÀ_ÖõøôO©ý{©ýƒ ‘õªARôª r‚$€Ræùÿ—àø7ó *Õ ´àªYùÿ—!  ° ‘!à ‘à*âªä*¦ùÿ—”  ´`  4è 2ˆ 9á2à*?ùÿ— ø7È  é2Ê  ë'2á2â 2ã 2àªä*åª	=ùKAùeùÿ— ±@ T(  ° ä o  ­  ­  ­ €= DýhšRÈ¿r ¹ˆ€Rô ª @ ü  ¹à*ùÿ—	  à* ùÿ—  ‚ùÿ— @¹5 qà  Tôªàªý{B©ôOA©õCøÀ_Öhùÿ—ÿƒÑôO©ý{	©ýC‘á ‘ó *©ùÿ—€ø7è@¹H 5è@¹ 5èC@9I€R	j Tá@ù( ñ+ TÔ  É  â 2ã 2àªä*åª>ù(Aù'ùÿ— ±  T@¹	JŠRé	ªr	kÁ  T@¹išRÉ¿r	k€  T>Bùýøÿ—àªý{I©ôOH©ÿƒ‘À_ÖôO¾©ý{©ýC ‘A R rnùÿ— 1  Tó *Çÿÿ—ô ªà*·øÿ—àª  àªý{A©ôOÂ¨À_Ö @¹É  *ABù) ‘)õ~’+‹
ëi  TàªÀ_ÖH  ¹ @¹	  ¹H @¹ ‹ ‘À_Ö÷øöW©ôO©ý{© @¹È  ô*	ABùˆb ‘y~’
‹_	ëýÃ ‘i  Tõª  è ‹  ¹¢¸¶R ‘àªâªóªrøÿ—ßj48w ¹àªý{C©ôOB©öWA©÷DøÀ_Öúg»©ø_©öW©ôO©ý{©ý‘ @¹É  è*)ABù‘‘y~’‹	ëi  Tõª'  
 ‹óªö*ô*÷ªHŸpqU‘  ¹ã TÊ J ‘Jy~’KA(‹	ëèýÿTé*
  ¹ 	‹‘â*àªáª@øÿ—àªáªâ*ã*?K68> ”  àªáªâ*ä* ”z ¹àªý{D©ôOC©öWB©ø_A©úgÅ¨À_ÖÈ  ABù	@!‹)‘A!ëà3‰šÀ_ÖùøôO©óªø_©öW©ý{©ý‘¡ ´ø*ö*õª÷ ªô*Ù    è‹‘áª(DA¸k©  Th
@¹( 4h" ‘
  Ã  TàªâªTøÿ—à 4àþÿ7h@¹h 4h2 ‘ýßˆ)CBù?ëBýÿT  ˜ 6ø@¹)CBùˆb ‘y~’
‹_	ë¨ Tù‹è ¹6¸6S ‘àªáªâªí÷ÿ—h" ‘ßj48   6ø@¹)CBùˆb ‘y~’
‹_	ë) Tóªàªý{D©ôOC©öWB©ø_A©ùEøÀ_Öù‹è ¹6¸6S ‘àªáªâªÑ÷ÿ—ßj48h2 ‘ýŸˆóªìÿÿÿÃÑöW©öªüo©úg©ø_©ôO©ý{©ýƒ‘ ´ó*õªô ªÛ  ã ¹Á€RàªÚ÷ÿ—÷ ª€  ´øËØ  52  àªï÷ÿ—ø ªØ 4È@¹( 4ÈB ‘ýßˆiCBù?ëã Tˆ‹‘  s 6œ@¹@’iCBù(c ‘y~’
‹_	ëh Tˆš‹ˆ ¹X¸óª[S ‘àªáªâª”÷ÿ—k98ûªó@¹ÈB ‘ýŸˆd àªáªâªã*eÿÿ—ö ª   ´õ ‘7ùÿµ  öªàªý{F©ôOE©öWD©ø_C©úgB©üoA©ÿÃ‘À_Öùøø_©öW©ôO©ý{©ý‘ö*Ã  ù*ó*õª÷ªô ª¡ÿÿ—` ´@¹ø ªH 4 ‘ýßˆÉ  )ABùŠ‹J‘?ëà3Šš  ¹ 6å3 ‘àªáªâ*ãªä*îþÿ—À  ´è@¹	 ‘(ýŸˆ  àªý{D©ôOC©öWB©ø_A©ùEøÀ_ÖöW½©ôO©ý{©ýƒ ‘a ´(@¹óªôªöªõ ª¨ 4È" ‘ýßˆÉ  )ABùàªª‹J‘?ëá3Ššâªãªêÿÿ—È@¹¨ 4È ‘É  ýßˆ)ABù?ëb  Tà*"  ¨‹ ‘áª€?ÖÈ@¹¨ 4ÈB ‘ýßˆÉ  )ABùàªª‹J‘?ëá3ŠšâªãªÎÿÿ—È@¹¨ 4È2 ‘ýßˆÉ  )ABùàªª‹J‘?ëá3ŠšâªãªÀÿÿ—à 2ý{B©ôOA©öWÃ¨À_Öóøâª ‘ã*ý{©ýC ‘ó ª2ÿÿ—  ´@¹H 4 ‘ýßˆÉ  )ABùj‹J‘?ëà3Šš  àªý{A©óBøÀ_Öý{¿©ý ‘å*äªã*âª ‘æ 2kÿÿ—  ñàŸý{Á¨À_Öý{¿©âª ‘ã*ý ‘ÿÿ—€  ´ ‘à 2ýŸˆý{Á¨À_Ö ‘ãªâªáª…ÿÿø_¼©öW©ôO©ý{©@¹ýÃ ‘	hh¸6 qd T ó ªôª÷* ‹È q¥ˆ}Ûu¸È  4i
@¹?kb  Tàª  `‹áªÛöÿ—  4¨ Q  q÷¦•Á–ßkŠýÿT €à*ý{C©ôOB©öWA©ø_Ä¨À_Öø_¼©öW©ôO©ý{©@¹ýÃ ‘	hh¸6 qd T ó ªôª÷* ‹È q¥ˆ}Ûu¸È  4i
@¹?kb  Tàª  `‹áª±öÿ—  4¨ Q  q÷¦•Á–ßkŠýÿT €à*ý{C©ôOB©öWA©ø_Ä¨À_ÖÿÃÑüo©úg©ø_©öW©ôO©ý{©@ùýƒ‘	@¹9 q„ T @ù	@¹ôªø*õ*ú‹ã ù<Ÿ qˆ§œ}HÛ{¸ó‹h@¹èjh¸È  4é
@¹?kb  Töª  ö‹àªáªâª¡öÿ—€  4  ø6x   Èju8¨ 4y Q?kªüÿTà*ý{F©ôOE©öWD©ø_C©úgB©üoA©ÿÃ‘À_Öˆ 	 q£þÿTè@ùà 2M ©òÿÿÿÃÑöW©ö ªàªüo©úg©ø_©ôO©ý{©ýƒ‘ã ©øªõªjöÿ—@ù:@¹: 4@ù)@¹÷ ªè*	‹|Ûh¸}@“‹‚@¹_ kˆ Tˆ@¹È  4É
@¹?kb  Tàª  À‹áªböÿ—   4h k£ýÿT  ˆ
@¹ 1À  Té@ù( ¹(@¹‹ys¸C<‹@¹ 1`  Té@ù( ¹ý{F©ôOE©öWD©ø_C©úgB©üoA©ÿÃ‘À_ÖÿCÑ €üo©úg©ø_©öW©ôO©ý{©è#)@¹óªôªõ ªöªý‘è  4©
@¹?k‚  Tøªùª  ¸‹ùªùc©  âKàC ‘ã ‘áªÿ ©aÿÿ—@ 6àÀ=ö ‘à€=ùcA©Á€Ràªóõÿ—@¹÷ ª(‹		@¹? 1@  Té/ ¹@¹ 1@  Tè+ ¹âC ‘ã³ ‘ä£ ‘àªáª‡ÿÿ—7üÿµ÷@ùù@¹Ù 4ø@ùé@¹è*	‹IÛh¸	‹i@¹É  4ª
@¹_	kb  Tàª   	‹áª}@“Õõÿ—  4ˆ kþÿTâC ‘ã³ ‘ä£ ‘àªáªhÿÿ—t  ´è/@¹ˆ ¹s  ´è+@¹h ¹ý{H©ôOG©öWF©ø_E©úgD©üoC©ÿC‘À_ÖÔ  ´h@¹é/@¹ 1(ˆˆ ¹Sþÿ´è@¹‹y|¸‹@¹ 1AýÿTèÿÿõøôO©óªõªâ3 ‘ã# ‘ý{©ýƒ ‘ô ª‚ÿÿ—• ´è@¹ 1à  T‰@¹) ‰	‹(yh¸ˆ‹  èª¨ ù“ ´è@¹ 1à  T‰@¹) ‰	‹(yh¸ˆ‹  èªh ùý{B©ôOA©õCøÀ_Ö!  !$!‘  ÿÃÑöW©ôO	©ý{
©ýƒ‘èª Rô ª ràªöÿ—á ‘ó *öÿ— ø7è@¹è  5è@¹¨  5èC@9I€R	j  Tà*Xõÿ—à*ý{J©ôOI©öWH©ÿÃ‘À_Öõ@ùõþÿ·â 2ã 2àªáªä*åª™õÿ— ±ÀýÿT@¹ö ª q( TÈ
@¹¿ëÁ  Tà*>õÿ—à 2–V ©åÿÿàªáªlõÿ—Þÿÿóøý{©ýC ‘@ùó ªa  ´`@ùcõÿ—~ ©ý{A©óBøÀ_Ö÷ø€‘öW©õ*ö ªâ*àªôO©ý{©ýÃ ‘ó*ôª
õÿ—hSÿJ58ÈF ¸â*àªáªõÿ—ßJ38ý{C©ôOB©öWA©÷DøÀ_ÖöW½©€‘ôO©ô*õ ªâ*àªý{©ýƒ ‘ó*òôÿ—!  (@¦R  ‘!¬!‘b€RßJ48¨ ¹êôÿ—³> ¹ý{B©ôOA©öWÃ¨À_ÖÿÑ÷C ùöW	©ôO
©ý{©ýÃ‘öªô ªtõÿ—ˆ"A9 @¹ó ªh  4à 2A  àªõÿ—üñi  Tà*;  •&‘àªáª4õÿ—á ‘Fõÿ— 1  Tè@¹@q¡ T    $!‘á2 õÿ—  4¨  ÐÝGù ä o	A ‘@ù‰ ù€‚<€‚€<”" ùàªá*âªãª ?Ö  ¨  Ðá-‘ˆ~ ©”" ùàªûûÿ—€ ù  µ  ¨  ÐýGù ä o€€=	A ‘@ù‰~ ©€‚ ­”" ùàªá*âªãª ?Öàø6à 2€"9w ¹ý{K©ôOJ©öWI©÷C@ùÿ‘À_Öw ¹T	 ”öW½©ôO©ý{©ýƒ ‘ó ªàªôªõªÀôÿ—üñè Tv&‘àªáªæôÿ—¨  ÐýGù ä o`€=á 2	A ‘@ùàªâªãªi~ ©`‚ ­s" ùõ 2 ?Ö`  6u"9  õ*ý{B©ôOA©à*öWÃ¨À_Ö A9ˆ 4ý{¿©ý ‘  @ù @ù@ù ?Öý{Á¨€  ´ ‘ ýßˆÀ_Ö  €À_Öóøý{©ýC ‘ A9h 4  @ùóª @ù@ù ?Ö   ´ý{A©áªóBøDýÿý{A©àªóBøÀ_Öø_¼©öW©ôO©ý{©ýÃ ‘öªõªóª7 ‘áª  ”|Sø *‚ àªáª3ôÿ—¿9Õh@¹k¡þÿTv‚‘µ  ´â2àªáªÑôÿ—!  !$‘â 2àªpôÿ—@  5@¹à*ý{C©ôOB©öWA©ø_Ä¨À_ÖöW½©ôO©ý{©ýƒ ‘5üßˆ• 6óª¬ôÿ—ô ª  – ¹  –@¹@€Ráªâ*ã*äªåªæ*wôÿ— 1€þÿTuþßˆ•þ7ý{B©ôOA©à*öWÃ¨À_Öúg»©ø_©öW©ôO©ý{©ý‘ÿC Ñõª3€‘!  ¢ƒø!$‘â 2àªôª:ôÿ—áª÷ *Ðÿÿ—ö *× 4è*ýXÓ(C |’é ‘ú ‘8Ë ‘· ‘  _ ‘áªÁÿÿ—è *ýXÓ(C |’é ‘ö *ú ‘8Ë ‘àªáªâªÏóÿ—k98¿9Õ¨@¹ßkýÿT¤ƒ[øàªáªâªã*€ ?Ö_ ‘¿Ñý{D©ôOC©öWB©ø_A©úgÅ¨À_Ö¨@¹¤ƒ[øh €7¢ ‘  ¨>@¹¢‹àªáªã*¿Ñý{D©ôOC©öWB©ø_A©úgÅ¨€ ÖôO¾©ý{©ýC ‘óªô ªOÿÿ—  ´ý{A©á ªàªâªãªôOÂ¨Zÿÿ 9ý{A©ôOÂ¨À_Ö÷øöW©ôO©ý{©ýÃ ‘lqè T A9¨ 4  @ùó*öªôª @ù@ù ?Ö  ´ˆ@¹õ ª€ ‘b  2— ¹¿;Õáª'ôÿ—è Yh3ˆþŸˆôÿ— @¹ó ª@€Râ 2ã{ 2áªäªåªæ*àóÿ— 1  T¨@¹¡ ‘@€Râ 2 (üŸˆt@¹ã{ 2äªåªæ*Òóÿ— 1à*¡  Tà*t ¹    €ý{C©ôOB©öWA©÷DøÀ_Öv ¹çÿÿø_¼©öW©ôO©ý{©ýÃ ‘ó*ôªõ*öªŸpq÷ ªc T!  !$‘â 2àªóÿ—è *  €Õ 4h  4,  U 4è"A9 4à"@ù @ù@ù ?Ö` ´ø ªà"@ùáª @ù@ù ?Ö€ ´áªâ*ãªä*>üÿ—À 6@¹ ‘ ˆþŸˆ¼óÿ— @¹ó ª@€Râ 2ã{ 2áªäªåªæ*Šóÿ— 1à*¡  Tà*u ¹    €ý{C©ôOB©öWA©ø_Ä¨À_ÖõøôO©ý{©ýƒ ‘ A9È 4õ ª  @ùóª @ù@ù ?Öà ´ô ª "@ùáª @ù@ù ?Ö  ´áªüÿ—  6ˆN@¸ ˆþŸˆ‹óÿ— @¹ó ª@€Râ 2ã{ 2áªäªåªæ*Yóÿ— 1à*¡  Tà*u ¹    €ý{B©ôOA©õCøÀ_Öÿƒ Ñý{©ýC ‘â*£ Ñáªäª  ” Ã_¸ý{A©ÿƒ ‘À_Öø_¼©öW©ôO©ý{©ýÃ ‘ôªóªõ*¡ ´öª_óÿ—÷ ªø@¹@€Ráªâ*ã*äªåªæ*-óÿ— 1à  T¸1  TÈþßˆk þÿT  è@¹ø ¹àK÷ÿÿà*  h ¹
   A9 4  @ù @ù@ù ?Ö`  ´ ‘Ýÿÿà 2ý{C©ôOB©öWA©ø_Ä¨À_Öÿƒ Ñý{©ýC ‘á )ÿ ù A9H 4  @ù  ! 7‘â ‘ @ù@ù ?Öà@ù  àªý{A©ÿƒ ‘À_Ö A9H 4ý{¿©ý ‘  @ù @ù@ù ?Öà*ý{Á¨À_Ö  €À_Ö) @)
 	k* ¹A  T  ùÀ_Ö…òÿóøý{©ýC ‘ó ªàªÀùÿ—` ùý{A©  ñèŸà*óBøÀ_Ö @ùÀ_Ö @ùÀ_Ö @ùûÿÀ_Öóøý{©ýC ‘ó ª @ùÀ  ´¨  ÐéGù@ùŸòÿ— ùý{A©óBøÀ_Ö €Ò €R@8¤@’„ Ãšc B ªeÿ?7"  ùÀ_Ö €Ò €R@8…@’¥ Ãšc B ªdÿ?7ü q¨  T„ 06 €’ƒ Ãšb ª"  ùÀ_Öý{¼©ý ‘óS© Sóªõ[©ŸBqöªõª¡  TS ‘`ò}’„@ø*   ?0 qè T    Ð"‘ Ha8a   ˆ ‹  Öàª¡ã ‘Íÿÿ—  àª¡ã ‘Óÿÿ—¡@ù  àª$@x  àªD@¸  àª$€x  àªD€¸  àª„@ø  ‘òÿ—á  ´‚
_@ qs–š! ‹T 86! @ù¡ ùóSA©õ[B©ý{Ä¨À_Ö¢  ð €RA@‘@@9  9  9  9  9  9  9  9   9 $ 9 ( 9 , 9 0 9 4 9 8 9 < 9 @ 9 D 9 H 9 L 9 P 9 T 9 X 9 \ 9 ` 9 d 9 h 9 l 9 p 9 t 9 x 9 | 9  9 9 9 9 9 9 9 9  9 $9 (9 ,9 09 49 89 <9 @9 D9 H9 L9 P9 T9 X9 \9 `9 d9 h9 l9 p9 t9 x9 |9 €9À_Ö£  Ðý{¿©ý ‘c¼R9  qÁ  TA  ù Aù ð¶Ü9  4òÿ—| ùý{Á¨À_Öý{¿©?„qý ‘M  T,òÿ—£  Ð"|@“c@‘ AùØaøchb8Ä ð¶ ‹@`M9`  4àª    qAþÿT  @ùý{Á¨À_Öý{¼©ý ‘õ[©õ ª  AùóS©óª` ð¶ ÜM9à  5`~@ù   µaŠAùàª¢ã ‘Ìÿÿ— €Ò¶  Ð¢‹ ztøaztøB`M9B  4òÿ—c‹ ëâŸc`M9 4â  4ÂB‘‚jb8_  qÁþÿT  ù
  ? ëãŸ_ jÀ  T?  ë€  TÂB‘Bht8fñÿ—” ‘Ÿ†ñaüÿT ¢Aù€ ð¶¡ÞM9  €Òa 5¡~@ù  €Ò µá€Ràª°ÿÿ—¡ŠAùuªAù  Ë  ‹óSA©õ[B©ý{Ä¨À_Ö ˆAùÀ_Öý{¿©?„qý ‘M  TÑñÿ—¤  Ð#|@“„@‘ Aù„hc8Å ð¶ ‹c`M9c  4Ø!ø  Ÿ  q ØaøAþÿT  ùý{Á¨À_Ö ŒAùÀ_Ö Aù ŒAùBüÓ"  ¹À_ÖŒùÀ_Ö AùÀ_Ö œAùÀ_Öý{½©  Ñý ‘¡c ‘N ” ë @ùý{Ã¨ ŸšÀ_Ö ˜AùÀ_Ö ”AùÀ_Öý{¿© Sàª_üqý ‘À TB_€ qÀ T¨  T" 4_@ qÁ T  _ q  T_@q@ T_À qá  Tý{Á¨çÿÿý{Á¨çÿÿý{Á¨×ÿÿŠñÿ—  €Òý{Á¨À_Öý{·©ý ‘õ[©÷c©ùk©ûs©óS©  ðüªúªóªù €Ò{#‘¶ã‘µ‘7 €R¹#‘ ëb$ TA£AùCAùb&CùaüA‹_ ë¢# T @9 ‘# qÁ  T`2Cù!@’! ›a&ù  q¡  T!@’àª¡? ù1   q¡  T!|Óa‹? ¹ ?¼ qH  Ta[axc  a¨!‹  ÖbÂY9áª¢7 ùà*¨ÿÿ—á ª¢7@ùãªà*âª†þÿ—ô ª G@ù`&ùð  @9 ‘c2Cù"›b&ùê  @x ‘c2Cù"›b&ùä  @¸ ‘c2Cù"›b&ùÞ  àªáªSþÿ—áªQþÿ—ô ªa.Cù C@ùk  àªáªJþÿ—ô ª ?@ù„ñÈ T` ‹ ¹Ë  àªáª@þÿ—ô ª ?@ù„ñˆ T` ‹Á €R ¹À  àªáª5þÿ—áª3þÿ—ô ª ?@ù„ñè T ì|ÓB €Ra ‹" ¹¡G@ù®  ˜  ´ãªCù  ÿƒÑã ‘àªáªÉ€Òqðÿ—`ù¥  cCùàªÉ€Ò£7 ùáªiðÿ—£7@ùxùøª›  àªáªþÿ—¡C@ùaùáªþÿ—ô ª C@ù`ù  àªáªþÿ—ô ª C@ù`ùwB¹ˆ  àªáªýýÿ—ô ª C@ù,  @ €Rtù`B¹àªS  áªàªòýÿ—¡?@ù?„ñ¨	 T!ì|Óc €Rb‹G  àªáªèýÿ—áªðýÿ—ô ªa.Cù G@ù|› ?@ù„ñè T ì|Ób ‹W ¹`  àªáªØýÿ—¡C@ùaùáªÞýÿ—wB¹ô ªa.Cù G@ù |›`ùU  àªáªÔýÿ—ô ª÷ÿÿàªáªÅýÿ—áªÃýÿ—ô ªa.Cù C@ù	  àªáª¼ýÿ—áªÄýÿ—ô ªa.Cù G@ù|› ?@ù„ñh T ì|Óƒ €Rb ‹C ¹3  áªàª«ýÿ—¡?@ù?„ñÈ  T!ì|Ó£ €Rb‹C ¹`j!øáª¡ýÿ—´C@ù ‹&   €Òb‘ ? ù €ÒW ¹  ‘Aø€ ñ!  ‘aÿÿT  àªáªýÿ—ô ª C@ù@«ù  àªáª‰ýÿ—áª‡ýÿ—ô ª¡C@ù`.Cù|› ?@ù„ñ T ì|ÓáËb ‹W ¹aj ø  Sðÿ—àªÝþÿ¿ ‘óSA©õ[B©÷cC©ùkD©ûsE©ý{É¨À_Öý{·©Ð€Òý ‘óS©ô ªóªàª €Rõ[©÷c©ùk©û+ ù(ðÿ—ŽAùŸªùŸ’ùa  µ  €Rð  €¢Aù üÓ  Ñ   ‹¢‘Ð
 ”õ ª  µŽAù -‚R Pºr" @¹_  k!þÿT!@¹  €R €ºr?  k! €RaýÿT”ŠAùà€Ò`ù &€ÒaB¹ƒÂ‘`ù €Ò €ÒâªA ¹@ø   ‘ ëÿÿTá}€’€B	‘! Ëe‘& €R! Ë @¹â 4$ RÊ¨r_ k Tb‘$ ‹F ¹ ‹Dø_ ëaÿÿT@¹  ‹ñÿÿ”Â‘  €R”Ë`Î9tú ù`ú¹€ €R`
¹`„@ù Ë €Òcù`6ù¦  ¡€¹¶ ‘€žAùÖË`&ù×& ‘àª›ïÿ—  ‘Á&@9à ‹?”qá  TÁ*@9? q  T„@ø×. ‘a>ùÁ"@9? qé  T @9?  qá T@9¡ 5  ‘¹ã‘áªöüÿ—¡?@ùa2ù¡‘üüÿ—á ª¢C@ùb.ùÂ"@9_ q  T  ‘! @9  áªçüÿ—¡?@ùa6ù €aÆ9 €Òá@9?èq Táª÷ ‘Üüÿ—¸?@ù! €RaÊ9 ‹÷ ‘; €R¹#‘áò_8A 4?0q  T @9aÆ9  ?Hq¡  T @9aÂ9  ‘  ?@q Tâ ªáªZ@8¢7 ùà*¢3 ùõýÿ—á ª¢7@ùà*ãªÔüÿ—¡G@ùa*ù  ?Lqá  T{Î9÷ ‘Þÿÿë €š  àª  ´Á@¹âªãª! ‘Á‹ûýÿ—`ÂY9üqà T   qh T  ð!$‘ H`8a   ˆ ‹  ÖB €R  ‚ €R  pïÿ— €R  €R`ÊY9BxSB  ‘ €Ò¢‹à  4àª¡#‘Šüÿ—¶G@ùâ ª ‹wÆY9ÿþq  Táªà*¢7 ù¶ýÿ—á ª¢7@ùà*£#‘•üÿ—â ª G@ù€’ù¡@¹ßëÀ‚šãª! ‘âª¡‹Äýÿ—  €R  ` €RóSA©õ[B©÷cC©ùkD©û+@ùý{É¨À_ÖÿÃÑä ªý{º©ý ‘ûs©¼C‘õ[©÷c©  ð  ðóS©ùk©øªóªƒ ù9 €RÖÒ$‘´ã‘÷²$‘µÃ‘Ÿ ë¢) Tš @9› ‘_ƒ qˆ T_ qb T_C q  TH T_+ q€ T T_ q` TÈ  T_ q¡& T€@ø›$ ‘3 _# q› ‘À T_' q¡% T€€9, _7 qà Tè  T_/ q  T_3 q$ T€@¸a  _; q›$ ‘  T_? qÀ T _W q  T( T_K q€ Tã T_O q€ T_S qà T _g q@ TH T_[ q! Q  Q€ T__ q  T _?qè T_Ã q T_Ÿ qÈ  T_“ qb T_‹ q T¸  _» q¨ T_§ qã T³  _Cq  TÈ  T_¿qH	 TàªACQE  _Sq  Tè T_KqA Tàªáªº‘ðûÿ—áªøûÿ—û ª¡@¹àª”üÿ—A@ùA  _[q  T_ÇqA T‚@9áª› ‘¢7 ùà*º#‘ýÿ—á ª¢7@ùãªà*âªñûÿ—  @Ã QË  €@9É  €@x  €€x› ‘Ä  €€¸› ‘Á  €@ø¿  àªáªÅûÿ—û ª€@ù¹  º#‘àªáªÈûÿ—û ª@@ù²  àªáª¸ûÿ—û ª¡@¹àª^üÿ—ª  ¢‘àªáª¢7 ù¸ûÿ—û ªAÃQàªTüÿ—¢7@ùA @ù  ‹  y 4  Q   49 Q›    Q@9¡C ù› ‘ |@“?  ëê T  Ë€{`ø  ? qM T  Q€Û`øˆ  ? q­ TƒÛ`ø‚ÛaøƒÛ!ø‚Û ø…  ? qÍ T" Q…Û`øƒÛaø„Ûbø…Û!ø„Û øƒÛ"ø{  ¹ 49 Q_ q‚Ûyø@ T T_ q  T_g q TAü‚Ê üB‹j  _ q@ T_Sq   T_ƒ qa Tà"ªb  €@9› ‘  Q qˆ TàJ`8a   ˆ ‹  Ö@ @9W  @ @yU  @ @¹S  @ @ùQ  àËO  àªáª¢7 ùTûÿ—û ª@ù¢7@ù@ ‹F  ? qm T" QZk Q9 Q_S qÛbø€Ûyøˆ TÃJz8d  ƒˆ#‹` Ö  Š$   Àš"     Ë   #Àš`„ ›   |›    ª    ‹    Àš   $Àš   (Àš    Ê  ?  ëàÇŸš  ?  ëà·Ÿš	  ?  ëàŸš  ?  ëà§Ÿš  ?  ëà×Ÿšù*  ?  ëàŸšüÿÿ€€x› ‘{ ‹  9 49 Q› ‘€Àx`£ ‹Ûyø? ë{€š  äíÿ—?ÿ qÈÿÿT€Û9ø9 äª³þÿ9ÿÿ49 QóSA©õ[B©€Ûyø÷cC©ùkD©ûsE©ý{Æ¨ÿÃ‘À_ÖÿƒÑx€Òý{º©ý ‘óS©õ[©ó ªµ‘÷c©àªøªáªùk©ûs©<íÿ— ãAù` ð¶ ÞM9à  5 ~@ù   µaŠAùàª¢Ã‘{ûÿ—`¢Aù@ ð¶Þ9 CF¹~ ù qÀ T q! T´ã‘ CùáªÓúÿ—@ùâª €Ò ‹iþÿ—÷ ª  3F¹àªsûÿ—Cù ‹   ÐwŠù 0%‘úªvb‘ €Ò 7 ù¹ã‘»  °< €R@@¹  Q q T¡7@ù H`8a   ˆ ‹  Ö@@ùà ‹  A@ù Â!‹ `M9  4àªUûÿ—aC‘ja8?  q‰ T€íÿ— Úaø	  @@ùáª¡úÿ—!@ùâªãª ‹7þÿ—a¢Aùð¶ß 9  @@ùà ‹ëÿÿ@@ùáª’úÿ—!@ùâªãª ‹(þÿ—âÿÿÜ 9`z4ø” ‘ZC ‘ŸŠñÖ ‘!ùÿT`¢AùÏY9 A² ø@’_ k  š`¢ùõ[B©óSA©÷cC©ùkD©ûsE©ý{Æ¨ÿƒ‘À_ÖÿCÑý{¼©ý ‘óS©õ[©ó ª÷ ùöª÷ªx€Ò €Rôª/íÿ—tŽù  èÒ´C‘`¢ùáªàªôüÿ—@  4:íÿ—   µ  ° àGù   µ BR9  5¯úÿ—   B‘áÿÿÐ ‘! ?‘Ýìÿ—àþÿ5¢#‘àªáªéúÿ—¿?ù  €R “¹à€Òáª CùàªNÿÿ—vŽù÷@ùóSA©õ[B©ý{Ä¨ÿC‘À_Öý{¾©ý ‘óS©óªô ªAÿÿ—a6Cùs‹`
@¹ qa  TŸŽù  àªÚúÿ—€ŽùóSA©ý{Â¨À_ÖÿÑý{¼©ý ‘óS©õ[©÷c©õ ªóª´‘— €Ráªàª±üÿ—ø *àªûÿ—a¢Aù üAË¡@ù ëöŸ˜ 5…*Cùe  µ¶ 4ììÿ—¢@ù  €RÁ2ãªäª  ?Ö q  T  q þÿT  àªáªÆÿÿ—âÿÿ@ €RóSA©õ[B©÷cC©ý{Ä¨ÿ‘À_ÖÿÑý{»©ý ‘÷c©@ù@ùõ[©ùk©óS©õªô ª¶C‘Y€RZ€Ràªáª}üÿ— qó *@  T  5 q‚@ù  €R!šãªäªåªà?Ö€ 5 q€ TÅ*Cù¥  µàªáª™ÿÿ—éÿÿ‚@ù  €RA€Rãªäª  ?Ö qó *   T  q@þÿT@ €R  à*óSA©õ[B©÷cC©ùkD©ý{Å¨ÿ‘À_Öÿ)Ñx€Òý{½©ý ‘óS©´Ã ‘õ ùóªõ ª €Ràªµ ‘zìÿ—•Žù  èÒµÃ‘€¢ùáªàª?üÿ— €Ò@ 5 3J¹ qà T " ‘bÒ‘a‚ ‘µ¢‘ @9C  9 q`  T q  T€_ø#  ù  ?  ù @ ‘B ‘ ë!  ‘AþÿT Eùáª`
 ù Eù`by /Eù`fy€ªAù` ù 7Eù` ùóSA©õ@ùý{Ã¨àªÿ)‘À_ÖÀ_ÖÿC8Ñý{´©ý ‘õ[©è'mµC‘ê/	mà©ì7
mâ©î?móS©âª¡C;‘ô ªàª÷c©ùk©ûs©³C‘ùþÿ—àªáªx€Ò¶C!‘»ëÿ—àªáªøûÿ— qà T`  4` €R,  Å*Cù¥  µàªáªÿÿ—óÿÿ  €R‚@ùá *ãªäª  ?Ö q€  T  q`þÿTîÿÿŸ
 ùàªPúÿ—a¢Aùx€Ò üAËáª€ ùàª˜ëÿ—àªáªÿÿ— q Táªàªþùÿ—tŽAù g ù`ŠAùáª¶ÿÿ—ô ù¤g@ù   €ÒâB©è'HmóSC©ê/Imõ[D©ì7Jm÷cE©î?KmùkF©ûsG©àA©ý{Ì¨ð ‘ÿc$‹ÿC8‘À_ÖÿCÑý{´©ý ‘óS©è'm´C‘ê/	mâ©ì7
mà©î?mõ[©÷c©õ ª÷ªöªàªâª¡C!‘³C‘ùk©ûs©™þÿ—áªx€Òàª\ëÿ—·
 ù¶ ùàªáª
ÿÿ— q €Òa Táªàª¿ùÿ—³×Cù g ù ÓCùáªwÿÿ—ó ù¤g@ùâB©è'HmóSC©ê/Imõ[D©ì7Jm÷cE©î?KmùkF©ûsG©àA©ý{Ì¨ð ‘ÿc$‹ÿC‘À_ÖÿCÑý{´©ý ‘óS©è'm´C‘ê/	mà©ì7
mâ©î?mâª¡C!‘õ[©õ ªàª÷c©ùk©ûs©³C‘^þÿ—àªáªx€Ò!ëÿ— 
@ù   µàªáªŸþÿ—  àªáªËþÿ— q@  Tëÿ—áªàª€ùÿ—³×Cù g ù ÓCùáª8ÿÿ—ó ù¤g@ùè'HmàA©ê/ImâB©ì7JmóSC©î?Kmõ[D©÷cE©ùkF©ûsG©ý{Ì¨ð ‘ÿc$‹ÿC‘À_ÖÿCÑý{´©ý ‘è'mà©ê/	m@ùì7
mâ©î?mõ[©óS©÷c©ùk©ûs©âªõ ª  µÿÿ— €Ò  ´C‘¡C!‘àª³C‘þÿ—áªx€ÒàªÝêÿ—àªáªþÿ— q@  T_ëÿ—áªàªBùÿ—³×Cù g ù ÓCùáªúþÿ—ó ù¤g@ùâB©è'HmóSC©ê/Imõ[D©ì7Jm÷cE©î?KmùkF©ûsG©àA©ý{Ì¨ð ‘ÿc$‹ÿC‘À_Öý{¿©ý ‘@ù‚  ´á ª  €R@ ?Öý{Á¨À_Öÿ)Ñâªý{¶©ý ‘óS©è'm´ƒ‘ê/mõ[©ì7m÷c©î?	mö ª÷ªàª¡ƒ+‘µƒ‘ùk©ûs©Ùýÿ—àªáªÝúÿ— qó *@  T` 5àªáªÀ?Öà  5 qà  Tàªáª þÿ—ðÿÿ` €R  à*óSA©è'Fmõ[B©ê/Gm÷cC©ì7HmùkD©î?ImûsE©ý{Ê¨ÿ)‘À_Ö €Ò €R@8¤@’„ Ãšc B ªeÿ?7"  ùÀ_Ö €Ò €R@8…@’¥ Ãšc B ªdÿ?7ü q¨  T„ 06 €’ƒ Ãšb ª"  ùÀ_Ö#@ùA@ù ëà#ŸZ ë ”ŸÀ_Öý{º©ý ‘óS©sxSù# ù÷c©õ[©ø ªùªôª÷*s kª Tu ¿kj Ta~}“àª‚‹£/ ùjaøB@ù ?Ö k£/@ùs¢•u|}“v~}“àªjuø‚jvø ?Ö ø6jvøã*€juøj5ø€j6ø`zS ãÿÿóSA©õ[B©÷cC©ù#@ùý{Æ¨À_Öý{»©ý ‘óS©S@ùõ[©÷c©ù# ùø ªùªõªV@ ‘tþAÓ” Qø7àªáªâªã*ä*½ÿÿ—øÿÿ €Òs Q×Î3‹kÍ Tájtøä* 
@ùâª¡
 ù €Ràj4øáªàªs Q”" Ñ«ÿÿ—òÿÿóSA©õ[B©÷cC©ù#@ùý{Å¨À_Öý{¿©  Süqý ‘à T   qh T  Ð!P%‘ H`8a   ˆ ‹  Ö@ €R  € €R  wêÿ—  €R   €Rý{Á¨À_Öý{¿©  Süqý ‘  T € q@ Tˆ  T@ q   5
  À qÀ  T@qÀ  Taêÿ— @ù   @ù    €Òý{Á¨À_Öý{¿©  Süqý ‘  T € q@ Tˆ  T@ q   5
  À qÀ  T@qÀ  TIêÿ— @ù   @ù    €Òý{Á¨À_Öý{¼©ý ‘óS© Sóªõ[©ŸBqöªõª¡  TS ‘`ò}’„@ø*   ?0 qè T   ° p%‘ Ha8a   ˆ ‹  Öàª¡ã ‘!ÿÿ—  àª¡ã ‘'ÿÿ—¡@ù  àª$@x  àªD@¸  àª$€x  àªD€¸  àª„@ø  êÿ—á  ´‚
_@ qs–š! ‹T 86! @ù¡ ùóSA©õ[B©ý{Ä¨À_Öý{»©ý ‘óS©@@yô ª÷ ù÷ªs*CÓáªà*õ[©öªˆÿÿ—õ ªâ" ‘áª£‘à*²ÿÿ—€B@yáªÂ" ‘£#‘ üCÓ¬ÿÿ—÷@ù¢#@ù¡'@ùóSA©_ ëà#ŸZ_ ëõ[B© ”Ÿý{Å¨À_Öý{¼©ý ‘óS©$ ‘ó ªàªéÿ—  ‘b"@9 ‹_ q	 T‚j`8à€R_  q¡ T"@9b 5! ‘`&@9èq`  T  €R%  ´Ã ‘àªáª¿þÿ—¡ã ‘Çþÿ—b"@9_ qa  T  ‘  áª¶þÿ—áªs* ‘´£ ‘²þÿ—a@9?Hqa  T  @9  ?@q Tâ ª €Òãª@@8  kÿÿ—  ?0qÁûÿT  ‘s ‘îÿÿóSA©ý{Ä¨À_Öý{¹©ý ‘õ[©ö ª @@yóS© (CÓóªáª÷c©ùk©ûs©÷ª S#ÿÿ—¸ƒ‘û ª¹£‘  €Ò: €Òa@¹a 4a@¹a 4Â‚@9ô ªb6t ‘”Â!ËŸ ëà  Tàª¡ÿÿ—õ *áªÿÿ—û ªµ 5`@ùa
@ù 3 ù¡7 ù  ´ 3@ù¡7@ùà Ë ë‚ Tàª   ¼ Sáªb" ‘ãªà**ÿÿ—â ª €Òãª  %ÿÿ—à*Ûþÿ—à * ñ €’ˆ  Tð}ÓA#Áš! Ñ 3@ù?  ê`  Tâÿÿô ª`@¹  ‘s ‹àªÅÿÿ  €ÒóSA©õ[B©÷cC©ùkD©ûsE©ý{Ç¨À_Ö ‘ €¹   Ëfÿÿý{·©?¼ ñý ‘óS©õ[©÷c©í—Ÿ@ù @ù­ 4C(@¹c 4¡  @ù@ù#@ùŸ ë£  a Ti|Bùhà‘¿ 	ëá T	@ù €Ò €Òá	ª! ´J @ù( @ù$@ù_ë£ T_ëb T? 	ë'@ù(@ùÀ Tcà‘ @ù  ù) ùa ùX  ª¤ ´(@ùäªë¥ šáªçÿÿ$ ùdà‘ ‘e|ù„ ‘? ø?€ø!€ø!À ‘? ëaÿÿTcà‘a@ ‘Ä ùa ù_( ¹  ?d ñ	 T €Ò €Ò  äª0@y	 €Ò
 €Ò €Ò €Ò €ÒÎ Ñß ±€ TÀ @¹ qá TÁ@ùC @ùá ‹ ëC TÀ@ù   ‹  ë)!€š  ëJ!š  ëŒ%Ÿš	  ªœRŽ¬r k€  T qk†š  èªÆà ‘ãÿÿl  µ  €R  - 4    ´ä  ´@ù¡ ùà‘#@ùƒ ù$ ù à‘ @ù ù ù ù
  ù	 ù¨ýÿ´ 	@ùõ  ‹àh`8 q T´@9áªóªà*_þÿ—á ª¢ ‘à*£C‘rþÿ—´
@9â ªŸþq@	 T @9ì qá Táªà*¢' ùOþÿ—á ª¢'@ùà*£c‘bþÿ—ö ª /@ùà
 ´Á@’! µÂ€¹a@ù¢‹? ë
 T  Ñð}ÓÂj¢¸¢‹? ë¢ Tã ª €ÒŸ ëB	 T` ‹ üAÓð}ÓÅj¢¸¥‹? ë TÂ‹ ‘B€¹¢‹? ëBþÿT  ã ªïÿÿð}ÓÀ‹€¹·‹àª/ÿÿ— Sà*íýÿ—âB ‹ €Ò  B  ‘£ƒ‘/þÿ—Àj´¸a@ùµ ‹ 3@ù  ‹?  ëB  Tw ùu ù  `@ù 7 ù`
@ù ; ù€ €R¡+@ù¿C ùb@ù 9 ƒ‘¿3 ù¡? ù·þÿ—` ùÀ ´ÿÿ— Sáªà*ùýÿ—á ªb@ùà*£c‘B  ‘þÿ— /@ù` ù  €R    €  Eèÿ—óSA©õ[B©÷cC©ý{É¨À_Öý{¼©ý ‘õ[©õ ªàªóS©óªôªíþÿ— Sáªà*Âýÿ—á ª‚" ‘£Ã ‘à*íýÿ—àªâþÿ— Sáªà*·ýÿ—á ªb" ‘£ã ‘à*âýÿ—¢@ù¡@ùóSA©_ ëà#ŸZ_ ëõ[B© ”Ÿý{Ä¨À_Öý{¹©ý ‘óS©÷c©ùk©õ[©óªû+ ùô ª €Ò €R €Ò €Ò·£‘8 €Ò`@¹  4`@¹  4u ‘µÂ Ë¿ë` Tàªþÿ—üqù *a  T  €’4   Sáªà*†ýÿ—ú ªB@y àq  Ta3B y	  !(CÓ? kÀ  T€‚@9  2€‚ 9  õª; Sáªb" ‘ãªà* ýÿ—à*Výÿ—à * ñ €’ˆ  Tð}Ó#Áš! Ñ 7@ù?  ê  T@ùÖ ‘ ë‚  T€ ù  õª`@¹áª  ‘s ‹ÀÿÿàªóSA©õ[B©÷cC©ùkD©û+@ùý{Ç¨À_Öý{¹©ý ‘õ[©ö ª @@y÷c© (CÓ÷ªáªû+ ùùk©óS© SóªAýÿ—¸£‘ú ª9 €Ò  €Òa@¹A 4a@¹ô ªA 4Â‚@9b6t ‘”Â!ËŸ ëà  TàªÀýÿ—õ *áª-ýÿ—ú ª•  5`@ù` ´  » Sáªb" ‘ãªà*Rýÿ—à*ýÿ—à * ñ €’ˆ  Tð}Ó!#Áš! Ñ 7@ù?  ê  Tà@ùÀ  ´@ù" ‘! ‘ ùx!ø`@¹  ‘s ‹àªÎÿÿóSA©õ[B©÷cC©ùkD©û+@ùý{Ç¨À_Öý{·©ý ‘õ[©öª€@9óS©÷c©ùk©ûs©ó ª  6`‚@9  7Ù   @¹”~KÓt µa6@ù¡@ù¡ ´àªEÿÿ— ±! T ÿ€R ù`B y    à‘ `‘` ùT  ” ‹µ" ‘ðÿÿ@ù6ÿÿ— ±ô ª þÿT€R  ëá ª`"@¹a  T P3   ( `" ¹T ´•
 ‘µò}Óàªáæÿ— C ù€ ´ ùàªÜæÿ— G ù  ´ ù¶  $‹E@ù„@ùD ´$ ‘! ‘{$øB ‘Z# ‘_ ëÁþÿT ù  ù¢G@ù C@ùA@ù @ù   ‹ ëÁ Tàªáªgüÿ—»G@ùµC@ùt@ù4 ´¸@ùšò}Ó`‹” Ñ¹‹@ù µ€‹Z# Ñ  ‘¼z øôþÿµ¡@ù`@ù   ‹  ù G@ù—æÿ— C@ùa@ù  ù` ù`‚@9   2`‚ 9`@ùß ë¢òÿT  €ÒÝ  @6{@ù €Ò¹ã‘º‘w@ùëâþÿTõ‹µþAÓ 
 ‘t{`øàª£ýÿ— Sü *áªà*¢7 ùvüÿ—á ª¢7@ùãªà*‚" ‘ üÿ—â ª €Ò€ ãª›üÿ—¡?@ùßëã  T C@ù   ‹ß ë T¸ ‘õª÷ªÝÿÿ`B@yr¡ Te@ù €Ò¢@ù ë¢ùÿTA ‹!üAÓ  ‘ x`ø@ù@ùßëÃ  TÃ ‹ßë T# ‘òÿÿâªðÿÿ (CÓx@ùáª €Ò S»‘Büÿ—: ü ª@ù¤ã‘  àªŽ  ³‹ãªsþAÓáª`
 ‘¤7 ù{`øà*‚" ‘aüÿ—â ª €Òà*ãª\üÿ—¡?@ù¤7@ùßë¢  TõªÿëcýÿT™ÿÿ C@ù   ‹ß ëƒüÿTw ‘óªöÿÿ@6t@ù@ùÁñÿ´àªâªåüÿ—  µ”" ‘ùÿÿa@ùàªâªÞüÿ—a  `‚@9@6u@ù·‘¢@ùB ´àªáªµ" ‘µþÿ—úÿÿb@ùàª¡‘°þÿ—¸C@ù¸  ´ @ù ë@  Tjæÿ—`‚@9à 7`B@yrà  T÷ÿÿð÷ò‘  ÷ÿÿð÷R#‘  ÷ÿÿÐ÷r8‘¹G@ù ´•  ðC ‘µâ‘@ùµB‘üª €Òäªëà T¿ëà T@ùàª¢@ù¤3 ù£7 ùà?Ö£7@ù¤3@ùÀ ø6 Ë  ‹@ù ùñÿÿ ‹{ ‘ ùõªœ# ‘éÿÿ  €Ò €Ò €Òÿÿ ‘  ‘%{$øÿÿ!@ù Ñàª£7 ùâª9# Ñà?Ö k£7@ùãÿT ‹!@ùøª ùÿÿàªáªâªjûÿ—ÿÿóSA©õ[B©÷cC©ùkD©ûsE©ý{É¨À_Öý{½©ý ‘óS©õ ùà ´ @¹¤ 4óª €’”  ða ù  Ð ùõª` ù ÿ€Rb ùc
 ù`B y àGù€  ´€â‘ €‘æÿ—€â‘ä@ùa ùä ùµâGùÕ  ´óSA©õ@ù €‘ý{Ã¨ÁåÿóSA©õ@ùý{Ã¨À_Ö €Ò €Ò×ÿÿý{¾©ý ‘ @¹ó ù! 4ó ª €ÒŠåÿ—á ªàªó@ùý{Â¨ñÿÿó@ùý{Â¨À_Öý{½© €’ý ‘óS©? ù“  ð  ù@ €R € 9" ù‚  Ð @@yôª$  ù 2# ù @ y@àGùÀ  ´`â‘¡ ù €‘Ûåÿ—¡@ù`â‘ä@ù" ùä ù”âGù´  ´óSA©ý{Ã¨ €‘ˆåÿóSA©ý{Ã¨À_Ö €Ò €ÒÙÿÿý{¾©ý ‘ó ùó ª €ÒTåÿ—á ªàªó@ùý{Â¨óÿÿý{½©ý ‘óS©õ[©€ ´ @¹A 4”  Ðõ ª–  ð€âGù€  ´Àâ‘ €‘°åÿ—Àâ‘  ‘ @ùó  ´a@ù? ëà T`¢ ‘s@ùúÿÿÀâ‘ @‘  `¢ ‘ @ù“ ´a‚@9á  7a@ù? ë!ÿÿTa@ù  ù  a@ù! @ù? ëAþÿTa@ù  ù`@ùåÿ—
   €Ò
  ”âGù´  ´Àâ‘ €‘@åÿ—“  µråÿ—”âGùTÿÿµàªóSA©õ[B©ý{Ã¨À_ÖÀÿÿý{¿©ý ‘ @¹  4ûÿÿ—ý{Á¨óäÿý{Á¨À_Öý{¸©ý ‘õ[©•  Ðù# ù÷c©÷ ªöª âGù™  ðóS©€  ´ ã‘ €‘fåÿ— ã‘è@ù“ ´`@ùÿ ëã  TàªáªÖýÿ—ô ªÀ	 µ  s@ùõÿÿ8ã‘ç@ù³ ´`@ùáª ç ùàªÉýÿ—ô ªë@ùC‘ ´# @ù`@ù  ëƒ  T"  ‘!@ùùÿÿa ùS  ù”ýÿ´4  µâGù €ÒU ´ ã‘ €‘óäÿ—Ô ´`@ùÀ ùa‚@9`
@ùÀ ù`B@y (CÓa 6àªÒûÿ— Sáªà*§úÿ—á ª‚" ‘à*£C‘Òúÿ— +@ù    €R³C‘ { ¹àÿÿð à‘áª·+ ù €Ò¿/ ù¿3 ù¿7 ù¿; ù<åÿ— ø7´;@ùt ´`@ùÀ ù`
@ùÀ ù`@ùÀ
 ù  µâGùõùÿµÒÿÿàªù#@ùóSA©õ[B©÷cC©ý{È¨À_Ö    /dev/__properties__ Failed to initialize system properties
 resetprop: New prop [%s]
 resetprop: setprop [%s]: [%s] by %s
 modifing prop data structure resetprop: setprop error ro. ro.property_service.version /dev/socket/property_service %s/%s System property context nodes %s/properties_serial u:object_r:properties_serial:s0 18ContextsSerialized 8Contexts ctl. /property_contexts /system/etc/selinux/plat_property_contexts /vendor/etc/selinux/vendor_property_contexts /vendor/etc/selinux/nonplat_property_contexts 13ContextsSplit         PROPsecurity.selinux /dev/__properties__/property_info Must use __system_property_read_callback() to read 16ContextsPreSplit     	   þ      ! * 4 * ? N Z d o w } ‚ Œ œ ª ¯ ¸ É ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý ý Ù ä ë      WWWW 	7777# '            	   ;P  ‰   ’ÿÿl  D’ÿÿŒ  L’ÿÿ¤  ”’ÿÿÄ   ’ÿÿÜ  ´’ÿÿô  Ð’ÿÿ  ì’ÿÿ$  “ÿÿ<   “ÿÿT  @“ÿÿl  T“ÿÿ„  h“ÿÿœ  |“ÿÿ´  œ“ÿÿÌ  °“ÿÿä  È“ÿÿü  ¼”ÿÿ<  4—ÿÿl  l˜ÿÿŒ  Ü˜ÿÿ¬  l™ÿÿÌ  œ™ÿÿô  Xšÿÿl  ›ÿÿŒ  h›ÿÿ¬   ›ÿÿ  `œÿÿD  äœÿÿÌ  Ôÿÿô  džÿÿ  ´žÿÿ<  ¤Ÿÿÿd   ÿÿ„  œ ÿÿ¬  ð ÿÿÌ  ”¡ÿÿì  ä¡ÿÿ	  t¢ÿÿ<	  ˜¥ÿÿl	  P¦ÿÿ”	  ,§ÿÿ´	   ¨ÿÿÜ	  t¨ÿÿü	  È¨ÿÿ
  ø¨ÿÿ<
  ˜©ÿÿd
  Èªÿÿ„
  ˆ«ÿÿ¤
   ¬ÿÿÄ
  `°ÿÿô
  ±ÿÿ  ”²ÿÿD  ø²ÿÿd  È¸ÿÿ|  Ô¸ÿÿ”  ¼¹ÿÿ¼  ´ºÿÿÜ  ¼ÿÿ  °¼ÿÿ4  ì¼ÿÿT  8½ÿÿt  Ø½ÿÿœ  P¾ÿÿÄ  œ¿ÿÿô  ä¿ÿÿ  øÀÿÿ<  Âÿÿd  ÐÂÿÿ„   Ãÿÿ¤  ÐÃÿÿÌ   Äÿÿì  tÄÿÿ  ¸Äÿÿ,  ÄÄÿÿD  üÄÿÿ|  $Åÿÿ”  dÅÿÿ¬  TÆÿÿä  hÇÿÿü  ¤Çÿÿ$  üÇÿÿL  Éÿÿ„  Éÿÿœ  hÉÿÿÄ  pÉÿÿÜ  „Éÿÿô  ŒÉÿÿ  ”Éÿÿ$  œÉÿÿ<  ÄÉÿÿd  ÌÉÿÿ|  ÔÉÿÿ”  LÊÿÿÔ  DÏÿÿ$  hÓÿÿl  ÙÿÿÄ  (Ûÿÿ$  Üÿÿl  PÜÿÿœ  Ýÿÿì  üÝÿÿD  ðÞÿÿ„  ôÞÿÿœ  làÿÿD  `áÿÿì  \âÿÿ”  Tãÿÿ<  xãÿÿd  Häÿÿü  päÿÿ  °äÿÿ,  ÌäÿÿD  Œåÿÿ„  @æÿÿÄ   æÿÿì   çÿÿ  `çÿÿ<  Pèÿÿt  àèÿÿ¬  ÜéÿÿÜ  <ëÿÿ$  Lëÿÿ<  hïÿÿ|  ðÿÿ´  dñÿÿü  ˜òÿÿD  øÿÿ”  ¤øÿÿÔ  °øÿÿì  ðøÿÿ$  „ùÿÿ\  ùÿÿt  ¼ùÿÿœ  ¼úÿÿÌ  Àúÿÿä  äúÿÿ         zR |           ¤ÿÿ,    Hž          <   °ÿÿ              T    ÿÿH    Lž          t   Èÿÿ              Œ   ¼ÿÿ              ¤   ¸ÿÿ              ¼   ¼ÿÿ              Ô   Àÿÿ              ì   Àÿÿ                Äÿÿ                 Ìÿÿ              4  Èÿÿ              L  Äÿÿ              d  Àÿÿ               |  Èÿÿ              ”  Äÿÿ              ¬  Äÿÿô    Pž“”•        zPLR |œe       ,   $   xŽÿÿx        Tž“”•
–            ÀÿÿÈ    Pž“”      <  Ø‘ÿÿp    Lž“        \  (’ÿÿ    Lž“”   $   ´   ˜’ÿÿ$   £      Hž       $   ¤   ’ÿÿ¼    Tž“”•
œ       $   Ì  |”ÿÿÀ    Tž“”•
–—˜   $   ô  •ÿÿ„    Pž“”•
–            ä’ÿÿ`    Lž“        <  „“ÿÿP    Lž“        \  ´“ÿÿ8    Lž“     $   |  •ÿÿð    Pž“”•
–       $   ¤  Ø•ÿÿ    Tž“”•
œ          Ì  @–ÿÿP    Lž“     $   ì  p–ÿÿð    Tž“”•
–—          8—ÿÿx    Lž“     $   4  —ÿÿ€    Tž“”•
–—        \  è—ÿÿT    Pž“”•    |  ˜ÿÿ¤    Pž“”• $   Ô   ˜ÿÿ$   —      Hž       $   Ä  È˜ÿÿ    Tž“”•
œ       ,   ì  0™ÿÿ„   \ž“”•
–—˜™š       $     $œÿÿ¸    Tž“”•
–—        D  ´œÿÿÜ    Lž“     $   d  pÿÿÔ    Pž“”•
–          Œ  žÿÿt    Lž“”      ¬  pžÿÿT    Pž“”•    Ì  ¤žÿÿ0    Lž“     $   $  ´žÿÿ   [      Lž“”        ,Ÿÿÿ0   Pž“”•    4  < ÿÿÀ    Pž“”      T  Ü ÿÿL    Lž“”   ,   t  Ô¡ÿÿø    Xž“”•
–—˜™š       $   ¤  d¥ÿÿ¼    Xž“”•
–—˜™ $   Ì  ø¥ÿÿ   Pž“”•
–          ô  H§ÿÿ4    Hž            Œ§ÿÿ              ,  D­ÿÿ           $   D  8­ÿÿè    Tž“”•
–          l  ø­ÿÿ0    Lž“     ,   Ä  Ð®ÿÿ`  Ó      Xž“”•
–—     $   ¼   °ÿÿœ    Pž“”•
–          ä  t°ÿÿ<    Pž            °ÿÿL    Lž“     $   $  ¼°ÿÿ     Tž“”•
–—˜   $   L  4±ÿÿx    Pž“”•
–       ,   t  „±ÿÿL   \ž“”•
–—˜™š          ¤   ²ÿÿH    Lž“”   $   Ä  È²ÿÿ   Tž“”•
–—     $   ì  ´³ÿÿ   Tž“”•
–—˜      	  ¤´ÿÿÀ    Pž“”•    4	  Dµÿÿ0    Lž       $   T	  TµÿÿÐ    Tž“”•
–—˜      |	  üµÿÿP    Lž          œ	  ,¶ÿÿ4    Pž          ¼	  `¶ÿÿ4    Lž“        Ü	  „¶ÿÿ              ô	  x¶ÿÿ8    Lž“            zR x           x¶ÿÿ(              4   ˆ¶ÿÿ@           4   L   °¶ÿÿð    A@žAA“”C•–sÔÓAÖÕAÞÝ           „   h·ÿÿ          $   œ   d¸ÿÿ<    BžAKÞÝ        $   Ä   x¸ÿÿX    AžBRÞÝ        4   ì   ¨¸ÿÿ   A@žAA•–C“”zÔÓAÖÕAÞÝ           $  |¹ÿÿ           $   <  l¹ÿÿX    AžBRÞÝ           d  œ¹ÿÿ              |  Œ¹ÿÿ              ”  ˆ¹ÿÿ              ¬  x¹ÿÿ              Ä  h¹ÿÿ           $   Ü  X¹ÿÿ(    A0žBEÞÝ             X¹ÿÿ                H¹ÿÿ           <   4  8¹ÿÿx    AžDP
ÞÝ AA
ÞÝ AA
ÞÝ ACÞÝ     L   t  p¹ÿÿø   AžAE•–—˜™
š	›œ“”0AÔÓAÖÕAØ×AÚÙAÜÛAÞÝ       D   Ä  ¾ÿÿ$   AžBA“”H•–—˜™
š	›÷ÔÓAÖÕAØ×AÚÙAÛAÞÝ    T     ôÁÿÿ´   A°BRžQAA›HœGC•N–M—L˜KD“P”O™JšIYÔÓAÖÕBØ×AÚÙAÜÛAÞÝ°AÐ     \   d  PÇÿÿ   AàBÀˆž‡AB“†”…•„–ƒD—‚˜D™€š›~œ}nÖÕAÔÓAØ×AÚÙAÜÛAÞÝàA€     D   Ä  üÈÿÿÜ    AAÐÚžÙAB“Ø”×•Ö–ÕB—Ôk×AÔÓAÖÕAÞÝAÐ  ,     ÉÿÿL    A žAA“”NÔÓAÞÝ        L   <  ¬ÉÿÿÀ    A€AÀØž×AC“Ö”Õ•Ô–Ó—Ò˜ÑeÔÓAÖÕAØ×AÞÝ€AÀ       T   Œ  Êÿÿì    A€AÐÚžÙAA—Ô˜ÓE•Ö–Õ™ÒšÑ“Ø”×lÔÓAÖÕAØ×AÚÙAÞÝ€A°     <   ä  °Êÿÿô    AÀBðÎžÍAA“Ì”ËB•ÊqÔÓAÕAÞÝÀB      $  dËÿÿ           ¤   <  PËÿÿx   AAÐÚžÙAB•Ò–ÑHÊIÉGJÈKÇ€Ø×LÆMÅ‚ÖƒÕNÄOÃ“Ô”ÓG—Ð˜Ï™ÎšÍ›ÌœË|ÃÂAIHAÔÓAKJAÖÕAMLAØ×AONAÚÙAÜÛAÁÀAÞÝA      ¤   ä   Ìÿÿô    AAÐŠž‰AB“„”ƒHúIùHJøK÷‚†ƒ…LöMõ€ˆ‡NôOó•‚–—€˜ÿI™þšý›üœûXÃÂAIHAÔÓAKJAÖÕAMLAØ×AONAÚÙAÜÛAÁÀAÞÝA      ¤   Œ  lÌÿÿü    AAÐŠž‰AB“„”ƒHúIùFJøK÷€ˆ‡LöMõ‚†ƒ…NôOóC•‚–E—€˜ÿ™þšý›üœû]IHAÁÀAKJAÃÂAMLAÔÓAONAÖÕAØ×AÚÙAÜÛAÞÝA     ¤   4  ÀÌÿÿø    AAÐŠž‰ACHúIù€ˆ‡JøK÷ILöMõ‚†ƒ…NôOó•‚–“„”ƒ—€˜ÿ™þšý›üœû`ÃÂAIHAÔÓAKJAÖÕAMLAØ×AONAÚÙAÜÛAÁÀAÞÝA       $   Ü  Íÿÿ$    AžAFÞÝ        ”     ÍÿÿÐ    AÀBàÜžÛAB“Ú”ÙHÐIÏFJÎKÍ•Ø–×LÌMË—Ö˜ÕNÊOÉG™ÔšÓ›ÒœÑVÔÓAIHAÖÕAKJAØ×AMLAÚÙAONAÜÛAÞÝÀA           œ  DÍÿÿ(              ´  TÍÿÿ@              Ì  |Íÿÿ           <   ä  €ÍÿÿÀ    A`žAA“
”	D™—˜•–dÔÓAÖÕAØ×AÙAÞÝ     <   $	   Îÿÿ´    AP
ž	AA“”D•–—˜™aÔÓAÖÕAØ×AÙAÞÝ     $   d	  tÎÿÿ`    AžCSÞÝ        $   Œ	  ¬Îÿÿ`    AžCSÞÝ        $   ´	  äÎÿÿ`    AžCSÞÝ        4   Ü	  Ïÿÿð    A@žAA“”C•–sÔÓAÖÕAÞÝ        4   
  ÔÏÿÿ    AP
ž	AA“”C—E•–O×CÔÓDÖÕBÞÝ   ,   L
  ,Ðÿÿü    A@žAA“”zÔÓAÞÝ        D   |
  øÐÿÿ`   ApžAA•
–	D“”E—˜™š›œFÔÓAÖÕAØ×AÚÙAÜÛAÞÝ    Ä
  Òÿÿ           <   Ü
  Òÿÿ   AžBC“”•–—˜ýÔÓAÖÕAØ×AÞÝ        4     äÕÿÿœ    A@žAA•–C“”ZÔÓDÖÕBÞÝ        D   T  HÖÿÿ`   ApžAD“”—˜™š•
–	B›JÔÓAÖÕAØ×AÚÙAÛAÞÝ     D   œ  `×ÿÿ4   ApžAA•
–	D—˜E›™š“”{ÔÓAÖÕAØ×AÚÙAÛAÞÝ     L   ä  LØÿÿp   AžAA•–F“”—˜™
š	›œMÔÓAÖÕAØ×AÚÙAÜÛAÞÝ        <   4  lÝÿÿœ    A0žAB“”•[
ÔÓAÕBÞÝ AAÔÓAÕAÞÝ         t  ÈÝÿÿ           4   Œ  ¼Ýÿÿ@    A žAB“G
ÓAÞÝ AAÓAÞÝ        4   Ä  ÄÝÿÿ”    A0žBA“”[
ÔÓAÞÝ BAÔÓAÞÝ       ü   Þÿÿ           $     Þÿÿ,    A žAA“FÓAÞÝ   ,   <  Þÿÿ    A0žAB“”•–yÔÓAÖÕAÞÝ    l  èÞÿÿ           ,   „  ÔÞÿÿ$    AžAD
ÞÝ AAÞÝ       <   ´  ÈÞÿÿÄ   A€žAA•–C™—
˜	E“”aÙAÔÓAÖÕAØ×AÞÝ  ÿÿ( ø  øä Œ¤  °Ü ÀØ ¨à ¸@  ÿœ           ÿœ           ÿœ	LŒT<           ÿÿ¼tØ °0                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               ÿÿÿÿÿÿÿÿ        ÿÿÿÿÿÿÿÿ        ÿÿÿÿÿÿÿÿ                Øª      +      $+       (      )      H+      ˆ)      *      \*              _‡      ðª             t‡              P«     ô2      ø2      ˜0      l1      ü2      à1      42      d2              ˆ      ðª             ¸«     ô2      ÜM      àM      N      N      $N      ,N      0N              žˆ      ðª                          ¥              ­              Æ               Xª     !                     hª                          xª                   õþÿo    è             @
             P      
       þ                                          ð­            ø                           ˜                          €      	                            ûÿÿo           þÿÿo    Ø      ÿÿÿo           ðÿÿo    >      ùÿÿo    )                                                                                                                                                                                                                                                                                                                                                                                                                                             Ð«      «             hª     x´     4      Xª     xª             ˆª             ÿÿÿÿÿÿÿÿAndroid (5220042 based on r346389c) clang version 8.0.7 (https://android.googlesource.com/toolchain/clang b55f2d4ebfd35bf643d27dbca1bb228957008617) (https://android.googlesource.com/toolchain/llvm 3c393fe7a7e13b0fba4ac75a01aa683d7a5b11cd) (based on LLVM 8.0.7svn) GCC: (GNU) 4.9.x 20150123 (prerelease)  .shstrtab .interp .note.android.ident .gnu.hash .dynsym .dynstr .gnu.version .gnu.version_r .rela.dyn .rela.plt .text .rodata .eh_frame_hdr .eh_frame .gcc_except_table .preinit_array .init_array .fini_array .data.rel.ro .dynamic .got .data .bss .comment                                                                                8      8                                                 P      P      ˜                              '   öÿÿo       è      è      d                             1             P      P      ð                          9             @
      @
      þ                             A   ÿÿÿo       >      >      ”                            N   þÿÿo       Ø      Ø      @                            ]                         €                           g      B       ˜      ˜      ø                          l                         p                            q                           k                             w             †      †      Q                                          l‰      l‰      T                                          À      À                                    —             À¥      À¥      |                              ©             Xª     Xª                                   ¸             hª     hª                                   Ä             xª     xª                                   Ð             ˆª     ˆª      H                             Ý             Ð«     Ð«                                  æ             ð­     ð­                                  ë              °      °                                    ñ             °     °      À                             ö      0               °      /                                                  ?±      ÿ                              
