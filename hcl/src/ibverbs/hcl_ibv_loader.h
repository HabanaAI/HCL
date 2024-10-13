#pragma once

#include <cstdint>

typedef int (*hbldv_set_port_ex_fn)(struct ibv_context* context, struct hbldv_port_ex_attr* attr);
typedef struct ibv_cq* (*hbldv_create_cq_fn)(struct ibv_context*      context,
                                             int                      cqe,
                                             struct ibv_comp_channel* channel,
                                             int                      comp_vector,
                                             struct hbldv_cq_attr*    cq_attr);
typedef int (*hbldv_query_qp_fn)(struct ibv_qp* ibvqp, struct hbldv_query_qp_attr* qp_attr);
typedef int (*hbldv_reserve_coll_qps_fn)(struct ibv_pd*             ibvpd,
                                         struct hbldv_coll_qp_attr* coll_qp_attr,
                                         struct hbldv_coll_qp*      coll_qp);
typedef int (*hbldv_modify_qp_fn)(struct ibv_qp*        ibqp,
                                  struct ibv_qp_attr*   attr,
                                  int                   attr_mask,
                                  struct hbldv_qp_attr* hl_attr);

typedef int (*hbldv_query_qp_fn)(struct ibv_qp* ibvqp, struct hbldv_query_qp_attr* qp_attr);
typedef struct hbldv_usr_fifo* (*hbldv_create_usr_fifo_fn)(struct ibv_context*         context,
                                                           struct hbldv_usr_fifo_attr* attr);
typedef int (*hbldv_destroy_usr_fifo_fn)(struct hbldv_usr_fifo* usr_fifo);
typedef int (*hbldv_query_port_fn)(struct ibv_context*           context,
                                   uint32_t                      port_num,
                                   struct hbldv_query_port_attr* hl_attr);
typedef bool (*hbldv_is_supported_fn)(struct ibv_device* device);

typedef struct ibv_context* (*hbldv_open_device_fn)(struct ibv_device* device, struct hbldv_ucontext_attr* attr);
typedef const char* (*ibv_get_device_name_fn)(struct ibv_device* device);
typedef struct ibv_device** (*ibv_get_device_list_fn)(int* num_devices);
typedef void (*ibv_free_device_list_fn)(struct ibv_device** list);
typedef int (*ibv_close_device_fn)(struct ibv_context* context);
typedef struct ibv_pd* (*ibv_alloc_pd_fn)(struct ibv_context* context);
typedef int (*ibv_dealloc_pd_fn)(struct ibv_pd* pd);
typedef int (*ibv_destroy_cq_fn)(struct ibv_cq* cq);
typedef struct ibv_qp* (*ibv_create_qp_fn)(struct ibv_pd* pd, struct ibv_qp_init_attr* qp_init_attr);
typedef int (*ibv_modify_qp_fn)(struct ibv_qp* qp, struct ibv_qp_attr* attr, int attr_mask);
typedef int (*ibv_destroy_qp_fn)(struct ibv_qp* qp);
typedef int (*ibv_get_async_event_fn)(struct ibv_context* context, struct ibv_async_event* event);
typedef void (*ibv_ack_async_event_fn)(struct ibv_async_event* event);
typedef int (*ibv_query_port_fn)(struct ibv_context* context, uint8_t port_num, struct ibv_port_attr* port_attr);
typedef int (*ibv_query_qp_fn)(struct ibv_qp*           qp,
                               struct ibv_qp_attr*      attr,
                               int                      attr_mask,
                               struct ibv_qp_init_attr* init_attr);
typedef int (*ibv_query_gid_fn)(struct ibv_context* context, uint8_t port_num, int index, union ibv_gid* gid);
typedef int (*hbldv_query_device_fn)(struct ibv_context* context, struct hbldv_device_attr* attr);
enum ibv_gid_type_sysfs
{
    IBV_GID_TYPE_SYSFS_IB_ROCE_V1,
    IBV_GID_TYPE_SYSFS_ROCE_V2,
    IBV_GID_TYPE_SYSFS_UNDEFINED,
};
typedef int (*ibv_query_gid_type_fn)(struct ibv_context* context,
                                     uint8_t             port_num,
                                     unsigned int        index,
                                     ibv_gid_type_sysfs* type);

class ibv_lib_t
{
public:
    hbldv_open_device_fn      hbldv_open_device      = nullptr;
    hbldv_set_port_ex_fn      hbldv_set_port_ex      = nullptr;
    hbldv_create_cq_fn        hbldv_create_cq        = nullptr;
    hbldv_query_qp_fn         hbldv_query_qp         = nullptr;
    hbldv_modify_qp_fn        hbldv_modify_qp        = nullptr;
    hbldv_create_usr_fifo_fn  hbldv_create_usr_fifo  = nullptr;
    hbldv_destroy_usr_fifo_fn hbldv_destroy_usr_fifo = nullptr;
    hbldv_reserve_coll_qps_fn hbldv_reserve_coll_qps = nullptr;
    hbldv_query_port_fn       hbldv_query_port       = nullptr;
    hbldv_is_supported_fn     hbldv_is_supported     = nullptr;
    hbldv_query_device_fn     hbldv_query_device     = nullptr;

    ibv_get_device_name_fn  ibv_get_device_name  = nullptr;
    ibv_get_device_list_fn  ibv_get_device_list  = nullptr;
    ibv_free_device_list_fn ibv_free_device_list = nullptr;
    ibv_close_device_fn     ibv_close_device     = nullptr;
    ibv_alloc_pd_fn         ibv_alloc_pd         = nullptr;
    ibv_dealloc_pd_fn       ibv_dealloc_pd       = nullptr;
    ibv_destroy_cq_fn       ibv_destroy_cq       = nullptr;
    ibv_create_qp_fn        ibv_create_qp        = nullptr;
    ibv_modify_qp_fn        ibv_modify_qp        = nullptr;
    ibv_destroy_qp_fn       ibv_destroy_qp       = nullptr;
    ibv_get_async_event_fn  ibv_get_async_event  = nullptr;
    ibv_ack_async_event_fn  ibv_ack_async_event  = nullptr;
    ibv_query_port_fn       ibv_query_port       = nullptr;
    ibv_query_qp_fn         ibv_query_qp         = nullptr;
    ibv_query_gid_fn        ibv_query_gid        = nullptr;
    ibv_query_gid_type_fn   ibv_query_gid_type   = nullptr;

    ~ibv_lib_t();

    bool  load();
    void* lib_handle() { return hlib_handle; }

protected:
    bool  loaded      = false;
    void* hlib_handle = nullptr;

    void unload();
};

extern ibv_lib_t g_ldr;
