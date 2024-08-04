#include "socket_thread.h"

#include <cstdlib>                       // for free, size_t
#include <sys/socket.h>                  // for shutdown, SHUT_RD
#include <chrono>                        // for operator-, operator>, high_r...
#include <type_traits>                   // for __success_type<>::type
#include "hcl_tcp_utils.h"               // for recvAllFromSocket, sendAllTo...
#include "hcl_utils.h"                   // for LOG_HCL_ERR, LOG_HCL_DEBUG
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG, LOG_TRACE

static constexpr auto MAX_ASYNC_RECV_TIMEOUT = std::chrono::seconds(5);
static const Ack      g_ack_send_buff        = ACK_VALID;
static unsigned int   g_jobsCounter          = 0;

SocketThread::SocketThread(int globalRank, int socketThreadId)
: m_globalRank(globalRank), m_socketThreadId(socketThreadId)
{
}

void SocketThread::runThread()
{
    SocketJob* job = nullptr;
    while (!(m_stop && isEmpty()))
    {
        if (m_jobsQueue.peekHead(job) && job != nullptr)
        {
            bool status = executeJob(job);
            // remove job from the queue only after successful execution
            // otherwise it will remain at the head of the queue, and will be
            // executed on the next iteration
            if (likely(status == true))
            {
                m_jobsQueue.popHead();
                job->m_handle->setHandleAsDone();
                delete job;
                continue;
            }
            else
            {
                LOG_HCL_ERR(HCL, "Rank({}) Thread({}) failed to execute job", m_globalRank, m_socketThreadId);
                return;
            }
        }
    }
}

/**
 * @brief Look for a job in m_Asyncjobs that matches a hdr of a PendingJob.
 * In case such is found, place the PendingJob's data in the correct buffer, and release the job's handle.
 *
 */
void SocketThread::runPendingJobs()
{
    LOG_HCL_TRACE(HCL, "m_PendingJobs.size={}", m_PendingJobs.size());
    for (std::vector<PendingJob>::iterator itr = m_PendingJobs.begin(); itr != m_PendingJobs.end();)
    {
        if (!m_Asyncjobs[itr->hdr.source_peer].empty())
        {
            LOG_HCL_DEBUG(HCL,
                          "Rank({}) AsyncThread({}) found a match for a pending job with peer={}, seq={}",
                          m_globalRank,
                          m_socketThreadId,
                          itr->hdr.source_peer,
                          itr->hdr.sequence);
            SocketJob job = m_Asyncjobs[itr->hdr.source_peer].front();

            if (job.m_size != itr->hdr.payload_size || job.m_sequence != itr->hdr.sequence)
            {
                LOG_HCL_ERR(HCL,
                            "Rank({}) AsyncThread({}) received unexpected job from peer={} got seq={}, size={}, expected "
                            "seq={}, size={}",
                            m_globalRank,
                            m_socketThreadId,
                            itr->hdr.source_peer,
                            itr->hdr.sequence,
                            itr->hdr.payload_size,
                            job.m_sequence,
                            job.m_size);
                job.m_handle->result = false;
                job.m_handle->setHandleAsDone();
                return;
            }

            memcpy(job.m_address, itr->jobAddr, job.m_size);
            job.m_handle->setHandleAsDone();
            m_Asyncjobs[itr->hdr.source_peer].pop();
            free(itr->jobAddr);
            itr = m_PendingJobs.erase(itr);
        }
        else
        {
            itr++;
        }
    }
}

void SocketThread::runAsyncThread()
{
    SocketJob    pJob;
    msg_header_t hdr;
    // indicates whether the current received hdr and data from socket were pushed to PendingJobs vector
    bool pushedToPendingJobs = false;
    while (!m_stop)
    {
        pushedToPendingJobs = false;
        // try to release a pending job before receiving new data from socket
        if (!m_PendingJobs.empty())
        {
            runPendingJobs();
        }

        /*
        Check if any job is in queue. If yes => wait for to recv hdr & data payload on socket - it could be:
        1. A job we are waiting for in m_Asyncjobs - in this case we will process it
        2. A job we are not currently expecting - in this case we will push it to pending.
        In case the jobs queue is empty - we do not block on receive but simply wait for main thread to put jobs.
        If we receive something in the socket at this stage it is kept in the OS socket buffers and will be processed
        once any job is put into queue.
        */
        bool anyJob = false;
        for (auto const& entry : m_Asyncjobs)
        {
            if (!entry.second.empty())
            {
                anyJob = true;
                break;
            }
        }
        if (!anyJob)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));  // delay 2msec
            continue;
        }

        LOG_HCL_TRACE(HCL, "Rank({}) about to receive header from socket", m_globalRank);
        int result = recvFromSocket(m_socket, reinterpret_cast<void*>(&hdr), sizeof(hdr));
        if (result != sizeof(hdr))
        {
            if (result == 0 && !m_stop)
            {
                LOG_HCL_ERR(HCL, "Rank({}) AsyncThread({}) socket has been closed!", m_globalRank, m_socketThreadId);
            }
            else if (!m_stop)
            {
                LOG_HCL_ERR(HCL, "Rank({}) AsyncThread({}) failed to receive hdr", m_globalRank, m_socketThreadId);
            }

            return;
        }

        if (hdr.id != DATA_BETWEEN_RANKS)
        {
            LOG_HCL_ERR(HCL,
                        "Rank({}) AsyncThread({}) received message header with unexpected id={}",
                        m_globalRank,
                        m_socketThreadId,
                        hdr.id);
            return;
        }

        LOG_HCL_TRACE(HCL,
                      "Rank({}) AsyncThread({}) received header from peer={}, sequence={}, size={}",
                      m_globalRank,
                      m_socketThreadId,
                      hdr.source_peer,
                      hdr.sequence,
                      hdr.payload_size);
        if (hdr.dest_peer != m_globalRank)
        {
            LOG_HCL_ERR(HCL,
                        "Rank({}) AsyncThread({}) received message from coordinator, meant for other rank={}",
                        m_globalRank,
                        m_socketThreadId,
                        hdr.dest_peer);
            return;
        }

        const auto start_time = std::chrono::high_resolution_clock::now();
        unsigned   loopsCounter = 0;
        while (!pushedToPendingJobs)
        {
            loopsCounter++;
            if (!m_Asyncjobs[hdr.source_peer].empty())
            {
                pJob = m_Asyncjobs[hdr.source_peer].front();
                break;
            }

            if (std::chrono::high_resolution_clock::now() - start_time > MAX_ASYNC_RECV_TIMEOUT)
            {
                LOG_HCL_ERR(HCL,
                            "Rank({}) AsyncThread({}) failed to find matching message from peer={} seq={}",
                            m_globalRank,
                            m_socketThreadId,
                            hdr.source_peer,
                            hdr.sequence);
                return;
            }

            // If some peer has an async job in queue, push the current job to the pendingJobs queue to avoid deadlock
            for (auto const& entry : m_Asyncjobs)
            {
                if (!entry.second.empty())
                {
                    PendingJob pendingJob;
                    memcpy(&(pendingJob.hdr), &hdr, sizeof(hdr));
                    pendingJob.jobAddr = malloc(hdr.payload_size);
                    LOG_HCL_TRACE(HCL,
                                  "Rank({}) about to receive payload from socket and push a job to the pending queue "
                                  "from source_peer={}, loopsCounter={}",
                                  m_globalRank,
                                  pendingJob.hdr.source_peer,
                                  loopsCounter);
                    if (!recvAllFromSocket(m_socket, pendingJob.jobAddr, hdr.payload_size))
                    {
                        LOG_HCL_ERR(HCL,
                                    "Rank({}) AsyncThread({}) failed to receive data",
                                    m_globalRank,
                                    m_socketThreadId);
                        free(pendingJob.jobAddr);
                        return;
                    }
                    m_PendingJobs.push_back(pendingJob);
                    LOG_HCL_TRACE(HCL,
                                  "Rank({}) AsyncThread({}) pushed job from peer={} seq={} to pending jobs queue",
                                  m_globalRank,
                                  m_socketThreadId,
                                  hdr.source_peer,
                                  hdr.sequence);
                    pushedToPendingJobs = true;
                    break;
                }
            }
            std::this_thread::yield();
        }

        if (pushedToPendingJobs)
        {
            continue;
        }

        LOG_HCL_TRACE(HCL,
                      "Rank({}) about to receive payload from socket, loopsCounter={}",
                      m_globalRank,
                      loopsCounter);
        if (!recvAllFromSocket(m_socket, pJob.m_address, pJob.m_size))
        {
            LOG_HCL_ERR(HCL, "Rank({}) AsyncThread({}) failed to receive data", m_globalRank, m_socketThreadId);
            pJob.m_handle->result = false;
            pJob.m_handle->setHandleAsDone();
            return;
        }

        if (pJob.m_size != hdr.payload_size || pJob.m_sequence != hdr.sequence)
        {
            LOG_HCL_ERR(HCL,
                        "Rank({}) AsyncThread({}) received unexpected job from peer={} got seq={}, size={}, expected "
                        "seq={}, size={}",
                        m_globalRank,
                        m_socketThreadId,
                        hdr.source_peer,
                        hdr.sequence,
                        hdr.payload_size,
                        pJob.m_sequence,
                        pJob.m_size);
            pJob.m_handle->result = false;
            pJob.m_handle->setHandleAsDone();
            return;
        }

        LOG_HCL_TRACE(HCL,
                      "Rank({}) AsyncThread({}) received data from peer {} seq {}, loopsCounter={}",
                      m_globalRank,
                      m_socketThreadId,
                      hdr.source_peer,
                      hdr.sequence,
                      loopsCounter);
        pJob.m_handle->setHandleAsDone();
        m_Asyncjobs[hdr.source_peer].pop();
    }
}

bool SocketThread::isEmpty() const
{
    return m_jobsQueue.isEmpty();
}

bool SocketThread::executeRecv(SocketJob* job)
{
    // read message
    if (!recvAllFromSocket(job->m_socketFd, job->m_address, job->m_size))
    {
        LOG_HCL_ERR(HCL,
                    "Rank({}) Thread({}) Socket({}) Receiving data failed.",
                    m_globalRank,
                    m_socketThreadId,
                    job->m_socketFd);
        return false;
    }
    job->m_size = 0;

    // send ack
    if (!sendAllToSocket(job->m_socketFd, &g_ack_send_buff, sizeof(Ack)))
    {
        LOG_HCL_ERR(HCL,
                    "Rank({}) Thread({}) Socket({}) Sending ACK failed.",
                    m_globalRank,
                    m_socketThreadId,
                    job->m_socketFd);
        return false;
    }

    return true;
}

bool SocketThread::executeSend(SocketJob* job)
{
    // send message
    if (!sendAllToSocket(job->m_socketFd, job->m_address, job->m_size))
    {
        LOG_HCL_ERR(HCL,
                    "Rank({}) Thread({}) Socket({}) Sending data failed.",
                    m_globalRank,
                    m_socketThreadId,
                    job->m_socketFd);
        return false;
    }
    job->m_size = 0;

    // receive ack
    Ack ack_recv_buff = ACK_WAIT;
    if (!recvAllFromSocket(job->m_socketFd, &ack_recv_buff, sizeof(Ack)))
    {
        LOG_HCL_ERR(HCL,
                    "Rank({}) Thread({}) Socket({}) Receiving ACK failed.",
                    m_globalRank,
                    m_socketThreadId,
                    job->m_socketFd);
        return false;
    }

    // validate
    return ack_recv_buff == ACK_VALID;
}

bool SocketThread::executeJob(SocketJob* job)
{
    bool result = false;
    switch (job->m_jobType)
    {
        case TCP_SEND:
            result = executeSend(job);
            break;
        case TCP_RECV:
            result = executeRecv(job);
            break;
        default:
            return false;
    }
    return result;
}

void SocketThread::stopAsyncThread()
{
    if (!m_stop)
    {
        m_stop = true;
        shutdown(m_socket, SHUT_RD);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }
}

void SocketThread::stopThread()
{
    if (!m_stop)
    {
        m_stop = true;
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }
}

void SocketThread::initialize()
{
    m_thread = std::thread(&SocketThread::runThread, this);
}

void SocketThread::initializeAsync()
{
    m_thread = std::thread(&SocketThread::runAsyncThread, this);
}

void SocketThread::startThread()
{
    m_stop = false;
    initialize();
}

void SocketThread::startAsyncThread(int socket)
{
    m_stop    = false;
    m_socket  = socket;
    m_isAsync = true;
    initializeAsync();
}

bool SocketThread::pushAsyncJob(ConnectionOperations jobType,
                                size_t               size,
                                void*                address,
                                int                  peer,
                                uint32_t             sequence,
                                hcclHandle*          handle)
{
    // verify that thread is active
    if (m_stop)
    {
        LOG_HCL_ERR(HCL, "Tried to push job to inactive thread");
        return false;
    }
    SocketJob job;
    job.m_jobType  = jobType;
    job.m_size     = size;
    job.m_address  = address;
    job.m_jobId    = ++g_jobsCounter;
    job.m_handle   = &handle->internalHandle;
    job.m_sequence = sequence;
    LOG_HCL_TRACE(HCL,
                  "Rank({}) AsyncThread({}) add new job from peer={}, sequence={}, size={}",
                  m_globalRank,
                  m_socketThreadId,
                  peer,
                  sequence,
                  size);
    m_Asyncjobs[peer].push(job);

    return true;
}

bool SocketThread::pushJob(ConnectionOperations jobType, size_t size, void* address, int socket, hcclHandle* handle)
{
    // verify that thread is active
    if (m_stop)
    {
        LOG_HCL_ERR(HCL, "Tried to push job to inactive thread");
        return false;
    }
    SocketJob* job  = new SocketJob;
    job->m_socketFd = socket;
    job->m_jobType  = jobType;
    job->m_size     = size;
    job->m_address  = address;
    job->m_jobId    = ++g_jobsCounter;
    job->m_handle   = &handle->internalHandle;
    if (!m_jobsQueue.pushTail(job))
    {
        LOG_HCL_ERR(HCL, "Failed to push Job to queue");
        delete job;
        return false;
    }
    return true;
}

bool SocketThread::destroy()
{
    for (auto& pendingJob : m_PendingJobs)
    {
        free(pendingJob.jobAddr);
    }
    return true;
}

SocketThread::~SocketThread()
{
    SocketThread::destroy();
}

SocketThreadsManager::SocketThreadsManager() : m_nextThread(0), m_nThreads(0), m_nAsyncThreads(0) {}

void SocketThreadsManager::initThreads(int nThreads, int globalId)
{
    m_nThreads = nThreads;
    for (int i = 0; i < nThreads; ++i)
    {
        SocketThread* thread = new SocketThread(globalId, i);
        m_threadPool.push_back(thread);
    }

    startThreads();
}

void SocketThreadsManager::createAsyncThread(int socket, int globalId)
{
    SocketThread* thread = new SocketThread(globalId, m_nThreads + m_nAsyncThreads);
    m_nAsyncThreads++;
    m_asyncThreadPool.push_back(thread);

    thread->startAsyncThread(socket);
}

bool SocketThreadsManager::destroy()
{
    stopThreads();
    for (auto& syncThread : m_threadPool)
    {
        delete syncThread;
    }

    m_nThreads = 0;
    m_threadPool.clear();

    for (auto& asyncThread : m_asyncThreadPool)
    {
        delete asyncThread;
    }

    m_nAsyncThreads = 0;
    m_asyncThreadPool.clear();

    return true;
}

SocketThreadsManager::~SocketThreadsManager()
{
    destroy();
}

bool SocketThreadsManager::pushAsyncJob(ConnectionOperations jobType,
                                        size_t               size,
                                        void*                address,
                                        unsigned             peer,
                                        uint32_t             sequence,
                                        hcclHandle*          handle)
{
    return m_asyncThreadPool[0]->pushAsyncJob(jobType, size, address, peer, sequence, handle);
}

bool SocketThreadsManager::pushJob(ConnectionOperations jobType,
                                   size_t               size,
                                   void*                address,
                                   int                  socket,
                                   hcclHandle*          handle)
{
    m_nextThread = (m_nextThread + 1) % m_nThreads;
    return m_threadPool[m_nextThread]->pushJob(jobType, size, address, socket, handle);
}

void SocketThreadsManager::startThreads()
{
    for (auto& t : m_threadPool)
    {
        t->startThread();
    }
}

void SocketThreadsManager::stopThreads()
{
    for (auto& t : m_threadPool)
    {
        t->stopThread();
    }

    for (auto& t : m_asyncThreadPool)
    {
        t->stopAsyncThread();
    }
}
