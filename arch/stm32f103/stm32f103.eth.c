#define STM32F1

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/dma.h>

#include <enc28j60.h>

#include <stdio.h>
#include <string.h>
#include <output.h>
#include "config.h"

#include <control.h>
#include <moves.h>
#include <planner.h>
#include <gcode_handler.h>

#define FCPU 72000000UL
#define FTIMER 100000UL
#define PSC ((FCPU) / (FTIMER) - 1)
#define TIMEOUT_TIMER_STEP 1000UL

#define NORT_ETHERTYPE 0xFEFE

#define min(a, b) ((a) < (b) ? (a) : (b))

static int shell_cfg = 0;

static const uint8_t mac[6] = {0x0C, 0x00, 0x00, 0x00, 0x00, 0x02};
//static const uint8_t ip[4] = {10, 55, 2, 200};
static uint8_t host[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define MNUM 8
#define MLEN 120
static char messages[MNUM][MLEN];
static int mpos = 0;
static int mfirst = 0;
static int mnum = 0;

struct enc28j60_state_s eth_state;
bool configured = false;

void gpio_setup(void);
void step_timer_setup(void);
void usart_setup(int baudrate);
void uart_send(const uint8_t *data, ssize_t len);
void uart_pause(void);
void uart_continue(void);

// ********************************************

static void clock_setup(void)
{
    rcc_clock_setup_in_hse_8mhz_out_72mhz();

    /* Enable GPIOA clock. */
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);

    /* Enable clocks for GPIO port A (for GPIO_USART1_TX) and USART1. */
    rcc_periph_clock_enable(RCC_USART1);

    /* Enable TIM2 clock for steps*/
    rcc_periph_clock_enable(RCC_TIM2);

    /* Enable TIM3 clock for connection timeouts */
    rcc_periph_clock_enable(RCC_TIM3);

    /* Enable SPI2 */
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_SPI2);

    /* Enable DMA1 */
    rcc_periph_clock_enable(RCC_DMA1);
}

static void dma_int_enable(void)
{
    /* SPI2 RX on DMA1 Channel 4 */
    nvic_set_priority(NVIC_DMA1_CHANNEL4_IRQ, 0);
    nvic_enable_irq(NVIC_DMA1_CHANNEL4_IRQ);
    /* SPI2 TX on DMA1 Channel 5 */
    nvic_set_priority(NVIC_DMA1_CHANNEL5_IRQ, 0);
    nvic_enable_irq(NVIC_DMA1_CHANNEL5_IRQ);
}

/* Setup SPI */
uint8_t spi_rw(uint8_t d)
{
    spi_send(SPI2, d);
    return spi_read(SPI2);
}

void spi_cs(uint8_t v)
{
    if (v)
        gpio_set(GPIOB, GPIO_SPI2_NSS);
    else
        gpio_clear(GPIOB, GPIO_SPI2_NSS);
}

static bool send_completed;

static void spi_write_buf(const uint8_t *data, size_t len)
{
    while (len-- > 0)
    {
        spi_rw(*(data++));
    }
}

static bool read_completed;

static void spi_read_buf(uint8_t *data, size_t len)
{
    while (len > 0)
    {
        *(data++) = spi_rw(0xFF);
        len--;
    }
}

void spi_setup(void)
{
    nvic_enable_irq(NVIC_SPI2_IRQ);

    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_SPI2_MOSI | GPIO_SPI2_SCK);
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_SPI2_NSS);
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO_SPI2_MISO);

    spi_reset(SPI2);

    spi_init_master(SPI2,
                    SPI_CR1_BAUDRATE_FPCLK_DIV_8,
                    SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
                    SPI_CR1_CPHA_CLK_TRANSITION_1,
                    SPI_CR1_DFF_8BIT,
                    SPI_CR1_MSBFIRST);
    spi_enable_software_slave_management(SPI2);
    spi_set_nss_high(SPI2);

    spi_enable(SPI2);
}



/******** SHELL ******************/

int blink = 0;
int do_blink(void)
{
    if (blink)
    {
        gpio_set(GPIOC, GPIO13);
        blink = 0;
    }
    else
    {
        gpio_clear(GPIOC, GPIO13);
        blink = 1;
    }
    return 0;
}

static bool ethernet_lock = false;
static void eth_lock(void)
{
    nvic_disable_irq(NVIC_EXTI1_IRQ);
    ethernet_lock = true;
}

static void eth_unlock(void)
{
    ethernet_lock = false;
    nvic_enable_irq(NVIC_EXTI1_IRQ);
}

static bool poll_lock_f = false;
static void poll_lock(void)
{
    while (poll_lock_f)
            ;
    poll_lock_f = true;
}

static void poll_unlock(void)
{
    poll_lock_f = false;
}

static bool eth_has_pkg(void)
{
    bool res = enc28j60_has_package(&eth_state);
    return res;
}

static void run_eth(void)
{
    static bool eth_running = false;
    if (eth_running)
	return;
    eth_running = true;
    bool ehp;
    while ((ehp = eth_has_pkg()) || mnum > 0)
    {
        if (mnum > 0)
        {
            size_t len = strlen(messages[mfirst]);
            char hdr[ETHERNET_HEADER_LEN];
            enc28j60_fill_header(hdr, mac, host, NORT_ETHERTYPE);
            while (!enc28j60_tx_ready(&eth_state))
                ;
            uint8_t plen[2] = {len >> 8, len & 0xFF};
            enc28j60_send_start(&eth_state);
            enc28j60_send_append(&eth_state, hdr, ETHERNET_HEADER_LEN);
            enc28j60_send_append(&eth_state, plen, 2);
            enc28j60_send_append(&eth_state, messages[mfirst], len);
            enc28j60_send_commit(&eth_state);
            mfirst = (mfirst + 1) % MNUM;
            mnum--;
        }
        if (ehp)
        {
            char buf[1518];
            uint32_t status;
            uint32_t crc;

            ssize_t len = enc28j60_read_packet(&eth_state, buf, 1518, &status, &crc);

            uint16_t ethertype = enc28j60_get_ethertype(buf, len);
            uint8_t src[6];
            uint8_t dst[6];
            uint16_t plen;
            uint8_t *pl = (uint8_t *)enc28j60_get_payload(buf, len);
            memcpy(src, enc28j60_get_source(buf, len), 6);
            memcpy(dst, enc28j60_get_target(buf, len), 6);
            switch (ethertype)
            {
            case NORT_ETHERTYPE:
                memcpy(host, src, 6);
                plen = ((uint16_t)pl[0]) << 8 | pl[1];
                execute_g_command(pl+2, plen);
                break;
            default:
                break;
            }
        }
    }
    eth_running = false;
}

ssize_t write_fun(int fd, const void *data, ssize_t len)
{
    int i;
    if (len < 0)
        len = strlen((const char *)data);
    if (fd == 0)
    {
        if (mnum == MNUM)
            return -1;
        len = min(len, MLEN-1);
        memcpy(messages[mpos], data, len);
        messages[mpos][len] = 0;
        mpos = (mpos + 1) % MNUM;
        mnum++;
    }
    else
    {
        uart_send(data, len);
    }
    return 0;
}


static void shell_setup(void)
{
    output_set_write_fun(write_fun);
    output_control_set_fd(0);
    output_shell_set_fd(1);
}

/************* END SHELL ****************/

/* Ethernet */

static void enc28j60setup(struct enc28j60_state_s *state)
{
    // interrupt pin
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO1);

    // enable interrupt
/*    nvic_set_priority(NVIC_EXTI1_IRQ, 0x22);
    nvic_enable_irq(NVIC_EXTI1_IRQ);
    exti_select_source(EXTI1, GPIOB);
    exti_set_trigger(EXTI1, EXTI_TRIGGER_FALLING);
    exti_enable_request(EXTI1);*/

    enc28j60_init(state, spi_rw, spi_cs, spi_write_buf, spi_read_buf);
    enc28j60_configure(state, mac, 4096, false);
    enc28j60_interrupt_enable(state, true);
}

/*void exti1_isr(void)
{
    run_eth();
    exti_reset_request(EXTI1);
}*/

void hard_fault_handler(void)
{
    int i;
    while (1)
    {
        for (i = 0; i < 0x400000; i++)
            __asm__("nop");
        gpio_set(GPIOC, GPIO13);
        for (i = 0; i < 0x400000; i++)
            __asm__("nop");
        gpio_clear(GPIOC, GPIO13);
    }
}

void hardware_setup(void)
{

    SCB_VTOR = (uint32_t) 0x08000000;

    clock_setup();
    gpio_setup();
    dma_int_enable();
    spi_setup();
    
    step_timer_setup();
    usart_setup(SHELL_BAUDRATE);
    enc28j60setup(&eth_state);
    gpio_set(GPIOC, GPIO13);
    shell_setup();

    uart_send("configured", -1);
    configured = true;
}

void hardware_loop(void)
{
	run_eth();
}
