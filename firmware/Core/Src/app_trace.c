#include "app_trace.h"

#if USE_SYSVIEW

/* Olay aciklamasi (SystemView kullanici kilavuzu UM08027, "Registering the
 * module"): "M=<modul>, <no> <olay> <parametreler>, ...". Hedef bu metni en
 * fazla SEGGER_SYSVIEW_MAX_STRING_LEN (128) karakter gonderir; fazlasi
 * kesilir, bu yuzden adlar kisa tutuldu. Sira app_trace.h'deki enum ile
 * ayni olmali. Parametreler kilavuzdaki gibi "ad=%u" bicimindedir (n = olay
 * kimligi); '#' kullanilmaz: SystemView aciklama sozdiziminde yorum
 * baslatir. */
#define APP_TRACE_MODULE_DESC                                                  \
    "M=BTN, 0 ACCEPT n=%u S=%u, 1 TASK n=%u, 2 READY n=%u, 3 ENQUEUE n=%u q=%u, " \
    "4 DROP n=%u at=%u, 5 TX_START n=%u, 6 TX_TC n=%u"

_Static_assert(sizeof(APP_TRACE_MODULE_DESC) - 1U <= SEGGER_SYSVIEW_MAX_STRING_LEN,
               "SystemView modul aciklamasi 128 karakteri asiyor, kesilir");

SEGGER_SYSVIEW_MODULE g_app_trace_module = {
    APP_TRACE_MODULE_DESC,
    APP_EV_COUNT,
    0,    /* EventOffset: SEGGER_SYSVIEW_RegisterModule() yazar */
    NULL, /* ek aciklama yok */
    NULL, /* pNext: SEGGER_SYSVIEW_RegisterModule() yazar */
};

void app_trace_register(void)
{
    SEGGER_SYSVIEW_RegisterModule(&g_app_trace_module);
}

void app_trace_window(char variant, uint8_t scenario)
{
    char text[] = "Pencere ?/S?";
    text[8] = variant;
    text[11] = (char)('0' + scenario);
    SEGGER_SYSVIEW_Print(text);
}

#endif /* USE_SYSVIEW */
