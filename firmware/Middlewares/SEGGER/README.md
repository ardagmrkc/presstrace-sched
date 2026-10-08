# SEGGER SystemView ve RTT hedef kaynakları

SystemView kaydı için hedef (kart) tarafı kaynaklar. Kayıt yolu: J-Link + RTT.

| Bileşen | Kaynak | Etiket | Commit |
|---|---|---|---|
| SystemView | https://github.com/SEGGERMicro/SystemView | V4.12.0 | `92ca7a810c5765ba64911919acd511c61b6b083f` |
| RTT | https://github.com/SEGGERMicro/RTT | V8.58.0 | `4d8feab3150f86f37a9d323ddc88d6cdf5673072` |

FreeRTOS entegrasyonu: `Sample/FreeRTOSV11` (V11'de çekirdek yaması gerekmez).
Lisans: her klasördeki `LICENSE.md` (SEGGER, BSD tarzı; telif notu korunmalı).

## Alınan dosyalar

- `SystemView/SEGGER/SEGGER.h`, `SystemView/SYSVIEW/*`: değiştirilmedi.
- `RTT/SEGGER_RTT.c/.h`, `RTT/SEGGER_RTT_ConfDefaults.h`: değiştirilmedi.
- `SystemView/Sample/FreeRTOSV11/SEGGER_SYSVIEW_FreeRTOS.h`: değiştirilmedi.
- `SystemView/Sample/FreeRTOSV11/SEGGER_SYSVIEW_FreeRTOS.c`: yalnızca
  `#if USE_SYSVIEW` koruması eklendi (izleme kapalı derlemede trace makroları
  yeniden tanımlanmasın).

## Projeye özel ayarlar (`Config/`)

| Dosya | Değişiklik |
|---|---|
| `SEGGER_SYSVIEW_Config_FreeRTOS.c` | Uygulama/cihaz adı, RAM tabanı `0x20000000` (SRAM), kesme adları (EXTI0, USART2, SysTick, PendSV, SVCall), DWT çevrim sayacının açılması, `app_trace` modül kaydı, `#if USE_SYSVIEW` koruması |
| `SEGGER_SYSVIEW_Conf.h` | `SEGGER_SYSVIEW_RTT_BUFFER_SIZE` 16 KB (varsayılan 1 KB) |
| `SEGGER_RTT_Conf.h` | `RTT_USE_ASM 0` (RTT'nin C gerçeklemesi; assembly dosyası eklenmedi) |
| `Global.h` | değiştirilmedi |

## Açma/kapama

`firmware/Core/Inc/FreeRTOSConfig.h` içindeki `USE_SYSVIEW`:

- `1`: izleme açık. Kart ACK'te sürümün sonuna `T` ekler (örnek `AT`), arayüz
  dosya adlarını buna göre verir (`AT_S5.csv`).
- `0`: izleme kodu hiç derlenmez (izleme maliyeti karşılaştırmasının
  "kapalı" tarafı).

Uygulama olayları (BTN ACCEPT … TX_TC): `firmware/Core/Inc/app_trace.h`.

## Kesme kayıtları

- Kesme girişi, işleyicinin içinde zaman damgası (t0 / t4) alındıktan **sonra**
  kaydedilir; izleme ölçüm noktalarını kaydırmaz.
- Bütün kesmeler kaydedilir; USART2'nin bayt başına gelen TXE kesmeleri de.
  S5'te bunlar saniyede ~6.400 kesme ediyor. RTT hattı yetişemezse SystemView
  olayları sessizce atmaz, izde **Overflow** olayı ve atılan olay sayısı görünür.
