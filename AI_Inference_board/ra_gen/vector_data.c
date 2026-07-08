/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = sci_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [1] = sci_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [2] = sci_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [3] = sci_uart_eri_isr, /* SCI9 ERI (Receive error) */
            [4] = spi_rxi_isr, /* SPI0 RXI (Receive buffer full) */
            [5] = spi_txi_isr, /* SPI0 TXI (Transmit buffer empty) */
            [6] = spi_tei_isr, /* SPI0 TEI (Transmission complete event) */
            [7] = spi_eri_isr, /* SPI0 ERI (Error) */
            [8] = iic_master_rxi_isr, /* IIC1 RXI (Receive data full) */
            [9] = iic_master_txi_isr, /* IIC1 TXI (Transmit data empty) */
            [10] = iic_master_tei_isr, /* IIC1 TEI (Transmit end) */
            [11] = iic_master_eri_isr, /* IIC1 ERI (Transfer error) */
            [12] = sci_uart_rxi_isr, /* SCI2 RXI (Receive data full) */
            [13] = sci_uart_txi_isr, /* SCI2 TXI (Transmit data empty) */
            [14] = sci_uart_tei_isr, /* SCI2 TEI (Transmit end) */
            [15] = sci_uart_eri_isr, /* SCI2 ERI (Receive error) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI,GROUP0), /* SCI9 RXI (Receive data full) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI,GROUP1), /* SCI9 TXI (Transmit data empty) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI,GROUP2), /* SCI9 TEI (Transmit end) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI,GROUP3), /* SCI9 ERI (Receive error) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_SPI0_RXI,GROUP4), /* SPI0 RXI (Receive buffer full) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_SPI0_TXI,GROUP5), /* SPI0 TXI (Transmit buffer empty) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_SPI0_TEI,GROUP6), /* SPI0 TEI (Transmission complete event) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_SPI0_ERI,GROUP7), /* SPI0 ERI (Error) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_IIC1_RXI,GROUP0), /* IIC1 RXI (Receive data full) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TXI,GROUP1), /* IIC1 TXI (Transmit data empty) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_IIC1_TEI,GROUP2), /* IIC1 TEI (Transmit end) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_IIC1_ERI,GROUP3), /* IIC1 ERI (Transfer error) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_SCI2_RXI,GROUP4), /* SCI2 RXI (Receive data full) */
            [13] = BSP_PRV_VECT_ENUM(EVENT_SCI2_TXI,GROUP5), /* SCI2 TXI (Transmit data empty) */
            [14] = BSP_PRV_VECT_ENUM(EVENT_SCI2_TEI,GROUP6), /* SCI2 TEI (Transmit end) */
            [15] = BSP_PRV_VECT_ENUM(EVENT_SCI2_ERI,GROUP7), /* SCI2 ERI (Receive error) */
        };
        #endif
        #endif
