/*
 * Single-File-Header for SPI with circular DMA for audio output
 * 04-10-2023 E. Brombaugh
 */

#ifndef _SPIDAC_H
#define _SPIDAC_H

#include <stdint.h>

// uncomment this to enable GPIO diag
#define DAC_DIAG

// uncomment this to fill the buffer with static test data
#define DAC_STATIC

// uncomment this for timer-generated DMA
#define DAC_TIMDMA

#define DACBUFSZ 32

uint16_t dac_buffer[DACBUFSZ];

/*
 * initialize SPI and DMA
 */
void spidac_init( void )
{
#ifdef DAC_STATIC
	uint16_t data = 0xffff;
	for(int i=0;i<DACBUFSZ;i++)
	{
		/* just a full-scale ramp for now */
		dac_buffer[i] = data;
		data >>= 1;
	}	
#endif
	
	// Enable DMA + Peripherals
	RCC->AHBPCENR |= RCC_AHBPeriph_DMA1;
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_SPI1;

#ifdef DAC_DIAG
	// GPIO D0 Push-Pull for diags
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;
	GPIOD->CFGLR &= ~(0xf<<(4*0));
	GPIOD->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*0);
#endif
	
	// MOSI on PC6
	GPIOC->CFGLR &= ~(0xf<<(4*6));
	GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF)<<(4*6);
	// SCK on PC5
	GPIOC->CFGLR &= ~(0xf<<(4*5));
	GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF)<<(4*5);
	// NSS on PC1 is GPIO
	GPIOC->CFGLR &= ~(0xf<<(4*1));
	GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*1);

	// Configure SPI 
	SPI1->CTLR1 = 
		SPI_NSS_Soft | SPI_CPHA_1Edge | SPI_CPOL_Low | SPI_DataSize_16b |
		SPI_Mode_Master | SPI_Direction_1Line_Tx |
		SPI_BaudRatePrescaler_16;

#ifndef DAC_TIMDMA
	// SPI generates DMA Req
	SPI1->CTLR2 = SPI_CTLR2_TXDMAEN;
	//SPI1->HSCR = 1;

	// enable SPI port
	SPI1->CTLR1 |= CTLR1_SPE_Set;

	//DMA1_Channel3 is for SPI1TX
	DMA1_Channel3->PADDR = (uint32_t)&SPI1->DATAR;
	DMA1_Channel3->MADDR = (uint32_t)dac_buffer;
	DMA1_Channel3->CNTR  = DACBUFSZ;
	DMA1_Channel3->CFGR  =
		DMA_M2M_Disable |		 
		DMA_Priority_VeryHigh |
		DMA_MemoryDataSize_HalfWord |
		DMA_PeripheralDataSize_HalfWord |
		DMA_MemoryInc_Enable |
		DMA_Mode_Circular |
		DMA_DIR_PeripheralDST |
		DMA_IT_TC | DMA_IT_HT;

	NVIC_EnableIRQ( DMA1_Channel3_IRQn );
	DMA1_Channel3->CFGR |= DMA_CFGR1_EN;
#else
	// enable SPI port
	SPI1->CTLR1 |= CTLR1_SPE_Set;

	// TIM1 generates DMA Req and external signal
	// Enable TIM1
	RCC->APB2PCENR |= RCC_APB2Periph_TIM1;
	
	// PC4 is T1CH4, 50MHz Output PP CNF = 10: Mux PP, MODE = 11: Out 50MHz
	GPIOC->CFGLR &= ~(GPIO_CFGLR_MODE4 | GPIO_CFGLR_CNF4);
	GPIOC->CFGLR |= GPIO_CFGLR_CNF4_1 | GPIO_CFGLR_MODE4_0 | GPIO_CFGLR_MODE4_1;
		
	// Reset TIM1 to init all regs
	RCC->APB2PRSTR |= RCC_APB2Periph_TIM1;
	RCC->APB2PRSTR &= ~RCC_APB2Periph_TIM1;
	
	// CTLR1: default is up, events generated, edge align
	// SMCFGR: default clk input is CK_INT
	
	// Prescaler 
	TIM1->PSC = 0x0000;
	
	// Auto Reload - sets period to ~47kHz
	TIM1->ATRLR = 1023;
	
	// Reload immediately
	TIM1->SWEVGR |= TIM_UG;
	
	// Enable CH4 output, positive pol
	TIM1->CCER |= TIM_CC4E;// | TIM_CC4P;
	
	// CH2 Mode is output, PWM1 (CC1S = 00, OC1M = 110)
	TIM1->CHCTLR2 |= TIM_OC4M_2 | TIM_OC4M_1;
	
	// Set the Capture Compare Register value to 50% initially
	TIM1->CH4CVR = 512;
	
	// Enable TIM1 outputs
	TIM1->BDTR |= TIM_MOE;
	
	// Enable CH4 DMA Req
	TIM1->DMAINTENR |= TIM_CC4DE;
	
	// Enable TIM1
	TIM1->CTLR1 |= TIM_CEN;

	//DMA1_Channel4 is for TIM1CH4 
	DMA1_Channel4->PADDR = (uint32_t)&SPI1->DATAR;
	DMA1_Channel4->MADDR = (uint32_t)dac_buffer;
	DMA1_Channel4->CNTR  = DACBUFSZ;
	DMA1_Channel4->CFGR  =
		DMA_M2M_Disable |		 
		DMA_Priority_VeryHigh |
		DMA_MemoryDataSize_HalfWord |
		DMA_PeripheralDataSize_HalfWord |
		DMA_MemoryInc_Enable |
		DMA_Mode_Circular |
		DMA_DIR_PeripheralDST |
		DMA_IT_TC | DMA_IT_HT;

	NVIC_EnableIRQ( DMA1_Channel4_IRQn );
	DMA1_Channel4->CFGR |= DMA_CFGR1_EN;
#endif
}

/*
 * DAC buffer updates - called at IRQ time
 */
void dac_update(uint16_t *buffer)
{
}

#ifndef DAC_TIMDMA
/*
 * SPI DMA IRQ Handler
 */
void DMA1_Channel3_IRQHandler( void ) __attribute__((interrupt));
void DMA1_Channel3_IRQHandler( void ) 
{
#ifdef DAC_DIAG
	GPIOD->BSHR = 1;
#endif
	
	// why is this needed? Can't just direct compare the reg in tests below
	volatile uint16_t intfr = DMA1->INTFR;

	if( intfr & DMA1_IT_TC3 )
	{
		// Transfer complete - update 2nd half
		dac_update(dac_buffer+DACBUFSZ/2);
		
		// clear TC IRQ
		DMA1->INTFCR = DMA1_IT_TC3;
		
		GPIOC->BSHR = (1<<1); // NSS 1
	}
	
	if( intfr & DMA1_IT_HT3 )
	{
		// Half transfer - update first half
		dac_update(dac_buffer);
		
		// clear HT IRQ
		DMA1->INTFCR = DMA1_IT_HT3;
		
		GPIOC->BSHR = (1<<(1+16)); // NSS 0
	}

	// clear the Global IRQ
	DMA1->INTFCR = DMA1_IT_GL3;
	
#ifdef DAC_DIAG
	GPIOD->BSHR = 1<<16;
#endif
}

#else
/*
 * TIM1CH4 DMA IRQ Handler
 */
void DMA1_Channel4_IRQHandler( void ) __attribute__((interrupt));
void DMA1_Channel4_IRQHandler( void ) 
{
#ifdef DAC_DIAG
	GPIOD->BSHR = 1;
#endif
	
	// why is this needed? Can't just direct compare the reg in tests below
	volatile uint16_t intfr = DMA1->INTFR;

	if( intfr & DMA1_IT_TC4 )
	{
		// Transfer complete - update 2nd half
		dac_update(dac_buffer+DACBUFSZ/2);
		
		// clear TC IRQ
		DMA1->INTFCR = DMA1_IT_TC4;
		
		GPIOC->BSHR = (1<<1); // NSS 1
	}
	
	if( intfr & DMA1_IT_HT4 )
	{
		// Half transfer - update first half
		dac_update(dac_buffer);
		
		// clear HT IRQ
		DMA1->INTFCR = DMA1_IT_HT4;
		
		GPIOC->BSHR = (1<<(1+16)); // NSS 0
	}

	// clear the Global IRQ
	DMA1->INTFCR = DMA1_IT_GL4;
	
#ifdef DAC_DIAG
	GPIOD->BSHR = 1<<16;
#endif
}

#endif


#endif
