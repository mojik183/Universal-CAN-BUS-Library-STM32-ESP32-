/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_port_virtual.c
 * @brief   Virtual backend implementation (software loopback and fault injection).
 */
#include <string.h>
#include "can_lib.h"

#if CAN_CFG_ENABLE_BACKEND_VIRTUAL

/** One virtual controller per instance number. */
static CanVirtualCtx_t s_ctx[CAN_CFG_MAX_INSTANCES];

/** Get the virtual controller of an instance, or NULL if it is not a virtual one. */
static CanVirtualCtx_t *ctx_of(const CanLib_t *can)
{
    if ((can == NULL) || (can->cfg.backend != CAN_BACKEND_VIRTUAL))
    {
        return NULL;
    }
    return (CanVirtualCtx_t *)can->port_ctx;
}

/** Push a frame into the simulated receive FIFO. Returns false when full. */
static bool ring_push(CanVirtualCtx_t *v, const CanFrame_t *frame)
{
    if (v->rx_count >= CAN_VIRTUAL_RX_DEPTH)
    {
        return false;
    }
    v->rx_ring[v->rx_head] = *frame;
    v->rx_head = (uint8_t)((v->rx_head + 1U) % CAN_VIRTUAL_RX_DEPTH);
    v->rx_count++;
    return true;
}

/* ---- backend functions ---- */

static CanResult_e virt_init(CanLib_t *can)
{
    CanVirtualCtx_t *v;

    if (can->instance >= CAN_CFG_MAX_INSTANCES)
    {
        return CAN_ERR_BAD_PARAM;
    }
    v = &s_ctx[can->instance];
    memset(v, 0, sizeof(*v));
    can->port_ctx = v;
    return CAN_OK;
}

static CanResult_e virt_deinit(CanLib_t *can)
{
    CanVirtualCtx_t *v = ctx_of(can);

    if (v != NULL)
    {
        v->started = false;
    }
    can->port_ctx = NULL;
    return CAN_OK;
}

static CanResult_e virt_start(CanLib_t *can)
{
    ctx_of(can)->started = true;
    return CAN_OK;
}

static CanResult_e virt_stop(CanLib_t *can)
{
    ctx_of(can)->started = false;
    return CAN_OK;
}

static CanResult_e virt_transmit(CanLib_t *can, const CanFrame_t *frame)
{
    CanVirtualCtx_t *v = ctx_of(can);

    if (!v->started)
    {
        return CAN_ERR_BAD_STATE;
    }
    if (v->bus_off)
    {
        return CAN_ERR_BUS_OFF;
    }
    if (v->tx_full)
    {
        return CAN_ERR_TX_FULL;
    }

    v->last_sent = *frame;
    v->sent_count++;

    if (can->cfg.mode == CANLIB_MODE_LOOPBACK)
    {
        (void)ring_push(v, frame);          /* Internal loopback: the node hears itself. */
    }
    return CAN_OK;
}

static CanResult_e virt_receive(CanLib_t *can, CanFrame_t *frame)
{
    CanVirtualCtx_t *v = ctx_of(can);

    if (v->rx_count == 0U)
    {
        return CAN_ERR_RX_EMPTY;
    }
    *frame = v->rx_ring[v->rx_tail];
    v->rx_tail = (uint8_t)((v->rx_tail + 1U) % CAN_VIRTUAL_RX_DEPTH);
    v->rx_count--;
    return CAN_OK;
}

static CanResult_e virt_get_errors(CanLib_t *can)
{
    CanVirtualCtx_t *v = ctx_of(can);

    can->bus.tec = v->tec;
    can->bus.rec = v->rec;
    can->bus.hw_error_flags = v->bus_off ? 1UL : 0UL;
    return v->bus_off ? CAN_ERR_BUS_OFF : CAN_OK;
}

static CanResult_e virt_recover(CanLib_t *can)
{
    CanVirtualCtx_t *v = ctx_of(can);

    v->bus_off = false;
    v->tec = 0U;
    v->rec = 0U;
    return CAN_OK;
}

#if CAN_CFG_ENABLE_XCVR_CONTROL
static CanResult_e virt_set_xcvr_mode(CanLib_t *can, CanXcvrMode_e mode)
{
    ctx_of(can)->xcvr_mode = mode;
    return CAN_OK;
}
#endif

/** Function table of the virtual backend. */
const CanPortOps_t g_CanPortVirtual =
{
    .name          = "virtual",
    .init          = virt_init,
    .deinit        = virt_deinit,
    .start         = virt_start,
    .stop          = virt_stop,
    .transmit      = virt_transmit,
    .receive       = virt_receive,
    .get_errors    = virt_get_errors,
    .recover       = virt_recover,
#if CAN_CFG_ENABLE_XCVR_CONTROL
    .set_xcvr_mode = virt_set_xcvr_mode,
#endif
};

/* ---- control functions for tests ---- */

CanResult_e CanVirtual_InjectRx(CanLib_t *can, const CanFrame_t *frame)
{
    CanVirtualCtx_t *v = ctx_of(can);

    if ((v == NULL) || (frame == NULL))
    {
        return CAN_ERR_NULL_PTR;
    }
    return ring_push(v, frame) ? CAN_OK : CAN_ERR_RX_OVERFLOW;
}

void CanVirtual_SetTxFull(CanLib_t *can, bool full)
{
    CanVirtualCtx_t *v = ctx_of(can);

    if (v != NULL)
    {
        v->tx_full = full;
    }
}

void CanVirtual_SetBusOff(CanLib_t *can, bool bus_off)
{
    CanVirtualCtx_t *v = ctx_of(can);

    if (v != NULL)
    {
        v->bus_off = bus_off;
    }
}

void CanVirtual_SetErrorCounters(CanLib_t *can, uint8_t tec, uint8_t rec)
{
    CanVirtualCtx_t *v = ctx_of(can);

    if (v != NULL)
    {
        v->tec = tec;
        v->rec = rec;
    }
}

const CanVirtualCtx_t *CanVirtual_GetCtx(const CanLib_t *can)
{
    return ctx_of(can);
}

#endif /* CAN_CFG_ENABLE_BACKEND_VIRTUAL */
