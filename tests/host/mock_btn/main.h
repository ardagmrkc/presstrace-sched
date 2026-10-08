/* button.c icin taklit main.h: GPIO/EXTI/RCC/SYSCFG register'lari bellekte. */
#ifndef MOCK_BTN_MAIN_H
#define MOCK_BTN_MAIN_H
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
typedef struct { volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2]; } GPIO_TypeDef;
typedef struct { volatile uint32_t AHB1ENR, APB1ENR, APB2ENR; } RCC_TypeDef;
typedef struct { volatile uint32_t EXTICR[4]; } SYSCFG_TypeDef;
typedef struct { volatile uint32_t IMR, EMR, RTSR, FTSR, SWIER, PR; } EXTI_TypeDef;
extern GPIO_TypeDef g_gpioa, g_gpiod; extern RCC_TypeDef g_rcc;
extern SYSCFG_TypeDef g_syscfg; extern EXTI_TypeDef g_exti;
#define GPIOA (&g_gpioa)
#define GPIOD (&g_gpiod)
#define RCC (&g_rcc)
#define SYSCFG (&g_syscfg)
#define EXTI (&g_exti)
#define RCC_AHB1ENR_GPIOAEN 1u
#define RCC_AHB1ENR_GPIODEN 8u
#define RCC_APB2ENR_SYSCFGEN (1u << 14)
#define SYSCFG_EXTICR1_EXTI0 0xFu
#define EXTI0_IRQn 6
static inline void NVIC_SetPriority(int irq, uint32_t p) { (void)irq; (void)p; }
static inline void NVIC_ClearPendingIRQ(int irq) { (void)irq; }
static inline void NVIC_EnableIRQ(int irq) { (void)irq; }
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define BUTTON_GPIO_PORT GPIOA
#define BUTTON_GPIO_PIN  0U
#define BUTTON_EXTI_LINE 0U
#define LED_GPIO_PORT    GPIOD
#define LED_GPIO_PIN     12U
#define BUTTON_REPEAT_WINDOW_US 30000U
#define BTN_FAST_PATH 0
typedef struct { uint8_t id; } scenario_config_t;
extern scenario_config_t g_scenario;
#endif
