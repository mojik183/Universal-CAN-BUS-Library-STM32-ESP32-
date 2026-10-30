/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * Minimal stand-in of the ST HAL CAN API for host tests (names and values follow the real HAL).
 */
#ifndef STUB_HAL_H
#define STUB_HAL_H
#include <stdint.h>
#include <stddef.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int x; } GPIO_TypeDef;
typedef struct { int x; } SPI_HandleTypeDef;

typedef struct { volatile uint32_t ESR; volatile uint32_t RF0R; } CAN_TypeDef;
#define CAN_RF0R_FOVR0      (1UL << 4)
#define CAN_MODE_NORMAL     0x00000000U
#define CAN_MODE_LOOPBACK   0x40000000U
#define CAN_MODE_SILENT     0x80000000U
#define CAN_FILTERMODE_IDMASK   0U
#define CAN_FILTERSCALE_32BIT   1U
#define CAN_FILTER_FIFO0        0U
#define CAN_FILTER_ENABLE       1U
#define CAN_RX_FIFO0            0U
#define CAN_IT_RX_FIFO0_MSG_PENDING 0x2U
#define CAN_ID_STD  0x0U
#define CAN_ID_EXT  0x4U
#define CAN_RTR_DATA   0x0U
#define CAN_RTR_REMOTE 0x2U

typedef struct { uint32_t Prescaler, Mode, SyncJumpWidth, TimeSeg1, TimeSeg2;
                 FunctionalState TimeTriggeredMode, AutoBusOff, AutoWakeUp, AutoRetransmission,
                                 ReceiveFifoLocked, TransmitFifoPriority; } CAN_InitTypeDef;
typedef struct { CAN_TypeDef *Instance; CAN_InitTypeDef Init; uint32_t ErrorCode; } CAN_HandleTypeDef;
typedef struct { uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow,
                 FilterFIFOAssignment, FilterBank, FilterMode, FilterScale, FilterActivation,
                 SlaveStartFilterBank; } CAN_FilterTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; FunctionalState TransmitGlobalTime; } CAN_TxHeaderTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, Timestamp, FilterMatchIndex; } CAN_RxHeaderTypeDef;

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t Delay);
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *pTxData, uint8_t *pRxData, uint16_t Size, uint32_t Timeout);
uint32_t HAL_RCC_GetPCLK1Freq(void);
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *hcan, const CAN_FilterTypeDef *sFilterConfig);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *hcan);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *hcan, uint32_t ActiveITs);
HAL_StatusTypeDef HAL_CAN_DeactivateNotification(CAN_HandleTypeDef *hcan, uint32_t InactiveITs);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *hcan, const CAN_TxHeaderTypeDef *pHeader, const uint8_t aData[], uint32_t *pTxMailbox);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *hcan);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *hcan, uint32_t RxFifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *hcan, uint32_t RxFifo, CAN_RxHeaderTypeDef *pHeader, uint8_t aData[]);
uint32_t HAL_CAN_GetError(const CAN_HandleTypeDef *hcan);
static inline uint32_t __get_IPSR(void) { return 0U; }   /* CMSIS: compile check of the RTOS layer only. */
#endif
