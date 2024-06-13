#pragma once

#include <cstddef>                // for size_t
#include <cstdint>                // for uint32_t
#include <map>                    // for map
#include <queue>                  // for queue
#include <thread>                 // for thread
#include <vector>                 // for vector

#include "infra/concurrent_unordered_map.hpp"  // for ConcurrentUnorderedMap
#include "infra/concurrent_queue.hpp"          // for ConcurrentQueue
#include "infra/hcl_mpsc_fifo.h"  // for mpsc_fifo_t
#include "hccl_internal_defs.h"   // for msg_header_t

struct hcclHandle;
struct hcclInternalHandle;

#define JOBS_QUEUE_CAPACITY 10000

typedef int Ack;
#define ACK_WAIT  0
#define ACK_VALID 999

enum ConnectionOperations
{
    TCP_SEND = 0,
    TCP_RECV,
};

struct SocketJob
{
    int                  m_socketFd = -1;
    ConnectionOperations m_jobType;
    size_t               m_size;
    void*                m_address;
    int                  m_jobId;
    uint32_t             m_sequence;
    hcclInternalHandle*  m_handle = nullptr;
};

struct PendingJob
{
    msg_header_t hdr;
    void*        jobAddr;
};

class SocketThread
{
public:
    SocketThread() = default;
    SocketThread(int globalRank, int socketThreadId);
    ~SocketThread();
    bool pushJob(ConnectionOperations jobType, size_t size, void* address, int socket, hcclHandle* handle = nullptr);
    bool pushAsyncJob(ConnectionOperations jobType,
                      size_t               size,
                      void*                address,
                      int                  peer,
                      uint32_t             sequence,
                      hcclHandle*          handle = nullptr);
    void startThread();
    void startAsyncThread(int socket);
    void stopThread();
    void stopAsyncThread();
    void runThread();
    void runAsyncThread();
    bool isEmpty() const;
    bool destroy();

private:
    void initialize();
    void initializeAsync();
    bool executeJob(SocketJob* job);
    bool executeRecv(SocketJob* job);
    bool executeSend(SocketJob* job);
    void runPendingJobs();

    mpsc_fifo_t<SocketJob*, JOBS_QUEUE_CAPACITY> m_jobsQueue;
    std::thread                                  m_thread;
    volatile bool                                m_stop = true;
    int                                          m_globalRank;
    int                                          m_socketThreadId;
    int                                          m_socket  = -1;
    bool                                         m_isAsync = false;
    std::map<int, std::queue<SocketJob>>         m_Asyncjobs;
    std::vector<PendingJob>                      m_PendingJobs;
};

class SocketThreadsManager
{
public:
    SocketThreadsManager();
    void initThreads(int nThreads, int globalId);
    ~SocketThreadsManager();
    bool pushJob(ConnectionOperations jobType, size_t size, void* address, int socket, hcclHandle* handle = nullptr);
    bool pushAsyncJob(ConnectionOperations jobType,
                      size_t               size,
                      void*                address,
                      unsigned             peer,
                      uint32_t             sequence,
                      hcclHandle*          handle = nullptr);
    void createAsyncThread(int socket, int globalId);
    void startThreads();
    void stopThreads();
    bool destroy();

private:
    std::vector<SocketThread*> m_threadPool;
    std::vector<SocketThread*> m_asyncThreadPool;
    unsigned                   m_nextThread;
    unsigned                   m_nThreads;
    unsigned                   m_nAsyncThreads;
};
