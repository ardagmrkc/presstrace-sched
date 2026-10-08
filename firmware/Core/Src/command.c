#include "command.h"
#include "main.h"
#include "protocol.h"

static TaskHandle_t s_target = NULL;
static uint8_t s_buf[CMD_FRAME_SIZE];
static uint8_t s_len = 0;

void command_init(TaskHandle_t target)
{
    s_target = target;
}

void command_rx_byte_from_isr(uint8_t b, BaseType_t *higher_prio_woken)
{
    if (s_len == 0U)
    {
        if (b == CMD_SYNC0)
        {
            s_buf[s_len++] = b;
        }
        return;
    }
    if (s_len == 1U && b != CMD_SYNC1)
    {
        s_len = (b == CMD_SYNC0) ? 1U : 0U; /* AA AA 55 ... durumunda yeniden hizala */
        return;
    }

    s_buf[s_len++] = b;
    if (s_len < CMD_FRAME_SIZE)
    {
        return;
    }
    s_len = 0;

    uint8_t sum = (uint8_t)(s_buf[0] + s_buf[1] + s_buf[2] + s_buf[3]);
    uint8_t arg = s_buf[3];
    if (sum != s_buf[4])
    {
        return;
    }

    uint32_t value;
    if (s_buf[2] == CMD_TYPE_SCENARIO)
    {
        if (arg >= SCENARIO_COUNT && arg != CMD_ARG_QUERY && arg != CMD_ARG_DUMP)
        {
            return;
        }
        value = arg;
    }
    else if (s_buf[2] == CMD_TYPE_VARIANT)
    {
        if (arg != (uint8_t)'A' && arg != (uint8_t)'B' && arg != (uint8_t)'C')
        {
            return;
        }
        value = CMD_NOTIFY_VARIANT | arg;
    }
    else
    {
        return;
    }

    (void)xTaskNotifyFromISR(s_target, value, eSetValueWithOverwrite, higher_prio_woken);
}
