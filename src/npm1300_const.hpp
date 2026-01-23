#pragma once

#include "zephyr/sys/util_macro.h"
#include <cstdint>

namespace alc::npm1300::common
{
  // Reg Access Status.
  enum class RegAccess {
    Read,
    Write,
    ReadWrite
  };

  // General.
  constexpr static uint8_t M_BIT_0_MASK { BIT(0) };
  constexpr static uint8_t M_BIT_1_MASK { BIT(1) };
  constexpr static uint8_t M_BIT_2_MASK { BIT(2) };
  constexpr static uint8_t M_BIT_3_MASK { BIT(3) };
  constexpr static uint8_t M_BIT_4_MASK { BIT(4) };
  constexpr static uint8_t M_BIT_5_MASK { BIT(5) };
  constexpr static uint8_t M_BIT_6_MASK { BIT(6) };
  constexpr static uint8_t M_BIT_7_MASK { BIT(7) };

  // nPM1300 CHARGER.BCHGCHARGESTATUS register bitmasks.
  constexpr static uint32_t M_CHG_STATUS_COMPLETE_MASK { BIT(1) };
  constexpr static uint32_t M_CHG_STATUS_TRICKLE_MASK { BIT(2) };
  constexpr static uint32_t M_CHG_STATUS_CC_MASK { BIT(3) };
  constexpr static uint32_t M_CHG_STATUS_CV_MASK { BIT(4) };

  // Main Registers (0x00).
  constexpr static uint8_t M_MAIN_BASE { 0x00 };

  constexpr static uint8_t M_TASKSWRESET_OFFSET { 0x01 };
  constexpr static uint8_t M_EVENTSADCSET_OFFSET { 0x02 };
  constexpr static uint8_t M_EVENTSADCCLR_OFFSET { 0x03 };
  constexpr static uint8_t M_INTENEVENTSADCSET_OFFSET { 0x04 };
  constexpr static uint8_t M_INTENEVENTSADCCLR_OFFSET { 0x05 };
  constexpr static uint8_t M_EVENTSBCHARGER0SET_OFFSET { 0x06 };
  constexpr static uint8_t M_EVENTSBCHARGER0CLR_OFFSET { 0x07 };
  constexpr static uint8_t M_INTENEVENTSBCHARGER0SET_OFFSET { 0x08 };
  constexpr static uint8_t M_INTENEVENTSBCHARGER0CLR_OFFSET { 0x09 };
  constexpr static uint8_t M_EVENTSBCHARGER1SET_OFFSET { 0x0A };
  constexpr static uint8_t M_EVENTSBCHARGER1CLR_OFFSET { 0x0B };
  constexpr static uint8_t M_INTENEVENTSBCHARGER1SET_OFFSET { 0x0C };
  constexpr static uint8_t M_INTENEVENTSBCHARGER1CLR_OFFSET { 0x0D };
  constexpr static uint8_t M_EVENTSBCHARGER2SET_OFFSET { 0x0E };
  constexpr static uint8_t M_EVENTSBCHARGER2CLR_OFFSET { 0x0F };
  constexpr static uint8_t M_INTENEVENTSBCHARGER2SET_OFFSET { 0x10 };
  constexpr static uint8_t M_INTENEVENTSBCHARGER2CLR_OFFSET { 0x11 };
  constexpr static uint8_t M_EVENTSSHPHLDSET_OFFSET { 0x12 };
  constexpr static uint8_t M_EVENTSSHPHLDCLR_OFFSET { 0x13 };
  constexpr static uint8_t M_INTENEVENTSSHPHLDSET_OFFSET { 0x14 };
  constexpr static uint8_t M_INTENEVENTSSHPHLDCLR_OFFSET { 0x15 };
  constexpr static uint8_t M_EVENTSVBUSIN0SET_OFFSET { 0x16 };
  constexpr static uint8_t M_EVENTSVBUSIN0CLR_OFFSET { 0x17 };
  constexpr static uint8_t M_INTENEVENTSVBUSIN0SET_OFFSET { 0x18 };
  constexpr static uint8_t M_INTENEVENTSVBUSIN0CLR_OFFSET { 0x19 };
  constexpr static uint8_t M_EVENTSVBUSIN1SET_OFFSET { 0x1A };
  constexpr static uint8_t M_EVENTSVBUSIN1CLR_OFFSET { 0x1B };
  constexpr static uint8_t M_INTENEVENTSVBUSIN1SET_OFFSET { 0x1C };
  constexpr static uint8_t M_INTENEVENTSVBUSIN1CLR_OFFSET { 0x1D };
  constexpr static uint8_t M_EVENTSGPIOSET_OFFSET { 0x22 };
  constexpr static uint8_t M_EVENTSGPIOCLR_OFFSET { 0x23 };
  constexpr static uint8_t M_INTENEVENTSGPIOSET_OFFSET { 0x24 };
  constexpr static uint8_t M_INTENEVENTSGPIOCLR_OFFSET { 0x25 };

  // VBUS Registers (0x02).
  constexpr static uint8_t M_VBUSIN_BASE { 0x02 };

  constexpr static uint8_t M_TASKUPDATEILIMSW_OFFSET { 0x00 };
  constexpr static uint8_t M_VBUSINILIM0_OFFSET { 0x01 };
  constexpr static uint8_t M_VBUSINILIMSTARTUP_OFFSET { 0x02 };
  constexpr static uint8_t M_VBUSSUSPEND_OFFSET { 0x03 };
  constexpr static uint8_t M_USBDETECTSTATUS_OFFSET { 0x05 };
  constexpr static uint8_t M_VBUSINSTATUS_OFFSET { 0x07 };

  // Battery Charging Registers (0x03).
  constexpr static uint8_t M_BCHARGER_BASE { 0x03 };

  constexpr static uint8_t M_TASKRELEASEERR_OFFSET { 0x00 };
  constexpr static uint8_t M_TASKCLEARCHGERR_OFFSET { 0x01 };
  constexpr static uint8_t M_TASKCLEARSAFETYTIMER_OFFSET { 0x02 };
  constexpr static uint8_t M_BCHGENABLESET_OFFSET { 0x04 };
  constexpr static uint8_t M_BCHGENABLECLR_OFFSET { 0x05 };
  constexpr static uint8_t M_BCHGDISABLESET_OFFSET { 0x06 };
  constexpr static uint8_t M_BCHGDISABLECLR_OFFSET { 0x07 };
  constexpr static uint8_t M_BCHGISETMSB_OFFSET { 0x08 };
  constexpr static uint8_t M_BCHGISETLSB_OFFSET { 0x09 };
  constexpr static uint8_t M_BCHGISETDISCHARGEMSB_OFFSET { 0x0A };
  constexpr static uint8_t M_BCHGISETDISCHARGELSB_OFFSET { 0x0B };
  constexpr static uint8_t M_BCHGVTERM_OFFSET { 0x0C };
  constexpr static uint8_t M_BCHGVTERMR_OFFSET { 0x0D };
  constexpr static uint8_t M_BCHGVTRICKLESEL_OFFSET { 0x0E };
  constexpr static uint8_t M_BCHGITERMSEL_OFFSET { 0x0F };
  constexpr static uint8_t M_NTCCOLD_OFFSET { 0x10 };
  constexpr static uint8_t M_NTCCOLDLSB_OFFSET { 0x11 };
  constexpr static uint8_t M_NTCCOOL_OFFSET { 0x12 };
  constexpr static uint8_t M_NTCCOOLLSB_OFFSET { 0x13 };
  constexpr static uint8_t M_NTCWARM_OFFSET { 0x14 };
  constexpr static uint8_t M_NTCWARMLSB_OFFSET { 0x15 };
  constexpr static uint8_t M_NTCHOT_OFFSET { 0x16 };
  constexpr static uint8_t M_NTCHOTLSB_OFFSET { 0x17 };
  constexpr static uint8_t M_DIETEMPSTOP_OFFSET { 0x18 };
  constexpr static uint8_t M_DIETEMPSTOPLSB_OFFSET { 0x19 };
  constexpr static uint8_t M_DIETEMPRESUME_OFFSET { 0x1A };
  constexpr static uint8_t M_DIETEMPRESUMELSB_OFFSET { 0x1B };
  constexpr static uint8_t M_BCHGILIMSTATUS_OFFSET { 0x2D };
  constexpr static uint8_t M_NTCSTATUS_OFFSET { 0x32 };
  constexpr static uint8_t M_DIETEMPSTATUS_OFFSET { 0x33 };
  constexpr static uint8_t M_BCHGCHARGESTATUS_OFFSET { 0x34 };
  constexpr static uint8_t M_BCHGERRREASON_OFFSET { 0x36 };
  constexpr static uint8_t M_BCHGERRSENSOR_OFFSET { 0x37 };
  constexpr static uint8_t M_BCHGCONFIG_OFFSET { 0x3C };
  constexpr static uint8_t M_BCHGBATLOWCHARGE_OFFSET { 0x50 };

  // Buck Registers (0x04).
  constexpr static uint8_t M_BUCK_BASE { 0x04 };

  constexpr static uint8_t M_BUCK1_ENASET_OFFSET { 0x00 };
  constexpr static uint8_t M_BUCK1_ENACLR_OFFSET { 0x01 };
  constexpr static uint8_t M_BUCK2_ENASET_OFFSET { 0x02 };
  constexpr static uint8_t M_BUCK2_ENACLR_OFFSET { 0x03 };
  constexpr static uint8_t M_BUCK1_PWMSET_OFFSET { 0x04 };
  constexpr static uint8_t M_BUCK1_PWMCLR_OFFSET { 0x05 };
  constexpr static uint8_t M_BUCK2_PWMSET_OFFSET { 0x06 };
  constexpr static uint8_t M_BUCK2_PWMCLR_OFFSET { 0x07 };
  constexpr static uint8_t M_BUCK1_NORMVOUT_OFFSET { 0x08 };
  constexpr static uint8_t M_BUCK1_RETVOUT_OFFSET { 0x09 };
  constexpr static uint8_t M_BUCK2_NORMVOUT_OFFSET { 0x0A };
  constexpr static uint8_t M_BUCK2_RETVOUT_OFFSET { 0x0B };
  constexpr static uint8_t M_BUCKENCTRL_OFFSET { 0x0C };
  constexpr static uint8_t M_BUCKVRETCTRL_OFFSET { 0x0D };
  constexpr static uint8_t M_BUCKPWMCTRL_OFFSET { 0x0E };
  constexpr static uint8_t M_BUCKSWCTRLSEL_OFFSET { 0x0F };
  constexpr static uint8_t M_BUCK1_VOUTSTATUS_OFFSET { 0x10 };
  constexpr static uint8_t M_BUCK2_VOUTSTATUS_OFFSET { 0x11 };
  constexpr static uint8_t M_BUCKCTRL0_OFFSET { 0x15 };
  constexpr static uint8_t M_BUCKSTATUS_OFFSET { 0x34 };

  // ADC Registers (0x05).
  constexpr static uint8_t M_ADC_BASE { 0x05 };

  constexpr static uint8_t M_TASKVBATMEASURE_OFFSET { 0x00 };
  constexpr static uint8_t M_TASKNTCMEASURE_OFFSET { 0x01 };
  constexpr static uint8_t M_TASKTEMPMEASURE_OFFSET { 0x02 };
  constexpr static uint8_t M_TASKVSYSMEASURE_OFFSET { 0x03 };
  constexpr static uint8_t M_TASKIBATMEASURE_OFFSET { 0x06 };
  constexpr static uint8_t M_TASKVBUSMEASURE_OFFSET { 0x07 };
  constexpr static uint8_t M_TASKDELAYEDVBATMEASURE_OFFSET { 0x08 };
  constexpr static uint8_t M_ADCCONFIG_OFFSET { 0x09 };
  constexpr static uint8_t M_ADCNTCRSEL_OFFSET { 0x0A };
  constexpr static uint8_t M_ADCAUTOTIMCONF_OFFSET { 0x0B };
  constexpr static uint8_t M_TASKAUTOTIMUPDATE_OFFSET { 0x0C };
  constexpr static uint8_t M_ADCDELTIMCONF_OFFSET { 0x0D };
  constexpr static uint8_t M_ADCIBATMEASSTATUS_OFFSET { 0x10 };
  constexpr static uint8_t M_ADCVBATRESULTMSB_OFFSET { 0x11 };
  constexpr static uint8_t M_ADCNTCRESULTMSB_OFFSET { 0x12 };
  constexpr static uint8_t M_ADCTEMPRESULTMSB_OFFSET { 0x13 };
  constexpr static uint8_t M_ADCVSYSRESULTMSB_OFFSET { 0x14 };
  constexpr static uint8_t M_ADCGP0RESULTLSBS_OFFSET { 0x15 };
  constexpr static uint8_t M_ADCVBAT0RESULTMSB_OFFSET { 0x16 };
  constexpr static uint8_t M_ADCVBAT1RESULTMSB_OFFSET { 0x17 };
  constexpr static uint8_t M_ADCVBAT2RESULTMSB_OFFSET { 0x18 };
  constexpr static uint8_t M_ADCVBAT3RESULTMSB_OFFSET { 0x19 };
  constexpr static uint8_t M_ADCGP1RESULTLSBS_OFFSET { 0x1A };
  constexpr static uint8_t M_ADCIBATMEASEN_OFFSET { 0x24 };

  // GPIO Registers (0x06).
  constexpr static uint8_t M_GPIOS_BASE { 0x06 };

  constexpr static uint8_t M_GPIOMODE0_OFFSET { 0x00 };
  constexpr static uint8_t M_GPIOMODE1_OFFSET { 0x01 };
  constexpr static uint8_t M_GPIOMODE2_OFFSET { 0x02 };
  constexpr static uint8_t M_GPIOMODE3_OFFSET { 0x03 };
  constexpr static uint8_t M_GPIOMODE4_OFFSET { 0x04 };
  constexpr static uint8_t M_GPIODRIVE0_OFFSET { 0x05 };
  constexpr static uint8_t M_GPIODRIVE1_OFFSET { 0x06 };
  constexpr static uint8_t M_GPIODRIVE2_OFFSET { 0x07 };
  constexpr static uint8_t M_GPIODRIVE3_OFFSET { 0x08 };
  constexpr static uint8_t M_GPIODRIVE4_OFFSET { 0x09 };
  constexpr static uint8_t M_GPIOPUEN0_OFFSET { 0x0A };
  constexpr static uint8_t M_GPIOPUEN1_OFFSET { 0x0B };
  constexpr static uint8_t M_GPIOPUEN2_OFFSET { 0x0C };
  constexpr static uint8_t M_GPIOPUEN3_OFFSET { 0x0D };
  constexpr static uint8_t M_GPIOPUEN4_OFFSET { 0x0E };
  constexpr static uint8_t M_GPIOPDEN0_OFFSET { 0x0F };
  constexpr static uint8_t M_GPIOPDEN1_OFFSET { 0x10 };
  constexpr static uint8_t M_GPIOPDEN2_OFFSET { 0x11 };
  constexpr static uint8_t M_GPIOPDEN3_OFFSET { 0x12 };
  constexpr static uint8_t M_GPIOPDEN4_OFFSET { 0x13 };
  constexpr static uint8_t M_GPIOOPENDRAIN0_OFFSET { 0x14 };
  constexpr static uint8_t M_GPIOOPENDRAIN1_OFFSET { 0x15 };
  constexpr static uint8_t M_GPIOOPENDRAIN2_OFFSET { 0x16 };
  constexpr static uint8_t M_GPIOOPENDRAIN3_OFFSET { 0x17 };
  constexpr static uint8_t M_GPIOOPENDRAIN4_OFFSET { 0x18 };
  constexpr static uint8_t M_GPIODEBOUNCE0_OFFSET { 0x19 };
  constexpr static uint8_t M_GPIODEBOUNCE1_OFFSET { 0x1A };
  constexpr static uint8_t M_GPIODEBOUNCE2_OFFSET { 0x1B };
  constexpr static uint8_t M_GPIODEBOUNCE3_OFFSET { 0x1C };
  constexpr static uint8_t M_GPIODEBOUNCE4_OFFSET { 0x1D };
  constexpr static uint8_t M_GPIOSTATUS_OFFSET { 0x1E };

  // Timer Registers (0x07).
  constexpr static uint8_t M_TIMER_BASE { 0x07 };

  constexpr static uint8_t M_TIMERSET_OFFSET { 0x00 };
  constexpr static uint8_t M_TIMERCLR_OFFSET { 0x01 };
  constexpr static uint8_t M_TIMERTARGETSTROBE_OFFSET { 0x03 };
  constexpr static uint8_t M_WATCHDOGKICK_OFFSET { 0x04 };
  constexpr static uint8_t M_TIMERCONFIG_OFFSET { 0x05 };
  constexpr static uint8_t M_TIMERSTATUS_OFFSET { 0x06 };
  constexpr static uint8_t M_TIMERHIBYTE_OFFSET { 0x08 };
  constexpr static uint8_t M_TIMERMIDBYTE_OFFSET { 0x09 };
  constexpr static uint8_t M_TIMERLOBYTE_OFFSET { 0x0A };

  // Load Switch Registers (0x08).
  constexpr static uint8_t M_LDSW_BASE { 0x08 };

  constexpr static uint8_t M_TASKLDSW1SET_OFFSET { 0x00 };
  constexpr static uint8_t M_TASKLDSW1CLR_OFFSET { 0x01 };
  constexpr static uint8_t M_TASKLDSW2SET_OFFSET { 0x02 };
  constexpr static uint8_t M_TASKLDSW2CLR_OFFSET { 0x03 };
  constexpr static uint8_t M_LDSWSTATUS_OFFSET { 0x04 };
  constexpr static uint8_t M_LDSW1GPISEL_OFFSET { 0x05 };
  constexpr static uint8_t M_LDSW2GPISEL_OFFSET { 0x06 };
  constexpr static uint8_t M_LDSWCONFIG_OFFSET { 0x07 };
  constexpr static uint8_t M_LDSW1LDOSEL_OFFSET { 0x08 };
  constexpr static uint8_t M_LDSW2LDOSEL_OFFSET { 0x09 };
  constexpr static uint8_t M_LDSW1VOUTSEL_OFFSET { 0x0C };
  constexpr static uint8_t M_LDSW2VOUTSEL_OFFSET { 0x0D };

  // POF Registers (0x09).
  constexpr static uint8_t M_POF_BASE { 0x09 };

  constexpr static uint8_t M_POFCONFIG_OFFSET { 0x00 };

  // LED Driver Registers (0x0A).
  constexpr static uint8_t M_LEDDRV_BASE { 0x0A };

  constexpr static uint8_t M_LEDDRV0MODESEL_OFFSET { 0x00 };
  constexpr static uint8_t M_LEDDRV1MODESEL_OFFSET { 0x01 };
  constexpr static uint8_t M_LEDDRV2MODESEL_OFFSET { 0x02 };
  constexpr static uint8_t M_LEDDRV0SET_OFFSET { 0x03 };
  constexpr static uint8_t M_LEDDRV0CLR_OFFSET { 0x04 };
  constexpr static uint8_t M_LEDDRV1SET_OFFSET { 0x05 };
  constexpr static uint8_t M_LEDDRV1CLR_OFFSET { 0x06 };
  constexpr static uint8_t M_LEDDRV2SET_OFFSET { 0x07 };
  constexpr static uint8_t M_LEDDRV2CLR_OFFSET { 0x08 };

  // Ship Registers (0x0B).
  constexpr static uint8_t M_SHIP_BASE { 0x0B };
  
  constexpr static uint8_t M_TASKENTERHIBERNATE_OFFSET { 0x00 };
  constexpr static uint8_t M_TASKSHPHLDCFGSTROBE_OFFSET { 0x01 };
  constexpr static uint8_t M_TASKENTERSHIPMODE_OFFSET { 0x02 };
  constexpr static uint8_t M_TASKRESETCFG_OFFSET { 0x03 };
  constexpr static uint8_t M_SHPHLDCONFIG_OFFSET { 0x04 };
  constexpr static uint8_t M_SHPHLDSTATUS_OFFSET { 0x05 };
  constexpr static uint8_t M_LPRESETCONFIG_OFFSET { 0x06 };

  // Reset and Error Registers (0x0E).
  constexpr static uint8_t M_ERRLOG_BASE { 0x0E };

  constexpr static uint8_t M_TASKCLRERRLOG_OFFSET { 0x00 };
  constexpr static uint8_t M_SCRATCH0_OFFSET { 0x01 };
  constexpr static uint8_t M_SCRATCH1_OFFSET { 0x02 };
  constexpr static uint8_t M_RSTCAUSE_OFFSET { 0x03 };
  constexpr static uint8_t M_CHARGERERRREASON_OFFSET { 0x04 };
  constexpr static uint8_t M_CHARGERERRSENSOR_OFFSET { 0x05 };
}
