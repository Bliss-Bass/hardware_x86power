# x86power

Vendor hardware support for Android-x86 / Bliss OS / Bass OS on **passively cooled x86_64** devices.

This tree provides:

| Component | Module | Role |
|-----------|--------|------|
| **Power HAL** | `power.x86` | Legacy hw module (`power.c`). Offlines non-boot CPUs on screen off when enabled. |
| **Thermal HAL 2.0** | `android.hardware.thermal@2.0-service.x86power` | Reads real x86 thermal zones, reports temperature and severity to `ThermalManagerService`, and runs a separate throttler thread for local cpufreq / CPU hotplug mitigation. |

The stock AOSP thermal mock HAL reports fake temperatures (for example 27.8°C and `test cooling device`). x86power replaces that with readings from sensors such as `x86_pkg_temp`.

---

## Source tree setup

Check out this repository into your AOSP tree as:

```text
hardware/x86power/
```

If you use a different path, set `X86POWER_DIR` before inheriting the product makefile (see below).

The build is defined in `Android.bp` (Soong). No `Android.mk` is required.

---

## Including in a device build

### 1. Add the product makefile

In your device `*.mk` (for example `device/android_x86/x86_64/x86_64.mk`), add:

```makefile
$(call inherit-product, hardware/x86power/x86power.mk)
```

If the repo is not at `hardware/x86power`:

```makefile
X86POWER_DIR := vendor/foo/hardware/x86power
$(call inherit-product, $(X86POWER_DIR)/x86power.mk)
```

### 2. What `x86power.mk` does

`x86power.mk` is the single integration point. It:

- Adds **`power.x86`** and **`android.hardware.thermal@2.0-service.x86power`** to `PRODUCT_PACKAGES`
- Installs the thermal service **init.rc** via `PRODUCT_INIT_RC`
- Merges the thermal **VINTF manifest** via `DEVICE_MANIFEST_FILE`
- Sets **`ro.hardware.power=x86`** so the system loads `vendor/lib*/hw/power.x86.so`
- Ships default **`persist.x86power.thermal.*`** properties for passive-cooled devices
- **Removes** AOSP mock/default thermal HAL packages from the image so only one `IThermal/default` implementation is present

### 3. Build

From the top of your AOSP tree:

```bash
source build/envsetup.sh
lunch <your-target>
m power.x86 android.hardware.thermal@2.0-service.x86power
# or a full image build, e.g. m -j$(nproc)
```

---

## Verifying on a flashed device

### HAL registration

```bash
adb shell lshal | grep -i thermal
```

Expected (among others):

```text
android.hardware.thermal@2.0::IThermal/default
```

Confirm the thermal service process is running:

```bash
adb shell ps -A | grep x86power
```

### Thermal service output

```bash
adb shell dumpsys thermalservice
```

**Good signs:**

- `TCPU` reflects the real CPU temperature (not a fixed ~27.8°C)
- `HotThrottlingThresholds` shows real values (not mostly `NaN`)
- No `test cooling device` entry
- `Thermal Status` rises above `0` when the CPU is under load and hot

**Compare with kernel sensors:**

```bash
adb shell 'for z in /sys/class/thermal/thermal_zone*; do
  echo "$(basename $z): $(cat $z/type) = $(cat $z/temp)"
done'
```

By default the HAL uses the **`x86_pkg_temp`** zone for `TCPU`.

### Logs

```bash
adb logcat -s X86ThermalHAL X86ThermalHelper X86Throttler X86PowerConfig ThermalManagerService
```

---

## Runtime configuration

Most thermal behaviour is controlled at runtime with **`persist.x86power.thermal.*`** properties. These survive reboot. The HAL reloads them periodically (default: every 30 poll cycles).

Set properties with:

```bash
adb shell setprop persist.x86power.thermal.<name> <value>
```

Or add overrides in your device makefile using `PRODUCT_PRODUCT_PROPERTIES`.

### Core switches

| Property | Default | Description |
|----------|---------|-------------|
| `persist.x86power.thermal.enabled` | `true` | Master enable for thermal monitoring and framework reporting. |
| `persist.x86power.thermal.throttle_enabled` | `true` | Enable the local throttler thread (cpufreq cap / optional core offline). |
| `persist.x86power.thermal.poll_interval_ms` | `2000` | Monitor and throttler poll interval in milliseconds. |
| `persist.x86power.thermal.config_reload_polls` | `30` | Reload all `persist.x86power.thermal.*` props every N polls. |
| `persist.x86power.thermal.hysteresis_c` | `2.0` | Hysteresis when severity decreases (°C). Reduces flapping. |

### Sensor selection

| Property | Default | Description |
|----------|---------|-------------|
| `persist.x86power.thermal.primary_sensor` | `x86_pkg_temp` | Thermal zone **type** name to use for `TCPU`. Set to `max` to use the hottest non-ignored zone. |
| `persist.x86power.thermal.ignore_sensors` | `SEN3,acpitz,INT3400 Thermal,iwlwifi_1` | Comma-separated list of zone types to ignore. |

Example — use the hottest allowed sensor instead of a single zone:

```bash
adb shell setprop persist.x86power.thermal.primary_sensor max
```

### Severity thresholds (°C)

These map to Android `ThrottlingSeverity` levels reported to the framework.

| Property | Default | Severity |
|----------|---------|----------|
| `persist.x86power.thermal.threshold_light` | `60` | LIGHT |
| `persist.x86power.thermal.threshold_moderate` | `70` | MODERATE |
| `persist.x86power.thermal.threshold_severe` | `80` | SEVERE |
| `persist.x86power.thermal.threshold_critical` | `85` | CRITICAL |
| `persist.x86power.thermal.threshold_emergency` | `90` | EMERGENCY |
| `persist.x86power.thermal.threshold_shutdown` | `95` | SHUTDOWN |

Tune these per chassis. Passive fanless tablets often need lower thresholds than a desktop-class mini PC.

### Local throttling (Throttler thread)

When `throttle_enabled` is true, a **separate thread** (not Power HAL) applies mitigation via sysfs:

- Caps **`scaling_max_freq`** on all CPUs as a percentage of `cpuinfo_max_freq`
- Optionally offlines non-boot CPUs at higher severities

| Property | Default | Description |
|----------|---------|-------------|
| `persist.x86power.thermal.throttle_freq_light` | `100` | Max CPU freq % at LIGHT severity |
| `persist.x86power.thermal.throttle_freq_moderate` | `85` | Max CPU freq % at MODERATE |
| `persist.x86power.thermal.throttle_freq_severe` | `70` | Max CPU freq % at SEVERE |
| `persist.x86power.thermal.throttle_freq_critical` | `50` | Max CPU freq % at CRITICAL |
| `persist.x86power.thermal.throttle_freq_emergency` | `30` | Max CPU freq % at EMERGENCY |
| `persist.x86power.thermal.throttle_freq_shutdown` | `20` | Max CPU freq % at SHUTDOWN |
| `persist.x86power.thermal.throttle_offline_cores` | `critical` | Offline non-boot CPUs at this severity or above. Values: `none`, `severe`, `critical`, `emergency`. |

Example — more aggressive passive cooling:

```bash
adb shell setprop persist.x86power.thermal.threshold_moderate 65
adb shell setprop persist.x86power.thermal.throttle_freq_moderate 75
adb shell setprop persist.x86power.thermal.throttle_offline_cores severe
```

Props are picked up automatically within a few reload cycles; reboot is not required.

### Power HAL (screen on/off CPU hotplug)

Controlled separately from thermal props:

| Property | Default | Description |
|----------|---------|-------------|
| `power.nonboot-cpu-off` | `1` (via `x86power.mk`) | When `1`, non-boot CPUs are offlined while the screen is off. Set to `0` to disable. |

```bash
adb shell setprop power.nonboot-cpu-off 0
```

---

## Example: enable and tune after first boot

```bash
# Confirm the x86power HAL is active
adb shell dumpsys thermalservice

# Ensure monitoring and throttling are on (usually already set by x86power.mk)
adb shell setprop persist.x86power.thermal.enabled true
adb shell setprop persist.x86power.thermal.throttle_enabled true

# Point at CPU package sensor (default)
adb shell setprop persist.x86power.thermal.primary_sensor x86_pkg_temp

# Passive tablet tuning — adjust to your hardware
adb shell setprop persist.x86power.thermal.threshold_moderate 68
adb shell setprop persist.x86power.thermal.throttle_freq_moderate 80

# Watch severity and throttle actions
adb logcat -s X86ThermalHelper X86Throttler
```

---

## Troubleshooting

### Still seeing ~27.8°C or `test cooling device`

The AOSP mock thermal HAL is still on the image or winning VINTF registration.

- Confirm `x86power.mk` is inherited by your device target
- Check that mock packages were filtered: `android.hardware.thermal@2.0-service-mock`, etc.
- Verify only one thermal 2.0 service is installed under `/vendor/bin/hw/`

### `TCPU` does not match `x86_pkg_temp` in sysfs

- Check `persist.x86power.thermal.primary_sensor`
- Check that the zone is not listed in `ignore_sensors`
- Inspect available zones with the sysfs loop command above

### Throttling not applied (severity rises but freq unchanged)

- Confirm `persist.x86power.thermal.throttle_enabled` is `true`
- Check logcat for `X86Throttler` denials or write failures
- Verify cpufreq nodes exist:

  ```bash
  adb shell cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
  adb shell cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
  ```

- SELinux may block sysfs writes from the HAL on some builds; vendor sepolicy may need allow rules for the thermal service.

### Framework thermal status stays 0 despite high kernel temps

Usually means the wrong sensor is selected or the mock HAL is still active. Fix HAL registration and sensor props first; `ThermalManagerService` does not need to be modified.

---

## Project layout

```text
hardware/x86power/
  Android.bp          Soong build (power.x86 + thermal service)
  x86power.mk         Device integration makefile
  power.c             Legacy Power HAL
  thermal/
    service.cpp       Thermal HAL service entry point
    Thermal.*         HIDL IThermal 2.0 implementation
    ThermalHelper.*   Sensor monitor thread + framework callbacks
    Throttler.*       Local throttling thread
    X86PowerConfig.*  persist.x86power.* property parsing
    manifest.xml      VINTF fragment
    android.hardware.thermal@2.0-service.x86power.rc
```

---

## License

GPL-2.0 (Power HAL) / Apache-2.0 (Thermal HAL sources). See file headers for details.
