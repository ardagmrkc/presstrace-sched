#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Hat bicimi (temel protokol): her mesaj ASCII metindir, bosluklarla 63
 * bayta tamamlanir ve 64. bayt LF'dir -> her mesaj tam 64 bayt. Metin 63
 * bayti asarsa mesaj KESILMEZ, hic gonderilmez (encode_error sayilir); asagidaki
 * bicimler en kotu durumda da 63 bayta sigacak sekilde secilmistir.
 *
 *   TEL,<seq>,S<n>,<temp_centi_c>,<temp_raw>,<vdda_mv>,<extra_us>,<period_us>,<txq>
 *   BTN,<event_id>,S<n>,PRESSED,<seq>,<t0>,<t1>,<t2>
 *   ACK,<seq>,S<n>,<surum>        surum: A, B ya da C; hizli yol aciksa sonuna F,
 *                                 SystemView izlemesi aciksa T, kisa ikili
 *                                 cerceve aciksa K (bu sirayla)
 *   REC,S<n>,<event_id>,<t0>,<t1-t0>,<t2-t1>,<t3-t2>,<t4-t3>,<status>
 *   CNT,S<n>,<ad>,<deger>
 *   END,S<n>,<gonderilen_REC_sayisi>
 *
 * TEL'deki period_us, bir onceki periyodun baslangicindan bu yana gecen
 * suredir; S6'da ayni periyotta uretilen 4 TEL ayni degeri tasir.
 *
 * REC'teki sureler bilesen sureleridir (analiz tablosunun bicimi); mutlak
 * zamanlar t0'dan toplanarak bulunur. Bilinmeyen ya da >= 1 s olan bir sure
 * BOS birakilir (0 yazilmaz). seq yalnizca TEL/BTN/ACK satirlarinda vardir ve
 * yalnizca onlar icin artar (kayip tespiti); REC/CNT/END yalnizca olcum
 * penceresi kapaninca gonderilir, sayilari END ile dogrulanir.
 *
 * Kisa ikili cerceve (PROTOCOL_COMPACT 1): olcum sirasinda hatta giden TEL ve
 * BTN, 64 bayt yerine kisa ikili cerceve olarak gonderilir. Hattaki bolunemez
 * birim kisalir: BTN'in onundeki satirlari bekleme suresi ~3,3 kat azalir.
 * ACK/REC/CNT/END 64 baytlik ASCII kalir; ACK'te surumun sonuna K eklenir.
 * Temel protokol 64 bayttir: karsilastirma olcumleri PROTOCOL_COMPACT 0 ile.
 *
 *   bayt 0      PROTOCOL_BIN_SYNC (0xA5; ASCII satirlarda hic gecmez)
 *   bayt 1      tur: 'T' (TEL) ya da 'B' (BTN)
 *   bayt 2..    alanlar, little-endian
 *   son bayt    CRC-8 (polinom 0x07, baslangic 0), bayt 0..n-2 uzerinden
 *
 *   TEL (19 bayt): seq u16, senaryo u8, temp_centi_c i16, temp_raw u16,
 *                  vdda_mv u16, extra_us u16 (65535'te doyar), period_us u32,
 *                  txq u8
 *   BTN (20 bayt): event_id u16, senaryo u8, seq u16, t0 u32, t1 u32, t2 u32
 */
#ifndef PROTOCOL_COMPACT
#define PROTOCOL_COMPACT 1
#endif

#define PROTOCOL_FRAME_SIZE 64U /* en uzun cerceve (ASCII satir) */
#define PROTOCOL_LINE_MAX   63U

#define PROTOCOL_BIN_SYNC     0xA5U
#define PROTOCOL_BIN_TYPE_TEL ((uint8_t)'T')
#define PROTOCOL_BIN_TYPE_BTN ((uint8_t)'B')
#define PROTOCOL_BIN_TEL_SIZE 19U
#define PROTOCOL_BIN_BTN_SIZE 20U

/*
 * Yer istasyonu -> kart komutu (5 bayt, ikili), yalnizca kullanici bir secim
 * yaptiginda gonderilir. 64 baytlik TEL/BTN kurali karttan cikan
 * mesajlar icindir; bu komut ayri bir kanaldir:
 *   AA 55 'C' arg checksum
 *   arg: 0..6 = senaryoyu degistir (yeni olcum penceresi),
 *        0xFE = olcumu bitir (kart S0'a gecer ve kayitlari doker),
 *        0xFF = yalnizca sorgula.
 *   AA 55 'V' arg checksum
 *   arg: 'A', 'B' ya da 'C' = surumu degistir (yeni olcum penceresi; senaryo
 *        ayni kalir).
 * checksum: bayt 0..3 toplaminin mod 256'si. Kart her gecerli komuta ACK ile
 * yanit verir.
 */
#define CMD_SYNC0          0xAAU
#define CMD_SYNC1          0x55U
#define CMD_FRAME_SIZE     5U
#define CMD_TYPE_SCENARIO  ((uint8_t)'C')
#define CMD_TYPE_VARIANT   ((uint8_t)'V')
#define CMD_ARG_DUMP       0xFEU
#define CMD_ARG_QUERY      0xFFU

#define EXPERIMENT_TIMEOUT_US 1000000U /* deney protokolu: zaman asimi 1 s */

typedef enum
{
    /* Hatta giden satirlar */
    MSG_TEL = 0,
    MSG_BTN,
    MSG_ACK,
    MSG_REC,
    MSG_CNT,
    MSG_END,

    /* Yalnizca TX kuyrugu icinde, TelemetryTask -> UartTxTask; hatta cikmaz */
    MSG_CMD_POOL_RESET, /* yeni olcum penceresi: kayitlar ve pencere sayaclari sifirlanir */
    MSG_CMD_DUMP,       /* kayitlari REC + CNT + END olarak gonder */

    /* Yalnizca TX kuyrugu icinde, UART TC ISR'si -> UartTxTask; hatta cikmaz.
     * Kesmeden gonderilen (hizli yol) BTN satirinin sonucu: u.rec doludur,
     * UartTxTask yalnizca kayit havuzuna yazar. */
    MSG_BTN_DONE,
} msg_type_t;

typedef enum
{
    REC_STATUS_OK = 0,
    REC_STATUS_TX_DROP,  /* t2'de TX kuyrugu dolu: yanit hic gonderilmedi */
    REC_STATUS_BTN_DROP, /* ISR'de buton kuyrugu dolu: gorev olayi hic almadi */
    REC_STATUS_TX_ERROR, /* UART baslatilamadi ya da TC baska aktarima aitti */
    REC_STATUS_TIMEOUT,  /* TC 50 ms icinde gelmedi ya da R >= 1 s */
} rec_status_t;

typedef enum
{
    CNT_ACCEPTED = 0,      /* filtrenin kabul ettigi basis */
    CNT_REPEAT,            /* filtrenin reddettigi kenar (sicrama) */
    CNT_BTN_QUEUE_DROP,    /* buton kuyrugu dolu */
    CNT_TX_QUEUE_DROP,     /* TX kuyrugu dolu (TEL + BTN) */
    CNT_TX_QUEUE_MAX,      /* TX kuyrugu en yuksek doluluk */
    CNT_POOL_OVERFLOW,     /* 64'luk kayit havuzu dolu */
    CNT_DROPLOG_OVERFLOW,  /* dusen olay kaydi dolu */
    CNT_TX_TIMEOUT,
    CNT_TX_START_FAIL,
    CNT_SPURIOUS_TC,
    CNT_ENCODE_ERROR,      /* 63 bayta sigmayan satir (olmamali) */
    CNT_TEL_SENT,          /* TX kuyruguna giren TEL sayisi */
    CNT_TEL_PERIOD_MIN_US, /* olculen gercek telemetri periyodu */
    CNT_TEL_PERIOD_AVG_US,
    CNT_TEL_PERIOD_MAX_US,
    CNT_BTN_DIRECT,        /* hizli yol: hat bosken EXTI ISR'sinden hemen baslatildi */
    CNT_BTN_LATCHED,       /* hizli yol: hat mesguldu, mandal kuruldu, TC ISR'si baslatti */
    CNT_BTN_FALLBACK,      /* mandal doluydu: olay ButtonTask yoluna gitti */
    CNT_BTN_RESULT_LOST,   /* hizli yol sonucu TX kuyruguna giremedi (olmamali) */
    CNT_TEL_DROP,          /* TX kuyrugu dolu oldugu icin dusen TEL */
    CNT_TEL_WAIT_AVG_US,   /* TEL'in kuyruga girisinden hatta baslamasina (t3) */
    CNT_TEL_WAIT_MAX_US,
    CNT_STACK_HWM_TELEMETRY, /* gorev yigininda acilistan beri en az bos alan (word) */
    CNT_STACK_HWM_BUTTON,
    CNT_STACK_HWM_UART_TX,
    CNT_COUNT
} cnt_id_t;

/* UART TX kuyruguna konulan mantiksal mesaj. Metne cevirme yalnizca
 * UartTxTask icinde, gonderim aninda yapilir. */
typedef struct
{
    msg_type_t type;
    uint8_t scenario_id;
    union
    {
        struct
        {
            int16_t temp_centi_c;
            uint16_t temp_raw;
            uint16_t vdda_mv;
            uint8_t txq_depth;
            uint32_t extra_load_us;
            uint32_t period_us; /* bir onceki periyot baslangicindan bu yana; ilkinde 0 */
            uint32_t enq_us;    /* TX kuyruguna verilmeden hemen once (hatta cikmaz) */
        } tel;
        struct
        {
            uint16_t event_id;
            uint32_t t0, t1, t2;
        } btn;
        struct
        {
            uint16_t event_id;
            rec_status_t status;
            uint8_t known; /* gecerli zaman sayisi: 1 = yalniz t0 ... 5 = t0..t4 */
            uint32_t t[5];
        } rec;
        struct
        {
            cnt_id_t id;
            uint32_t value;
        } cnt;
        struct
        {
            uint32_t records;
        } end;
        struct
        {
            char variant;   /* 'A', 'B' ya da 'C' (main.h g_variant) */
            bool fast_path; /* main.h BTN_FAST_PATH */
            bool trace;     /* FreeRTOSConfig.h USE_SYSVIEW */
        } ack;
    } u;
} tx_message_t;

/* msg'yi hatta gidecek cerceveye cevirir ve cerceve uzunlugunu dondurur:
 * 64 (ASCII satir) ya da PROTOCOL_COMPACT 1 iken TEL/BTN icin ikili cerceve
 * uzunlugu. seq yalnizca TEL/BTN/ACK icin kullanilir. Metin 63 bayti asarsa
 * 0 doner ve out gecersizdir. */
size_t protocol_encode(const tx_message_t *msg, uint16_t seq, uint8_t out[PROTOCOL_FRAME_SIZE]);

/* CRC-8 (polinom 0x07, baslangic 0); ikili cercevenin son bayti. */
uint8_t protocol_crc8(const uint8_t *data, size_t len);

/* seq numarasi tasiyan (ve dolayisiyla artiran) satir mi? */
bool protocol_has_seq(msg_type_t type);

#endif /* PROTOCOL_H */
