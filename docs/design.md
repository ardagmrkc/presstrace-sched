# Design notes

This document explains how the firmware is put together and which rules each part depends on. Source comments are
in Turkish; file and symbol names below point to the code.

- [1. Measurement points](#1-measurement-points)
- [2. Tasks, interrupts and priorities](#2-tasks-interrupts-and-priorities)
- [3. TX queue: one 16-slot limit, two queues](#3-tx-queue-one-16-slot-limit-two-queues)
- [4. UART arbiter](#4-uart-arbiter)
- [5. Frame formats](#5-frame-formats)
- [6. Button debounce filter](#6-button-debounce-filter)
- [7. Memory and stacks](#7-memory-and-stacks)
- [8. SystemView integration](#8-systemview-integration)
- [9. Known limitations and next steps](#9-known-limitations-and-next-steps)

---

## 1. Measurement points

All five timestamps are read from one free-running 32-bit TIM2 counter at 1 MHz (84 MHz / 84). Intervals are
computed with unsigned subtraction, so they stay correct across a counter wrap.

| Point | Taken in | What the interval up to the next point contains |
|---|---|---|
| **t0** | First line of the EXTI0 ISR, before any tracing | t1−t0: rest of the ISR, queue send, context switch, **ButtonTask's CPU wait** |
| **t1** | ButtonTask, right after `xQueueReceive` | t2−t1: building the BTN message (≈ 2 µs) |
| **t2** | ButtonTask, right before `tx_queue_send` | t3−t2: **TX-queue order**, UartTxTask's CPU wait, line wait, encoding |
| **t3** | UartTxTask, after encoding, right before starting the transfer | t4−t3: the frame on the wire + TC interrupt latency |
| **t4** | First line of the USART2 TC branch | — |

The end-to-end response is R = t4 − t0. A useful approximation is:

> R ≈ own frame time + CPU wait + line (queue) wait

Each experimental knob targets a different term:
- task priority → CPU wait;
- message order (variant C) → line wait;
- frame size → own frame time and line wait.

Two caveats:

- **t0 is not the electrical edge.** It is when the ISR ran. Any delay from a same-priority interrupt, a critical
  section or the trace lock happens before t0 and is not included in R.
- **t3−t2 mixes several waits.** Which one dominates is read from the trace. If Idle runs during the wait, the CPU
  is free and the line or queue is the bottleneck. If Idle does not run, the bottleneck is the CPU.

## 2. Tasks, interrupts and priorities

| Task | Priority | Stack | Blocks on | Woken by |
|---|---|---|---|---|
| TelemetryTask | 3 | 384 words | `vTaskDelayUntil` (period); in S0 a notification | tick; command notification |
| ButtonTask | A: 2 · B/C: 4 | 384 words | `xQueueReceive` (8-entry event queue) | EXTI0 ISR |
| UartTxTask | 1 | 512 words | `s_items` counting semaphore; TC notification | senders; USART2 TC ISR |
| Idle | 0 | 128 words | — | — |

The variant is switched at run time from the ground station. `vTaskPrioritySet` moves ButtonTask between 2 and 4;
the TX-queue rule reads `g_variant` per message. All three variants run from one firmware image.

| Interrupt source | NVIC priority | Notes |
|---|---|---|
| EXTI0 (button) | 5 | Calls `…FromISR` APIs; same priority as USART2 |
| USART2 (TXE / TC / RX) | 5 | Calls `…FromISR` APIs |
| SysTick, PendSV | 15 | Kernel (`configKERNEL_INTERRUPT_PRIORITY` = 0xF0) |

| Mask | BASEPRI | Masks |
|---|---|---|
| `taskENTER_CRITICAL` | 0x50 | priorities 5…15 |
| `SEGGER_RTT_LOCK` (trace) | 0x20 | priorities 2…15 |

EXTI0 and USART2 share priority 5 on purpose. Interrupts of equal priority cannot preempt each other. That gives
the UART arbiter (section 4) a lock between its two ISRs for free. The task side takes the same state only inside
`taskENTER_CRITICAL`, which masks both.

`NVIC_SetPriority` receives the *unshifted* value `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (5). Passing the
shifted 0x50 would be shifted again by CMSIS and truncate to priority 0, above the syscall ceiling.

## 3. TX queue: one 16-slot limit, two queues

`tx_queue.c` keeps a single limit of 16 pending messages across two physical queues. The urgent queue receives BTN
only in variant C. There is no queue set: two counting semaphores carry the state.

```c
bool tx_queue_send(const tx_message_t *msg, TickType_t wait)
{
    if (xSemaphoreTake(s_slots, wait) != pdPASS) return false;  // 1. reserve one of 16 slots
    xQueueSendToBack(queue_for(msg), msg, 0);                   // 2. cannot fail (each queue holds 16)
    xSemaphoreGive(s_items);                                    // 3. announce AFTER the data is in
    return true;
}

void tx_queue_receive(tx_message_t *msg)
{
    xSemaphoreTake(s_items, portMAX_DELAY);
    taskENTER_CRITICAL();                                       // one atomic selection point
    if (xQueueReceive(s_urgent, msg, 0) != pdPASS)
        xQueueReceive(s_fifo, msg, 0);
    taskEXIT_CRITICAL();
    xSemaphoreGive(s_slots);                                    // slot freed before the transfer starts
}
```

Invariants:

- **At most 16 messages wait in total.** A frame that is already on the wire does not count.
- **A woken receiver always finds a message.** The "data before announcement" order guarantees it, independent of
  task priorities.
- **The urgent queue is checked in every variant.** The cost is identical for A, B and C, and BTN messages left
  over after switching from C are never stranded.
- **The selection is a single critical section of two zero-timeout receives.** The UART transfer happens outside it.

Variant C reorders only at **frame boundaries**. A frame on the wire is never interrupted, so C's worst-case line
wait is "rest of the current frame + own frame".

## 4. UART arbiter

`uart.c` owns USART2. One state variable, `s_owner ∈ {NONE, TASK, BTN}`, records who started the frame currently
on the line.

| Transition | Where | Condition |
|---|---|---|
| NONE → TASK | `uart_claim_line`, in a critical section | line idle **and** no latched button frame |
| TASK → NONE | TC ISR; `uart_abort_task_tx` on timeout | transfer complete / aborted |
| NONE → BTN, BTN → NONE | `btn_launch_locked`, TC ISR | ISR fast path only (`BTN_FAST_PATH=1`) |

Invariants:

1. **One frame on the line.** "Is the line idle?" and "take it" are the same atomic step.
2. **Arbiter state is touched only under a lock.** That means a priority-5 ISR, or `taskENTER_CRITICAL` in the task.
3. **t4 belongs to this frame.**
   - `tx_begin_locked` clears TC before enabling TXEIE; TC is 1 whenever the line is idle.
   - TCIE is enabled only after the last byte is written to DR.
   - The ISR evaluates TC only when TCIE is set.
4. **Every TC is matched to one transfer.**
   - Each task transfer carries a non-zero tag, published before the transfer starts.
   - The TC ISR consumes the tag once.
   - The task compares tags after waking. A late TC from an aborted transfer is counted as `spurious_tc` and is
     never taken as the next frame's t4.
5. **Sequence numbers follow wire order.** `s_seq` increments under the lock, at the moment a frame starts.
6. **The frame buffer outlives the transfer.**
   - `send_frame` keeps its stack buffer until it sees TC, or until an abort has disabled TXEIE/TCIE.
   - The fast path uses a static buffer.

Task-notification bits on UartTxTask: bit 0 is `TX_DONE`, bit 1 is `LINE_FREE`. Both waits pass
`ulBitsToClearOnEntry = 0`, so a TC that arrives before the task blocks is not lost. Each wait clears only its own
bit on exit. Stale bits are cleared with `ulTaskNotifyValueClear` right after a successful claim.

Initialisation order matters. USART2 is enabled before PA2 is switched to its alternate function. In the reverse
order the pin glitches during start-up, and the ground station sees a corrupt first byte.

## 5. Frame formats

**Baseline:** every message is ASCII text, padded with spaces to 63 bytes and terminated by LF. That is 64 bytes,
2.77 ms at 230400 baud. A line longer than 63 bytes is never truncated: it is dropped and counted as `encode_error`.
The encoder is a small deterministic writer, not `snprintf`.

**Compact (`PROTOCOL_COMPACT=1`):** the two message types sent during a measurement window become binary.
`ACK`/`REC`/`CNT`/`END` stay ASCII, and `ACK` gets a `K` suffix.

| Offset | TEL (19 B, 0.82 ms) | BTN (20 B, 0.87 ms) |
|---|---|---|
| 0 | sync `0xA5` (never appears in ASCII) | sync `0xA5` |
| 1 | `'T'` | `'B'` |
| 2–3 | seq u16 | event_id u16 |
| 4 | scenario u8 | scenario u8 |
| 5–6 | temperature (centi-°C) i16 | seq u16 |
| 7–8 | ADC raw u16 | t0 u32 (7–10) |
| 9–10 | VDDA mV u16 | |
| 11–12 | extra load µs u16 (saturating) | t1 u32 (11–14) |
| 13–16 | measured period µs u32 | |
| 17 | TX queue depth u8 | t2 u32 (15–18) |
| last | CRC-8 | CRC-8 |

- **Byte order:** fields are little-endian.
- **CRC:** CRC-8/SMBUS (polynomial 0x07, init 0, no reflection) over all bytes except the CRC itself. It catches all
  odd-bit errors and all bursts ≤ 8 bits.
- **Frame length:** `_Static_assert`s tie the field list to the length constants.
- **Resynchronisation:** the receiver accepts a frame only if sync, type, length and CRC all match. Otherwise it
  slides one byte and searches again.

## 6. Button debounce filter

Both edges of PA0 raise EXTI0. An edge that arrives within 30 ms of the previous one is bounce. The direction of a
new group of edges comes from the **level settled before the group**, which is the level sampled at the previous
group's last edge. It does not come from the pin level sampled at the group's first edge:

```c
static bool accept_edge(uint32_t now_us, bool high)
{
    const bool settled_high = s_level;          // level before this group
    s_level = high;                             // last sample of a group = new settled level
    if (s_have_edge && (uint32_t)(now_us - s_last_edge_us) < BUTTON_REPEAT_WINDOW_US) {
        s_last_edge_us = now_us;                // bounce: restart the window
        s_repeat_count++;
        return false;
    }
    s_have_edge = true;
    s_last_edge_us = now_us;
    return !settled_high;                       // released before → this group is a press
}
```

The ISR takes its timestamp first, then clears `EXTI->PR`, then samples the pin. Any edge after the clear re-pends
the interrupt, so the last sample always reflects the line after the last edge.

The previous filter sampled the level at the first edge. When the ISR was delayed, for example behind a USART2
interrupt with tracing enabled, that sample could land on a bounce and classify a press as a release. The host test
(`tests/host/test_button_filter.c`) replays such delayed-ISR sequences. The old filter lost presses in 4 of 8
scenarios; the current one in none.

## 7. Memory and stacks

All kernel objects are statically allocated (`configSUPPORT_DYNAMIC_ALLOCATION 0`). There is no heap, and every byte
appears in the linker map.

| | Size |
|---|---|
| Flash | 24 424 B code + 1 448 B constants |
| RAM | 30 407 B, of which 16 384 B is the SystemView RTT buffer |
| Task stacks | 2 048 + 1 536 + 1 536 B (+ 512 B Idle, kernel-provided) |
| TX queues | 2 × 16 × 28 B |
| Measurement record pool | 64 × 28 B |

Stack sizes come from IAR's static stack analysis. `firmware/EWARM/stack_usage.suc` makes it possible:
- it declares the task entry functions as call-graph roots, since they are passed by pointer;
- it gives the possible targets of SEGGER's callbacks;
- it bounds SEGGER's one-level recursion.

The analysis is cross-checked against high-water marks measured on target:

| Task | Stack | IAR worst case (+ trace recursion + context frame) | Measured peak use |
|---|---|---|---|
| TelemetryTask | 384 w | ≈ 236 w | 56–66 w |
| ButtonTask | 384 w | ≈ 228 w | 56–60 w |
| UartTxTask | 512 w | ≈ 302–340 w | 132 w |

The static figure is the guarantee; the high-water mark only covers paths that actually ran.

For the interrupt stack (MSP, 2 KB), IAR's "2000 B" figure assumes that all four handlers nest. Equal-priority
interrupts cannot nest. The realistic worst case is one priority-15 handler interrupted by one priority-5 handler:
424 + 664 B plus two FPU exception frames, about 1.3 KB.

## 8. SystemView integration

SEGGER SystemView V4.12 target sources and RTT V8.58 run over J-Link: the on-board ST-LINK was reflashed to J-Link
OB. Timestamps come from the DWT cycle counter (168 MHz).

Application events, one module with seven events:

| Event | Point | Placement |
|---|---|---|
| `BTN ACCEPT n S` + marker start | t0 | EXTI0 ISR |
| `BTN TASK n` | t1 | ButtonTask |
| `BTN READY n` | t2 | ButtonTask |
| `BTN ENQUEUE n q` | | after the TX-queue send; `q` = pending messages |
| `BTN DROP n at` + marker stop | | queue full; `at` = 1 button queue, 2 TX queue |
| `BTN TX_START n` | t3 | UartTxTask |
| `BTN TX_TC n` + marker stop | t4 | USART2 TC ISR |

Rules that keep the trace from moving the measurement:
- **Timestamp first, trace second.** Every event is recorded right after its timestamp, so the trace cost falls into
  the next interval.
- **One exit per ISR.** Every ISR leaves through `portYIELD_FROM_ISR`, which also records the ISR exit.
- **No hidden interrupts.** Every hardware interrupt is traced, including each USART2 TXE byte interrupt.

`TRACERETURN_ENABLE 0` drops the API-return events and roughly halves the stream.

Two pitfalls hit during integration:

- **`#` in the module description.** SystemView's description syntax treats `#` as the start of a comment. With
  `#%u` in the module string, the host-side stream froze after 0.1–0.4 s. The string now uses `n=%u`, and a
  `_Static_assert` keeps it within 128 characters.
- **Stop could leave the CPU halted.** SystemView's live-recording Stop could leave the CPU halted on this
  J-Link-OB setup (DHCSR `0x00030003`). `analysis/sysview_record.py` records without the GUI:
  - It sends START/STOP itself through the RTT down-buffer.
  - It streams with JLinkRTTLogger and opens no other J-Link session while recording, because a second session
    starves the RTT reader.
  - It computes dropped events as a difference, because SystemView's DropCount is cumulative.
  - It trims the file at the SYNC sequence, so SystemView can load it directly.

## 9. Known limitations and next steps

- **No worst-case guarantee.** Thirty presses per run give a distribution, not a WCET bound. The runs use one board
  and a Debug build.
- **Tracing is always on.** It is compiled into every run, so its overhead is part of the numbers. A trace-off
  comparison with the same firmware is the next measurement.
- **A UART status-flag race.** The TC flag is cleared with a read-modify-write on `USART2->SR`
  (`SR &= ~TC`). TC and RXNE are both write-0-to-clear. An RX byte arriving in the 1–2 cycles between the read and
  the write would be cleared too. RX is only used for 5-byte commands, which the ground station retries, so the
  impact is negligible. The correct idiom is `SR = ~USART_SR_TC`.
- **One interrupt per transmitted byte.** At a fully loaded line that is ~23 k interrupts/s. DMA would remove them,
  at the cost of a different completion path.
- **UartTxTask runs at the lowest priority.** That is the source of the residual CPU wait in S5. Raising it is a
  separate experiment. The prediction is that the CPU-wait share of t3−t2 in B/S5 (1.72 ms mean) disappears and
  only the line wait remains.
