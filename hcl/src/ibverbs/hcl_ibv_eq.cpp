#include "hcl_ibverbs.h"
#include "helpers.h"
#include "hcl_types.h"  // for NicLkdEventsEnum

// non-trivial designated initializers not supported
#define MAX_ERRORS (IBV_EVENT_WQ_FATAL + 1)
static const char* err2str[MAX_ERRORS] = {};

#define MAX_SYNDROMS 0x100
static const char* qp_syndroms[MAX_SYNDROMS] = {};

static void init_error_tables()
{
    err2str[IBV_EVENT_QP_FATAL]            = "QP fatal event";
    err2str[IBV_EVENT_QP_REQ_ERR]          = "QP requester error";
    err2str[IBV_EVENT_QP_LAST_WQE_REACHED] = "QP align counters";
    err2str[IBV_EVENT_PATH_MIG]            = "WTD security error";
    err2str[IBV_EVENT_PATH_MIG_ERR]        = "Numerical error";
    err2str[IBV_EVENT_PORT_ACTIVE]         = "Link up";
    err2str[IBV_EVENT_PORT_ERR]            = "Link down";
    err2str[IBV_EVENT_LID_CHANGE]          = "Link shutdown";
    err2str[IBV_EVENT_GID_CHANGE]          = "GID table change",

    /* Rx packet errors*/
        qp_syndroms[0x1] = "[RX] pkt err, pkt bad format";
    qp_syndroms[0x2]     = "[RX] pkt err, pkt tunnel invalid";
    qp_syndroms[0x3]     = "[RX] pkt err, BTH opcode invalid";
    qp_syndroms[0x4]     = "[RX] pkt err, syndrome invalid";
    qp_syndroms[0x5]     = "[RX] pkt err, Reliable QP max size invalid";
    qp_syndroms[0x6]     = "[RX] pkt err, Reliable QP min size invalid";
    qp_syndroms[0x7]     = "[RX] pkt err, Raw min size invalid";
    qp_syndroms[0x8]     = "[RX] pkt err, Raw max size invalid";
    qp_syndroms[0x9]     = "[RX] pkt err, QP invalid";
    qp_syndroms[0xa]     = "[RX] pkt err, Transport Service mismatch";
    qp_syndroms[0xb]     = "[RX] pkt err, QPC Requester QP state invalid";
    qp_syndroms[0xc]     = "[RX] pkt err, QPC Responder QP state invalid";
    qp_syndroms[0xd]     = "[RX] pkt err, QPC Responder resync invalid";
    qp_syndroms[0xe]     = "[RX] pkt err, QPC Requester PSN invalid";
    qp_syndroms[0xf]     = "[RX] pkt err, QPC Requester PSN unset";
    qp_syndroms[0x10]    = "[RX] pkt err, QPC Responder RKEY invalid";
    qp_syndroms[0x11]    = "[RX] pkt err, WQE index mismatch";
    qp_syndroms[0x12]    = "[RX] pkt err, WQE write opcode invalid";
    qp_syndroms[0x13]    = "[RX] pkt err, WQE Rendezvous opcode invalid";
    qp_syndroms[0x14]    = "[RX] pkt err, WQE Read  opcode invalid";
    qp_syndroms[0x15]    = "[RX] pkt err, WQE Write Zero";
    qp_syndroms[0x16]    = "[RX] pkt err, WQE multi zero";
    qp_syndroms[0x17]    = "[RX] pkt err, WQE Write send big";
    qp_syndroms[0x18]    = "[RX] pkt err, WQE multi big";

    /* QPC errors */
    qp_syndroms[0x40] = "[qpc] [TMR] max-retry-cnt exceeded";
    qp_syndroms[0x41] = "[qpc] [req DB] QP not valid";
    qp_syndroms[0x42] = "[qpc] [req DB] security check";
    qp_syndroms[0x43] = "[qpc] [req DB] PI > last-index";
    qp_syndroms[0x44] = "[qpc] [req DB] wq-type is READ";
    qp_syndroms[0x45] = "[qpc] [req TX] QP not valid";
    qp_syndroms[0x46] = "[qpc] [req TX] Rendezvous WQE but wq-type is not WRITE";
    qp_syndroms[0x47] = "[qpc] [req RX] QP not valid";
    qp_syndroms[0x48] = "[qpc] [req RX] max-retry-cnt exceeded";
    qp_syndroms[0x49] = "[qpc] [req RDV] QP not valid";
    qp_syndroms[0x4a] = "[qpc] [req RDV] wrong wq-type";
    qp_syndroms[0x4b] = "[qpc] [req RDV] PI > last-index";
    qp_syndroms[0x4c] = "[qpc] [res TX] QP not valid";
    qp_syndroms[0x4d] = "[qpc] [res RX] max-retry-cnt exceeded";

    /* tx packet error */
    qp_syndroms[0x80] = "[TX] pkt error, QPC.wq_type is write does not support WQE.opcode";
    qp_syndroms[0x81] = "[TX] pkt error, QPC.wq_type is rendezvous does not support WQE.opcode";
    qp_syndroms[0x82] = "[TX] pkt error, QPC.wq_type is read does not support WQE.opcode";
    qp_syndroms[0x83] = "[TX] pkt error, QPC.gaudi1 is set does not support WQE.opcode";
    qp_syndroms[0x84] = "[TX] pkt error, WQE.opcode is write but WQE.size is 0";
    qp_syndroms[0x85] = "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size is 0";
    qp_syndroms[0x86] = "[TX] pkt error, WQE.opcode is send but WQE.size is 0";
    qp_syndroms[0x87] = "[TX] pkt error, WQE.opcode is rendezvous-write|rendezvous-read but WQE.size is 0";
    qp_syndroms[0x88] = "[TX] pkt error, WQE.opcode is write but size > configured max-write-send-size";
    qp_syndroms[0x89] =
        "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but size > configured max-stride-size";
    qp_syndroms[0x8a] = "[TX] pkt error, WQE.opcode is rendezvous-write|rendezvous-read but QPC.remote_wq_log_size <= "
                        "configured min-remote-log-size";
    qp_syndroms[0x8b] =
        "[TX] pkt error, WQE.opcode is rendezvous-write but WQE.size != configured rdv-wqe-size (per granularity)";
    qp_syndroms[0x8c] =
        "[TX] pkt error, WQE.opcode is rendezvous-read but WQE.size != configured rdv-wqe-size (per granularity)";
    qp_syndroms[0x8d] =
        "[TX] pkt error, WQE.inline is set but WQE.size != configured inline-wqe-size (per granularity)";
    qp_syndroms[0x8e] = "[TX] pkt error, QPC.gaudi1 is set but WQE.inline is set";
    qp_syndroms[0x8f] =
        "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but QPC.swq_granularity is 0";
    qp_syndroms[0x90] = "[TX] pkt error, WQE.opcode != NOP but WQE.reserved0 != 0";
    qp_syndroms[0x91] = "[TX] pkt error, WQE.opcode != NOP but WQE.wqe_index != execution-index [7.0]";
    qp_syndroms[0x92] = "[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size < stride-size";
    qp_syndroms[0x93] = "[TX] pkt error, WQE.reduction_opcode is upscale but WQE.remote_address LSB is not 0";
    qp_syndroms[0x94] = "[TX] pkt error, WQE.reduction_opcode is upscale but does not support WQE.opcode";
    qp_syndroms[0x95] = "[TX] pkt error, RAW packet but WQE.size not supported";
    qp_syndroms[0xA0] = "WQE.opcode is QoS but WQE.inline is set";
    qp_syndroms[0xA1] = "WQE.opcode above 15";
    qp_syndroms[0xA2] = "RAW above MIN";
    qp_syndroms[0xA3] = "RAW below MAX";
    qp_syndroms[0xA4] = "WQE.reduction is disable but reduction-opcode is not 0";
    qp_syndroms[0xA5] = "WQE.opcode is READ-RDV but WQE.inline is set";
    qp_syndroms[0xA7] = "WQE fetch WR size not 4";
    qp_syndroms[0xA8] = "WQE fetch WR addr not mod4";
    qp_syndroms[0xA9] = "RDV last-index";
    qp_syndroms[0xAA] = "Gaudi1 multi-dual";
    qp_syndroms[0xAB] = "WQE bad opcode";
    qp_syndroms[0xAC] = "WQE bad size";
    qp_syndroms[0xAD] = "WQE SE not RAW";
    qp_syndroms[0xAE] = "Gaudi1 tunnel";
    qp_syndroms[0xAF] = "Tunnel 0-size";
    qp_syndroms[0xB0] = "Tunnel max size";
};

#define SYNDROME_TYPE(syndrome) (((syndrome) >> 6) & 0x3)

const char* parse_qp_syndrome(uint32_t syndrome)
{
    int         syndrome_type;
    const char* str = nullptr;

    /* syndrome comprised from 8 bits
     * [2:type, 6:syndrome]
     * 6 bits for syndrome
     * 2 bits for type
     *   0 - rx packet error
     *   1 - qp error
     *   2 - tx packet error
     */

    if (syndrome >= MAX_SYNDROMS)
    {
        syndrome_type = SYNDROME_TYPE(syndrome);

        switch (syndrome_type)
        {
            case 0:
                str = "RX packet syndrome unknown";
                break;
            case 1:
                str = "QPC syndrome unknown";
                break;
            case 2:
                str = "TX packet syndrome unknown";
                break;
            default:
                str = "syndrome unknown";
                break;
        }
    }
    else
    {
        str = qp_syndroms[syndrome];
    }

    if (str == nullptr)
    {
        str = "Could not parse syndrome";
    }
    return str;
}

void hcl_ibverbs_t::eq_poll(bool& stop, uint32_t _usleep)
{
    LOG_IBV("");

    init_error_tables();

    poll_stop_ = &stop;
    poll_done_ = false;

    ibv_async_event ibev = {};
    pollfd          pfd  = {};

    auto flags = fcntl(ibctx_->async_fd, F_GETFL);
    int  rc    = fcntl(ibctx_->async_fd, F_SETFL, flags | O_NONBLOCK);
    VERIFY(rc == 0, "fcntl failed: {}", rc);

    pfd.fd      = ibctx_->async_fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    while (!stop)
    {
        rc = poll(&pfd, 1, _usleep / 1000);  // usec -> msec
        if (!rc) continue;

        if (ibctx_ == nullptr)
        {
            WRN_IBV("ibctx_ is nullptr (disappeared from under our feet). Did you properly destroy SynapseAI?");
            break;
        }
        /* Read an asynchronous event for this context */
        rc = ibv_.ibv_get_async_event(ibctx_, &ibev);
        if (rc) continue;

        if (parse_ib_eqe(&ibev))
        {
            hclNotifyFailureV2(DfaErrorCode::hclFailed, 0, "IB Async Event. DFA triggered");
            std::terminate();
        }
    }

    poll_done_ = true;
}

// returning true will trigger termination
bool hcl_ibverbs_t::parse_ib_eqe(ibv_async_event* event)
{
    union port_data_t
    {
        struct
        {
            uint32_t port : 16;  // 0  - 15
            uint32_t extd : 16;  // 16 - 31
        };
        uint32_t _raw = 0;
        port_data_t(int _val) : _raw(_val) {};
    };

    bool triggerDFA = true;

    /* There is no way to pass syndrome value to user via IBv
     * QP error syndromes will be printed to the dmesg.
     */

    switch (event->event_type)
    {
        case IBV_EVENT_QP_FATAL:
        case IBV_EVENT_QP_REQ_ERR:
        case IBV_EVENT_QP_LAST_WQE_REACHED:
        case IBV_EVENT_PATH_MIG:
        case IBV_EVENT_PATH_MIG_ERR:
        {
            hbldv_query_qp_attr dv_qp_attr   = {};
            ibv_qp_init_attr    qp_init_attr = {};
            ibv_qp_attr         qp_attr      = {};

            ibv_qp* evqp = event->element.qp;

            ibv_.hbldv_query_qp(evqp, &dv_qp_attr);
            ibv_.ibv_query_qp(evqp, &qp_attr, IBV_QP_PORT | IBV_QP_ACCESS_FLAGS, &qp_init_attr);

            uint32_t syndrome = qp_attr.qp_access_flags;
            uint32_t nic      = port2nic_[qp_attr.port_num];

            CRT_IBV("{}. QP({}), NIC({}), STATE({}), SYNDROME({}): {}",
                    err2str[event->event_type],
                    dv_qp_attr.qp_num,
                    nic,
                    evqp->state,
                    syndrome,
                    parse_qp_syndrome(syndrome));
            break;
        }

        case IBV_EVENT_CQ_ERR:
        {
            ibv_cq* evcq = event->element.cq;
            CRT_IBV("CQ error. CQ({}) CQE({})", evcq, evcq->cqe);
            break;
        }

        case IBV_EVENT_PORT_ACTIVE:
        case IBV_EVENT_PORT_ERR:
        case IBV_EVENT_LID_CHANGE:
        {
            const uint32_t nic = port2nic_[event->element.port_num];
            triggerDFA         = false;
            INF_IBV("{}, NIC({})", err2str[event->event_type], nic);

            report_nic_status(event->event_type, nic);
            break;
        }

        case IBV_EVENT_SM_CHANGE:
        {
            port_data_t data(event->element.port_num);
            CRT_IBV("NIC({}) got completion on congestion CQ({})", port2nic_[data.port], data.extd);
            break;
        }

        case IBV_EVENT_DEVICE_FATAL:
            /* if port was provided by the driver, it is probably FIFO-error */
            if (event->element.port_num)
            {
                port_data_t data(event->element.port_num);
                CRT_IBV("NIC({}) got DB_FIFO({}) error", port2nic_[data.port], data.extd);
            }
            else
            {
                CRT_IBV("Device fatal event");
            }
            break;

        case IBV_EVENT_GID_CHANGE:
            triggerDFA = false;
            INF_IBV("{}, NIC({}), reparsing data.", err2str[event->event_type], port2nic_[event->element.port_num]);
            parse_sysfs_infiniband();
            break;

        default:
            CRT_IBV("Unknown event ({})", event->event_type);
            break;
    }

    ibv_.ibv_ack_async_event(event);

    return triggerDFA;
}

void hcl_ibverbs_t::report_nic_status(const ibv_event_type& event, const uint32_t nic)
{
    switch (event)
    {
        case IBV_EVENT_PORT_ACTIVE:
            device_->updateNicState(nic, NicLkdEventsEnum::NIC_LKD_EVENTS_UP, false);
            break;
        case IBV_EVENT_PORT_ERR:
            device_->updateNicState(nic, NicLkdEventsEnum::NIC_LKD_EVENTS_DOWN, false);
            break;
        case IBV_EVENT_LID_CHANGE:
            device_->updateNicState(nic, NicLkdEventsEnum::NIC_LKD_EVENTS_SHUTDOWN, false);
            break;
        default:
            CRT_IBV("unknown NIC event: {}", err2str[event]);
    }
}
