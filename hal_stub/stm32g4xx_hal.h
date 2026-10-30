/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * Minimal stand-in of the ST HAL (FDCAN part, G4 values) for host tests. Never add it to a firmware project.
 */
#ifndef STUB_HAL_G4_H
#define STUB_HAL_G4_H
#include <stdint.h>
#include <stddef.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int x; } GPIO_TypeDef;
typedef struct { int x; } SPI_HandleTypeDef;
static inline uint32_t __get_IPSR(void) { return 0U; }
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t Delay);
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *pTxData, uint8_t *pRxData, uint16_t Size, uint32_t Timeout);

typedef struct { volatile uint32_t IR; } FDCAN_GlobalTypeDef;
#define FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST    (1UL << 3)
#define __HAL_FDCAN_GET_FLAG(h, f)          (((h)->Instance->IR & (f)) == (f))
#define __HAL_FDCAN_CLEAR_FLAG(h, f)        ((h)->Instance->IR &= ~(f))

#define FDCAN_CLOCK_DIV1            0x00000000U
#define FDCAN_FRAME_CLASSIC         0x00000000U
#define FDCAN_FRAME_FD_NO_BRS       0x00000200U
#define FDCAN_FRAME_FD_BRS          0x00000300U
#define FDCAN_MODE_NORMAL           0U
#define FDCAN_MODE_RESTRICTED_OPERATION 1U
#define FDCAN_MODE_BUS_MONITORING   2U
#define FDCAN_MODE_INTERNAL_LOOPBACK 3U
#define FDCAN_MODE_EXTERNAL_LOOPBACK 4U
#define FDCAN_TX_FIFO_OPERATION     0U
#define FDCAN_STANDARD_ID           0x00000000U
#define FDCAN_EXTENDED_ID           0x40000000U
#define FDCAN_DATA_FRAME            0x00000000U
#define FDCAN_REMOTE_FRAME          0x20000000U
#define FDCAN_ESI_ACTIVE            0x00000000U
#define FDCAN_ESI_PASSIVE           0x80000000U
#define FDCAN_BRS_OFF               0x00000000U
#define FDCAN_BRS_ON                0x00100000U
#define FDCAN_CLASSIC_CAN           0x00000000U
#define FDCAN_FD_CAN                0x00200000U
#define FDCAN_NO_TX_EVENTS          0x00000000U
#define FDCAN_RX_FIFO0              0x00000040U
#define FDCAN_ACCEPT_IN_RX_FIFO0    0x00000000U
#define FDCAN_FILTER_REMOTE         0x00000000U
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE 0x00000001U

typedef struct { uint32_t ClockDivider, FrameFormat, Mode; FunctionalState AutoRetransmission, TransmitPause, ProtocolException;
                 uint32_t NominalPrescaler, NominalSyncJumpWidth, NominalTimeSeg1, NominalTimeSeg2,
                          DataPrescaler, DataSyncJumpWidth, DataTimeSeg1, DataTimeSeg2,
                          StdFiltersNbr, ExtFiltersNbr, TxFifoQueueMode; } FDCAN_InitTypeDef;
typedef struct { FDCAN_GlobalTypeDef *Instance; FDCAN_InitTypeDef Init; uint32_t ErrorCode; } FDCAN_HandleTypeDef;
typedef struct { uint32_t Identifier, IdType, TxFrameType, DataLength, ErrorStateIndicator, BitRateSwitch, FDFormat,
                 TxEventFifoControl, MessageMarker; } FDCAN_TxHeaderTypeDef;
typedef struct { uint32_t Identifier, IdType, RxFrameType, DataLength, ErrorStateIndicator, BitRateSwitch, FDFormat,
                 RxTimestamp, FilterIndex, IsFilterMatchingFrame; } FDCAN_RxHeaderTypeDef;
typedef struct { uint32_t TxErrorCnt, RxErrorCnt, RxErrorPassive, ErrorLogging; } FDCAN_ErrorCountersTypeDef;
typedef struct { uint32_t LastErrorCode, DataLastErrorCode, Activity, ErrorPassive, Warning, BusOff, RxESIflag,
                 RxBRSflag, RxFDFflag, ProtocolException, TDCvalue; } FDCAN_ProtocolStatusTypeDef;

HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *hfdcan);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *hfdcan, uint32_t NonMatchingStd, uint32_t NonMatchingExt, uint32_t RejectRemoteStd, uint32_t RejectRemoteExt);
HAL_StatusTypeDef HAL_FDCAN_ConfigTxDelayCompensation(FDCAN_HandleTypeDef *hfdcan, uint32_t TdcOffset, uint32_t TdcFilter);
HAL_StatusTypeDef HAL_FDCAN_EnableTxDelayCompensation(FDCAN_HandleTypeDef *hfdcan);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *hfdcan);
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *hfdcan);
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *hfdcan, uint32_t ActiveITs, uint32_t BufferIndexes);
HAL_StatusTypeDef HAL_FDCAN_DeactivateNotification(FDCAN_HandleTypeDef *hfdcan, uint32_t InactiveITs);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *hfdcan, const FDCAN_TxHeaderTypeDef *pTxHeader, const uint8_t *pTxData);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *hfdcan);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *hfdcan, uint32_t RxLocation, FDCAN_RxHeaderTypeDef *pRxHeader, uint8_t *pRxData);
HAL_StatusTypeDef HAL_FDCAN_GetErrorCounters(const FDCAN_HandleTypeDef *hfdcan, FDCAN_ErrorCountersTypeDef *ErrorCounters);
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef *hfdcan, FDCAN_ProtocolStatusTypeDef *ProtocolStatus);
uint32_t HAL_FDCAN_GetError(const FDCAN_HandleTypeDef *hfdcan);
#endif
