#include "protocol.h"
#include <string.h>

/* snprintf yerine kucuk, deterministik bir yazici: C kutuphanesinin yigin ve
 * sure degiskenligi UartTxTask'a girmesin. Tampon 63'ten genistir; tasma
 * sonradan kontrol edilir, hicbir zaman sessizce kesilmez. */
typedef struct
{
    char buf[96];
    size_t len;
} line_t;

static void put_c(line_t *l, char c)
{
    if (l->len < sizeof(l->buf))
    {
        l->buf[l->len] = c;
    }
    l->len++;
}

static void put_s(line_t *l, const char *s)
{
    while (*s != '\0')
    {
        put_c(l, *s++);
    }
}

static void put_u32(line_t *l, uint32_t v)
{
    char tmp[10];
    size_t n = 0;
    do
    {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    while (n > 0U)
    {
        put_c(l, tmp[--n]);
    }
}

static void put_i32(line_t *l, int32_t v)
{
    if (v < 0)
    {
        put_c(l, '-');
        put_u32(l, (uint32_t)(-(int64_t)v));
    }
    else
    {
        put_u32(l, (uint32_t)v);
    }
}

static void put_sep_u32(line_t *l, uint32_t v)
{
    put_c(l, ',');
    put_u32(l, v);
}

static void put_scenario(line_t *l, uint8_t id)
{
    put_s(l, ",S");
    put_u32(l, id);
}

static const char *const s_status_names[] = {
    "ok", "tx_drop", "btn_drop", "tx_error", "timeout",
};

static const char *const s_cnt_names[] = {
    "accepted", "repeat", "btn_queue_drop", "tx_queue_drop", "tx_queue_max",
    "pool_overflow", "droplog_overflow", "tx_timeout", "tx_start_fail",
    "spurious_tc", "encode_error", "tel_sent", "tel_period_min_us",
    "tel_period_avg_us", "tel_period_max_us", "btn_direct", "btn_latched",
    "btn_fallback", "btn_result_lost", "tel_drop", "tel_wait_avg_us",
    "tel_wait_max_us", "stack_hwm_telemetry", "stack_hwm_button",
    "stack_hwm_uart_tx",
};
_Static_assert(sizeof(s_cnt_names) / sizeof(s_cnt_names[0]) == CNT_COUNT,
               "s_cnt_names, cnt_id_t ile ayni sirada ve sayida olmali");

bool protocol_has_seq(msg_type_t type)
{
    return type == MSG_TEL || type == MSG_BTN || type == MSG_ACK;
}

uint8_t protocol_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0U;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t b = 0U; b < 8U; b++)
        {
            const bool top = (crc & 0x80U) != 0U;
            crc = (uint8_t)(crc << 1);
            if (top)
            {
                crc ^= 0x07U;
            }
        }
    }
    return crc;
}

#if PROTOCOL_COMPACT
/* Ikili cerceve yazicisi (little-endian). Uzunluklar sabit, tasma olmaz. */
static uint8_t *put_le(uint8_t *p, uint32_t v, uint8_t bytes)
{
    for (uint8_t i = 0U; i < bytes; i++)
    {
        *p++ = (uint8_t)(v >> (8U * i));
    }
    return p;
}

static size_t encode_bin(const tx_message_t *m, uint16_t seq, uint8_t *out)
{
    uint8_t *p = out;
    *p++ = PROTOCOL_BIN_SYNC;
    if (m->type == MSG_TEL)
    {
        const uint32_t extra = m->u.tel.extra_load_us;
        *p++ = PROTOCOL_BIN_TYPE_TEL;
        p = put_le(p, seq, 2U);
        p = put_le(p, m->scenario_id, 1U);
        p = put_le(p, (uint16_t)m->u.tel.temp_centi_c, 2U);
        p = put_le(p, m->u.tel.temp_raw, 2U);
        p = put_le(p, m->u.tel.vdda_mv, 2U);
        p = put_le(p, (extra > 0xFFFFU) ? 0xFFFFU : extra, 2U);
        p = put_le(p, m->u.tel.period_us, 4U);
        p = put_le(p, m->u.tel.txq_depth, 1U);
    }
    else
    {
        *p++ = PROTOCOL_BIN_TYPE_BTN;
        p = put_le(p, m->u.btn.event_id, 2U);
        p = put_le(p, m->scenario_id, 1U);
        p = put_le(p, seq, 2U);
        p = put_le(p, m->u.btn.t0, 4U);
        p = put_le(p, m->u.btn.t1, 4U);
        p = put_le(p, m->u.btn.t2, 4U);
    }
    const size_t n = (size_t)(p - out);
    *p = protocol_crc8(out, n);
    return n + 1U;
}

_Static_assert(2U + 2U + 1U + 2U + 2U + 2U + 2U + 4U + 1U + 1U == PROTOCOL_BIN_TEL_SIZE,
               "TEL ikili cerceve uzunlugu protocol.h ile ayni olmali");
_Static_assert(2U + 2U + 1U + 2U + 4U + 4U + 4U + 1U == PROTOCOL_BIN_BTN_SIZE,
               "BTN ikili cerceve uzunlugu protocol.h ile ayni olmali");
#endif

static void encode_rec(line_t *l, const tx_message_t *m)
{
    put_s(l, "REC");
    put_scenario(l, m->scenario_id);
    put_sep_u32(l, m->u.rec.event_id);
    put_sep_u32(l, m->u.rec.t[0]);
    /* Bilesen sureleri: t1-t0, t2-t1, t3-t2, t4-t3. uint32 farki sayac
     * sarmasinda da dogrudur. Bilinmeyen ya da >= 1 s olan sure bos kalir. */
    for (uint8_t i = 1U; i < 5U; i++)
    {
        put_c(l, ',');
        if (i < m->u.rec.known)
        {
            uint32_t d = m->u.rec.t[i] - m->u.rec.t[i - 1U];
            if (d < EXPERIMENT_TIMEOUT_US)
            {
                put_u32(l, d);
            }
        }
    }
    put_c(l, ',');
    put_s(l, s_status_names[m->u.rec.status]);
}

size_t protocol_encode(const tx_message_t *m, uint16_t seq, uint8_t out[PROTOCOL_FRAME_SIZE])
{
    line_t l = { .len = 0 };

#if PROTOCOL_COMPACT
    if (m->type == MSG_TEL || m->type == MSG_BTN)
    {
        return encode_bin(m, seq, out);
    }
#endif

    switch (m->type)
    {
    case MSG_TEL:
        put_s(&l, "TEL");
        put_sep_u32(&l, seq);
        put_scenario(&l, m->scenario_id);
        put_c(&l, ',');
        put_i32(&l, m->u.tel.temp_centi_c);
        put_sep_u32(&l, m->u.tel.temp_raw);
        put_sep_u32(&l, m->u.tel.vdda_mv);
        put_sep_u32(&l, m->u.tel.extra_load_us);
        put_sep_u32(&l, m->u.tel.period_us);
        put_sep_u32(&l, m->u.tel.txq_depth);
        break;
    case MSG_BTN:
        put_s(&l, "BTN");
        put_sep_u32(&l, m->u.btn.event_id);
        put_scenario(&l, m->scenario_id);
        put_s(&l, ",PRESSED");
        put_sep_u32(&l, seq);
        put_sep_u32(&l, m->u.btn.t0);
        put_sep_u32(&l, m->u.btn.t1);
        put_sep_u32(&l, m->u.btn.t2);
        break;
    case MSG_ACK:
        put_s(&l, "ACK");
        put_sep_u32(&l, seq);
        put_scenario(&l, m->scenario_id);
        put_c(&l, ',');
        put_c(&l, m->u.ack.variant);
        if (m->u.ack.fast_path)
        {
            put_c(&l, 'F');
        }
        if (m->u.ack.trace)
        {
            put_c(&l, 'T');
        }
#if PROTOCOL_COMPACT
        put_c(&l, 'K');
#endif
        break;
    case MSG_REC:
        encode_rec(&l, m);
        break;
    case MSG_CNT:
        put_s(&l, "CNT");
        put_scenario(&l, m->scenario_id);
        put_c(&l, ',');
        put_s(&l, s_cnt_names[m->u.cnt.id]);
        put_sep_u32(&l, m->u.cnt.value);
        break;
    case MSG_END:
        put_s(&l, "END");
        put_scenario(&l, m->scenario_id);
        put_sep_u32(&l, m->u.end.records);
        break;
    default:
        return 0U; /* ic komutlar hatta cikmaz */
    }

    if (l.len > PROTOCOL_LINE_MAX)
    {
        return 0U;
    }
    memcpy(out, l.buf, l.len);
    memset(&out[l.len], ' ', PROTOCOL_LINE_MAX - l.len);
    out[PROTOCOL_LINE_MAX] = '\n';
    return PROTOCOL_FRAME_SIZE;
}
