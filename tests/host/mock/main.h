#ifndef MOCK_MAIN_H
#define MOCK_MAIN_H
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
typedef struct { volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR; } USART_TypeDef;
typedef struct { volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2]; } GPIO_TypeDef;
typedef struct { volatile uint32_t AHB1ENR, APB1ENR; } RCC_TypeDef;
extern USART_TypeDef g_usart; extern GPIO_TypeDef g_gpioa; extern RCC_TypeDef g_rcc;
#define RCC (&g_rcc)
#define RCC_AHB1ENR_GPIOAEN 1u
#define RCC_APB1ENR_USART2EN 1u
#define USART_SR_ORE  (1u << 3)
#define USART_SR_RXNE (1u << 5)
#define USART_SR_TC   (1u << 6)
#define USART_SR_TXE  (1u << 7)
#define USART_CR1_RE     (1u << 2)
#define USART_CR1_TE     (1u << 3)
#define USART_CR1_RXNEIE (1u << 5)
#define USART_CR1_TCIE   (1u << 6)
#define USART_CR1_TXEIE  (1u << 7)
#define USART_CR1_UE     (1u << 13)
#define USART2_IRQn 38
static inline void NVIC_SetPriority(int irq, uint32_t p) { (void)irq; (void)p; }
static inline void NVIC_EnableIRQ(int irq) { (void)irq; }
#define UART_PERIPH (&g_usart)
#define UART_TX_GPIO_PORT (&g_gpioa)
#define UART_TX_GPIO_PIN 2U
#define UART_RX_GPIO_PORT (&g_gpioa)
#define UART_RX_GPIO_PIN 3U
#define UART_GPIO_AF 7U
#define UART_BAUDRATE 230400U
#endif
