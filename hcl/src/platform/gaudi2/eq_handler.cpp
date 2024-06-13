#include "platform/gaudi2/eq_handler.h"

static char errorCauseQPCStrings[][64] = {
    "[TMR] max-retry-cnt exceeded",
    "[req DB] QP not valid",
    "[req DB] security check",
    "[req DB] PI > last-index",
    "[req DB] wq-type is READ",
    "[req TX] QP not valid",
    "[req TX] RDV WQE but wq-type is not WRITE",
    "[req RX] QP not valid",
    "[req RX] max-retry-cnt exceeded",
    "[req RDV] QP not valid",
    "[req RDV] wrong wq-type",
    "[req RDV] PI > last-index",
    "[res TX] QP not valid",
    "[res RX] max-retry-cnt exceeded",
};

static char errorCauseRXStrings[][64] = {
    "no error",
    "pkt bad format",
    "pkt tunnel invalid",
    "BTH opcode invalid",
    "syndrome invalid",
    "Reliable Connection max size invalid",
    "Reliable Connection min size invalid",
    "Raw min size invalid",
    "Raw max size invalid",
    "QP invalid",
    "Transport Service mismatch",
    "QPC Requester connection state invalid",
    "QPC Responder Connection state invalid",
    "QPC Responder resync invalid",
    "QPC Requester PSN invalid",
    "QPC Requester PSN unset",
    "QPC Responder RKEY invalid",
    "WQE index mismatch",
    "WQE write opcode invalid",
    "WQE Rendezvous opcode invalid",
    "WQE Read  opcode invalid",
    "WQE Write Zero",
    "WQE multi zero",
    "WQE Write send big",
    "WQE multi big",
};

static char errorCauseTXStrings[][128] = {
    "QPC.wq_type is write does not support WQE.opcode",
    "QPC.wq_type is rendezvous does not support WQE.opcode",
    "QPC.wq_type is read does not support WQE.opcode",
    "QPC.gaudi1 is set does not support WQE.opcode",
    "WQE.opcode is write but WQE.size is 0",
    "WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size is 0",
    "WQE.opcode is send but WQE.size is 0",
    "WQE.opcode is rendezvous-write|rendezvous-read but WQE.size is 0",
    "WQE.opcode is write but size > configured max-write-send-size",
    "WQE.opcode is multi-stride|local-stride|multi-dual but size > configured max-stride-size",
    "WQE.opcode is rendezvous-write|rendezvous-read but QPC.remote_wq_log_size <= configured min-remote-log-size",
    "WQE.opcode is rendezvous-write but WQE.size != configured rdv-wqe-size (per granularity)",
    "WQE.opcode is rendezvous-read but WQE.size != configured rdv-wqe-size (per granularity)",
    "WQE.inline is set but WQE.size != configured inline-wqe-size (per granularity)",
    "QPC.gaudi1 is set but WQE.inline is set",
    "WQE.opcode is multi-stride|local-stride|multi-dual but QPC.swq_granularity is 0",
    "WQE.opcode != NOP but WQE.reserved0 != 0",
    "WQE.opcode != NOP but WQE.wqe_index != execution-index [7.0]",
    "WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size < stride-size",
    "WQE.reduction_opcode is upscale but WQE.remote_address LSB is not 0",
    "WQE.reduction_opcode is upscale but does not support WQE.opcode",
    "RAW packet but WQE.size not supported",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "WQE.opcode is QoS but WQE.inline is set",
    "WQE.opcode above 15",
    "RAW above MIN",
    "RAW below MAX",
    "WQE.reduction is disable but reduction-opcode is not 0",
    "WQE.opcode is READ-RDV but WQE.inline is set",
    "TODO",
    "WQE fetch WR size not 4",
    "WQE fetch WR addr not mod4",
    "RDV last-index",
    "Gaudi1 multi-dual",
    "WQE bad opcode",
    "WQE bad size",
    "WQE SE not RAW",
    "Gaudi1 tunnal",
    "Tunnel 0-size",
    "Tunnel max size",
};

void Gaudi2EventQueueHandler::parseQpErrorParams(uint32_t ev_data, uint8_t& source, uint8_t& cause, uint8_t& qpcSource)
{
    source    = (ev_data >> 6) & 0x3;
    cause     = ev_data & 0x3f;
    qpcSource = 0;
}

char* Gaudi2EventQueueHandler::getErrorCauseRXStrings(uint8_t errorCause)
{
    return errorCauseRXStrings[errorCause];
}

char* Gaudi2EventQueueHandler::getErrorCauseQPCStrings(uint8_t errorCause, uint8_t errorQpcSource)
{
    return errorCauseQPCStrings[errorCause];
}

char* Gaudi2EventQueueHandler::getErrorCauseTXStrings(uint8_t errorCause)
{
    return errorCauseTXStrings[errorCause];
}
