// power_mgmt.cpp - see power_mgmt.h.
#include "power_mgmt.h"

#include <Arduino.h>
#include <Preferences.h>
#include <LilyGoLib.h>   // instance, instance.pmu (XPowersAXP2101)

namespace {

const char *const NS = "arguspower";
bool s_longevity = false;

void apply_charge_target()
{
    // 4.1 V spares the cell (longevity); 4.2 V is full capacity (default).
    instance.pmu.setChargeTargetVoltage(
        s_longevity ? XPOWERS_AXP2101_CHG_VOL_4V1 : XPOWERS_AXP2101_CHG_VOL_4V2);
}

}  // namespace

void power_boot_config()
{
    Preferences p;
    if (p.begin(NS, true)) { s_longevity = p.getBool("longv", false); p.end(); }

    // Input VOLTAGE limit (VINDPM): keep VBUS at >= ~4.44 V. When a weak source
    // or a thin cable sags below this under load, the PMU throttles the input
    // current instead of browning out - graceful on any charger/wattage.
    instance.pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V44);

    // Input CURRENT ceiling: 900 mA is ample for this small cell plus the system
    // rail and safe for a typical 5 V / 1 A+ source; VINDPM covers weaker ones.
    instance.pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);

    // Deep-discharge protection: power the system down at 2.9 V so the cell is
    // never dragged to a damaging low at the bottom of a 100 -> 0% cycle.
    instance.pmu.setSysPowerDownVoltage(2900);

    apply_charge_target();
}

void power_set_longevity(bool on)
{
    s_longevity = on;
    apply_charge_target();
    Preferences p;
    if (p.begin(NS, false)) { p.putBool("longv", s_longevity); p.end(); }
}

bool power_get_longevity() { return s_longevity; }
