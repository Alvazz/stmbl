/*
* This file is part of the stmbl project.
*
* Copyright (C) 2013-2017 Rene Hopf <renehopf@mac.com>
* Copyright (C) 2013-2017 Nico Stute <crinq@crinq.de>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "main.h"
#include "stm32f3xx_hal.h"
#include "version.h"
#include "common.h"
#include "f3hw.h"
#include "tim.h"

uint32_t systick_freq;
CRC_HandleTypeDef hcrc;

volatile bootloader_t rx_buf;
volatile bootloader_t tx_buf;


void SystemClock_Config(void);
void Error_Handler(void);

void TIM8_UP_IRQHandler() {
  __HAL_TIM_CLEAR_IT(&htim8, TIM_IT_UPDATE);
  tx_buf.opcode = BOOTLOADER_OPCODE_READ;
  tx_buf.state = BOOTLOADER_STATE_OK;
  tx_buf.addr = 0;
  tx_buf.data = 0;

  tx_buf.crc = HAL_CRC_Calculate(&hcrc, (uint32_t *)&tx_buf, sizeof(bootloader_t) / 4 - 1);

  // start tx DMA
  DMA1_Channel2->CCR &= (uint16_t)(~DMA_CCR_EN);
  DMA1_Channel2->CNDTR = sizeof(bootloader_t);
  DMA1_Channel2->CCR |= DMA_CCR_EN;
}

void uart_init(){
  GPIO_InitTypeDef GPIO_InitStruct;

  /* Peripheral clock enable */
  __HAL_RCC_USART3_CLK_ENABLE();

  UART_HandleTypeDef huart3;
  huart3.Instance                    = USART3;
  huart3.Init.BaudRate               = DATABAUD;
  huart3.Init.WordLength             = UART_WORDLENGTH_8B;
  huart3.Init.StopBits               = UART_STOPBITS_1;
  huart3.Init.Parity                 = UART_PARITY_NONE;
  huart3.Init.Mode                   = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling           = UART_OVERSAMPLING_8;
  huart3.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  USART3->CR3 |= USART_CR3_DMAT | USART_CR3_DMAR | USART_CR3_OVRDIS;
  HAL_UART_Init(&huart3);

  /**USART3 GPIO Configuration    
   PB10     ------> USART3_TX
   PB11     ------> USART3_RX 
   */
  GPIO_InitStruct.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_PULLUP;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  __HAL_RCC_DMA1_CLK_ENABLE();

  //TX DMA
  DMA1_Channel2->CCR &= (uint16_t)(~DMA_CCR_EN);
  DMA1_Channel2->CPAR  = (uint32_t) & (USART3->TDR);
  DMA1_Channel2->CMAR  = (uint32_t) & tx_buf;
  DMA1_Channel2->CNDTR = sizeof(packet_from_hv_t);
  DMA1_Channel2->CCR   = DMA_CCR_MINC | DMA_CCR_DIR;  // | DMA_CCR_PL_0 | DMA_CCR_PL_1
  DMA1->IFCR           = DMA_IFCR_CTCIF2 | DMA_IFCR_CHTIF2 | DMA_IFCR_CGIF2;

  //RX DMA
  DMA1_Channel3->CCR &= (uint16_t)(~DMA_CCR_EN);
  DMA1_Channel3->CPAR  = (uint32_t) & (USART3->RDR);
  DMA1_Channel3->CMAR  = (uint32_t) & rx_buf;
  DMA1_Channel3->CNDTR = sizeof(packet_to_hv_t);
  DMA1_Channel3->CCR   = DMA_CCR_MINC;  // | DMA_CCR_PL_0 | DMA_CCR_PL_1
  DMA1->IFCR           = DMA_IFCR_CTCIF3 | DMA_IFCR_CHTIF3 | DMA_IFCR_CGIF3;
  DMA1_Channel3->CCR |= DMA_CCR_EN;

  USART3->RTOR = 16;  // 16 bits timeout
  USART3->CR2 |= USART_CR2_RTOEN;  // timeout en
  USART3->ICR |= USART_ICR_RTOCF;  // timeout clear flag
}

int main(void) {
  // Relocate interrupt vectors
  extern void *g_pfnVectors;
  SCB->VTOR = (uint32_t)&g_pfnVectors;

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();
  systick_freq = HAL_RCC_GetHCLKFreq();
  /* Initialize all configured peripherals */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_RTC_ENABLE();

  hcrc.Instance                     = CRC;
  hcrc.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat              = CRC_INPUTDATA_FORMAT_WORDS;

  __HAL_RCC_CRC_CLK_ENABLE();

  if(HAL_CRC_Init(&hcrc) != HAL_OK) {
    Error_Handler();
  }

  uart_init();
  MX_TIM8_Init();
  if(HAL_TIM_Base_Start_IT(&htim8) != HAL_OK) {
    Error_Handler();
  }


  while(1) {
    
  }
}

/** System Clock Configuration
*/
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

  /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState       = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /**Initializes the CPU, AHB and APB busses clocks 
    */
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_USART3 | RCC_PERIPHCLK_TIM8 | RCC_PERIPHCLK_ADC12 | RCC_PERIPHCLK_ADC34 | RCC_PERIPHCLK_RTC;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  PeriphClkInit.Usart3ClockSelection = RCC_USART3CLKSOURCE_SYSCLK;
  PeriphClkInit.Adc12ClockSelection  = RCC_ADC12PLLCLK_DIV1;
  PeriphClkInit.Adc34ClockSelection  = RCC_ADC34PLLCLK_DIV1;
  PeriphClkInit.USBClockSelection    = RCC_USBCLKSOURCE_PLL_DIV1_5;
  PeriphClkInit.Tim8ClockSelection   = RCC_TIM8CLK_PLLCLK;
  PeriphClkInit.RTCClockSelection    = RCC_RTCCLKSOURCE_LSI;
  if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
    Error_Handler();
  }

  /**Configure the Systick interrupt time 
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);

  /**Configure the Systick 
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

//Delay implementation for hal_term.c
void Wait(uint32_t ms) {
  HAL_Delay(ms);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void Error_Handler(void) {
  /* User can add his own implementation to report the HAL error return state */
  while(1) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  }
}

#ifdef USE_FULL_ASSERT

/**
   * @brief Reports the name of the source file and the source line number
   * where the assert_param error has occurred.
   * @param file: pointer to the source file name
   * @param line: assert_param error line source number
   * @retval None
   */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
    ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}

#endif
