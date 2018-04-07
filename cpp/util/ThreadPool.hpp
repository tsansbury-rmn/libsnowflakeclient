/*
 * Copyright (c) 2018 Snowflake Computing, Inc. All rights reserved.
 */

#ifndef SNOWFLAKECLIENT_THREADPOOL_HPP
#define SNOWFLAKECLIENT_THREADPOOL_HPP

#include <pthread.h>
#include <vector>
#include <deque>
#include <functional>
#include <atomic>
#include "snowflake/platform.h"

namespace Snowflake
{
namespace Client
{
namespace Util
{

/**
 * A naive implementation of thread pool  
 */
class ThreadPool {
private:
  unsigned int threadCount;

  std::vector<SF_THREAD_HANDLE > threads;
  std::deque<std::function<void(void)>> queue;

  unsigned int busyThreads;
  bool finished;
  SF_CONDITION_HANDLE job_available_var;
  SF_CONDITION_HANDLE wait_var;
  SF_MUTEX_HANDLE queue_mutex;

  static void *TaskWrapper(void *arg)
  {
    reinterpret_cast<ThreadPool *>(arg)->execute_thread();
  }

  /**
   *  Get the next job; pop the first item in the queue,
   *  otherwise wait for a signal from the main thread.
   */
  void execute_thread() {
    std::function<void(void)> res;
    while(true)
    {
      _mutex_lock(&queue_mutex);

      busyThreads --;
      _cond_signal(&wait_var);

      // Wait for a job if we don't have any.
      while (queue.empty() && !finished)
      {
        _cond_wait(&job_available_var, &queue_mutex);
      }

      // Get job from the queue
      if (finished)
      {
        _mutex_unlock(&queue_mutex);
        break;
      }
      else
      {
        busyThreads ++;
        res = queue.front();
        queue.pop_front();
        _mutex_unlock(&queue_mutex);
        res();
      }
    }
  }

public:
  ThreadPool(unsigned int threadNum)
    : finished( false )
    , threadCount (threadNum)
    , busyThreads (threadNum)
  {
    _mutex_init(&queue_mutex);
    _cond_init(&job_available_var);
    _cond_init(&wait_var);

    for( unsigned i = 0; i < threadCount; ++i )
    {
      SF_THREAD_HANDLE tid;
      _thread_init(&tid, TaskWrapper, (void *)this);
      threads.push_back(tid);
    }
    //threads[ i ] = std::thread( [this]{ this->Task(); } );
  }

  /**
   *  JoinAll on deconstruction
   */
  ~ThreadPool() {
    JoinAll();
    _mutex_term(&queue_mutex);
    _cond_term(&job_available_var);
    _cond_term(&wait_var);
  }

  /**
   *  Add a new job to the pool. If there are no jobs in the queue,
   *  a thread is woken up to take the job. If all threads are busy,
   *  the job is added to the end of the queue.
   */
  void AddJob( std::function<void(void)> job ) {
    _mutex_lock(&queue_mutex);
    queue.emplace_back( job );
    _cond_signal(&job_available_var);
    _mutex_unlock(&queue_mutex);
  }

  /**
   *  Join with all threads. Block until all threads have completed.
   *  Params: WaitForAll: If true, will wait for the queue to empty
   *          before joining with threads. If false, will complete
   *          current jobs, then inform the threads to exit.
   *  The queue will be empty after this call, and the threads will
   *  be done. After invoking `ThreadPool::JoinAll`, the pool can no
   *  longer be used. If you need the pool to exist past completion
   *  of jobs, look to use `ThreadPool::WaitAll`.
   */
  void JoinAll() {
    _mutex_lock(&queue_mutex);
    finished = true;
    _cond_broadcast(&job_available_var);
    _mutex_unlock(&queue_mutex);

    for (auto &x : threads)
      _thread_join(x);
  }

  /**
   *  Wait for the pool to empty before continuing.
   *  This does not call `std::thread::join`, it only waits until
   *  all jobs have finshed executing.
   */
  void WaitAll() {
    _mutex_lock(&queue_mutex);
    while(busyThreads > 0 || !queue.empty())
    {
      _cond_wait(&wait_var, &queue_mutex);
    }
    _mutex_unlock(&queue_mutex);
  }
};


}
}
}

#endif //SNOWFLAKECLIENT_THREADPOOL_HPP
