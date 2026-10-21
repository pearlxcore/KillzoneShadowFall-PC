// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace Storage {
// A bounded single writer. Close first rejects new work, then drains every
// accepted job. A stop token must never discard accepted cache writes.
class CacheWriteQueue {
public:
    explicit CacheWriteQueue(size_t limit = 64u*1024*1024):limit(limit),worker([this]{Run();}){}
    ~CacheWriteQueue(){Close();}
    bool Submit(std::function<void()> job, size_t bytes=0) {
        std::unique_lock lock(mutex);
        ready.wait(lock,[&]{return !accepting || queued_bytes==0 ||
                                      (bytes<=limit && queued_bytes<=limit-bytes);});
        if(!accepting)return false;
        jobs.push_back({std::move(job),bytes});queued_bytes+=bytes;
        ready.notify_all();return true;
    }
    void Drain() {
        auto done=std::make_shared<std::promise<void>>();auto future=done->get_future();
        if(Submit([done]{done->set_value();}))future.wait();
    }
    void Close() {
        {std::scoped_lock lock(mutex);accepting=false;}
        ready.notify_all();if(worker.joinable())worker.join();
    }
    unsigned Errors() const {return errors.load();}
private:
    struct Job {std::function<void()> run;size_t bytes;};
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Job> jobs;
    size_t queued_bytes=0,limit;
    bool accepting=true;
    std::atomic<unsigned> errors{};
    std::thread worker;
    void Run() {
        for(;;) {
            Job job;
            {std::unique_lock lock(mutex);ready.wait(lock,[&]{return !jobs.empty()||!accepting;});
             if(jobs.empty())return;
             job=std::move(jobs.front());jobs.pop_front();}
            try{job.run();}catch(...){++errors;}
            {std::scoped_lock lock(mutex);queued_bytes-=job.bytes;}
            ready.notify_all();
        }
    }
};
} // namespace Storage
