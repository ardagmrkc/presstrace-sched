/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************
*                                                                    *
*        SEGGER RTT * Real Time Transfer for embedded targets        *
*                  https://github.com/SEGGERMicro/RTT                *
*                                                                    *
**********************************************************************

---------------------------END-OF-HEADER------------------------------
Purpose : User configuration file for RTT.
          For available configuration,
          refer to SEGGER_RTT_ConfDefaults.h.

----------------------------------------------------------------------
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H


/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/
//
// PressTrace: IAR + Cortex-M4'te varsayilan RTT_USE_ASM 1'dir ve
// SEGGER_RTT_ASM_ARMv7M.S dosyasini gerektirir. Bunun yerine RTT'nin C
// gerceklemesi kullaniliyor (Cortex-M4'te bellek bariyeri gerekmez);
// projeye assembly dosyasi eklenmez.
//
#define RTT_USE_ASM                               0

#endif
/*************************** End of file ****************************/
