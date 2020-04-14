#include "peripherals/irq.h"
#include "peripherals/timer.h"
#include "peripherals/uart0.h"
#include "queue.h"
#include "uart0.h"

void irq_enable() {
    // Enable IMO
    register unsigned int hcr_el2_value;
    asm volatile("mrs %0, hcr_el2"
                 : "=r"(hcr_el2_value));
    hcr_el2_value |= 1 << 4;  // IMO
    asm volatile("msr hcr_el2, %0"
                 :
                 : "r"(hcr_el2_value));
    // Unmask IRQ
    asm volatile("msr daifclr, #2");
}

void arm_core_timer_enable() {
    // enable timer
    register unsigned int enable = 1;
    asm volatile("msr cntp_ctl_el0, %0"
                 :
                 : "r"(enable));
    // set expired time
    register unsigned int expire_period = CORE_TIMER_EXPRIED_PERIOD;
    asm volatile("msr cntp_tval_el0, %0"
                 :
                 : "r"(expire_period));
    // enable timer interrupt
    *CORE0_TIMER_IRQ_CTRL |= 1 << 1;
}

void arm_core_timer_disable() {
    // disable timer
    register unsigned int enable = 0;
    asm volatile("msr cntp_ctl_el0, %0"
                 :
                 : "r"(enable));
    // disable timer interrupt
    *CORE0_TIMER_IRQ_CTRL &= !(1 << 1);
}

void arm_local_timer_enable() {
    unsigned int flag = 0x30000000;        // enable timer and interrupt.
    unsigned int reload = 0xfffffff / 10;  // 0.14 Hz * 10
    *LOCAL_TIMER_CTRL = flag | reload;
}

void arm_local_timer_disable() {
    *LOCAL_TIMER_CTRL &= !(0b11 << 28);  // disable timer and interrupt.
}

/*
 * Synchronous Exception
 */

void sync_el2h_router(unsigned long esr, unsigned long elr) {
    uart_printf("Exception return address 0x%x\n", elr);
    uart_printf("Exception class (EC) 0x%x\n", (esr >> 26) & 0b111111);
    uart_printf("Instruction specific syndrome (ISS) 0x%x\n", esr & 0xFFFFFF);
}

/*
 * IRQ Exception
 */

unsigned long long arm_core_timer_jiffies = 0;
unsigned long long arm_local_timer_jiffies = 0;

void uart_intr_handler() {
    if (*UART0_MIS & 0x10) {           // UARTTXINTR
        while (!(*UART0_FR & 0x10)) {  // RX FIFO not empty
            char r = (char)(*UART0_DR);
            queue_push(&read_buf, r);
        }
        *UART0_ICR = 1 << 4;
    }
    else if (*UART0_MIS & 0x20) {           // UARTRTINTR
        while (!queue_empty(&write_buf)) {  // flush buffer to TX
            while (*UART0_FR & 0x20) {      // TX FIFO is full
                asm volatile("nop");
            }
            *UART0_DR = queue_pop(&write_buf);
        }
        *UART0_ICR = 2 << 4;
    }
}

void arm_core_timer_intr_handler() {
    register unsigned int expire_period = CORE_TIMER_EXPRIED_PERIOD;
    asm volatile("msr cntp_tval_el0, %0"
                 :
                 : "r"(expire_period));
    uart_printf("Core timer interrupt, jiffies %d\n", ++arm_core_timer_jiffies);
    // bottom half simulation
    // irq_enable();
    // unsigned long long x = 100000000000;
    // while (x--) {
    // }
}

void arm_local_timer_intr_handler() {
    *LOCAL_TIMER_IRQ_CLR = 0b11 << 30;  // clear interrupt
    uart_printf("Local timer interrupt, jiffies %d\n", ++arm_local_timer_jiffies);
}

void irq_el2h_router() {
    unsigned int irq_basic_pending = *IRQ_BASIC_PENDING;
    unsigned int core0_intr_src = *CORE0_INTR_SRC;

    // GPU IRQ 57: UART Interrupt
    if (irq_basic_pending & (1 << 19)) {
        uart_intr_handler();
    }
    // ARM Core Timer Interrupt
    else if (core0_intr_src & (1 << 1)) {
        arm_core_timer_intr_handler();
    }
    // ARM Local Timer Interrupt
    else if (core0_intr_src & (1 << 11)) {
        arm_local_timer_intr_handler();
    }
}