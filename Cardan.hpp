#pragma once

#include <unistd.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <atomic>

using Tidx = int32_t; 
#define CARDAN_QUEUE_FULL      12306

// 定义queue队列格式
template<class InType>
struct ItemType {
    Tidx idx;
    InType inData;
    ItemType(Tidx x, InType y) : idx(x), inData(y) {}
};


// 定义queue队列格式
template<class InType, class OutType>
struct DataType {
    int ret;
    int size;
    std::vector<InType> inData;
    std::vector<OutType> outData;
    std::vector<InType> *inPtr;
    std::vector<OutType> *outPtr;
};


template<class ProcessIn, class ProcessOut>
class Cardan {
public:
    int fullSize_;
    int batchSize_;
    bool runEnable_; 
    std::condition_variable notice_;
    std::unique_ptr<std::thread> worker_;

    std::mutex workQueueMux_;
    std::queue<ItemType<ProcessIn>> workQueue_;                 // 存放待处理的结果

    std::atomic<int> sum_;
    std::atomic<int> pos_;
    std::vector<std::mutex> mutexList_;                         // 互斥锁
    std::vector<std::condition_variable> noticeList_;           // 存放每个线程的信号
    std::vector<DataType<ProcessIn, ProcessOut>> productList_;  // 存放处理后的结果

    std::function<int(std::vector<ProcessIn>&, std::vector<ProcessOut>&)> processFunc_;

    Cardan (
        int batchSize,
        std::function<int(std::vector<ProcessIn>&, std::vector<ProcessOut>&)> processFunc
    ): batchSize_(batchSize), sum_(0), pos_(0), runEnable_(true), fullSize_(10240), processFunc_(processFunc) {
        worker_.reset(new std::thread(&Cardan::Process, this));
        mutexList_ = std::vector<std::mutex>(fullSize_);
        noticeList_ = std::vector<std::condition_variable>(fullSize_);
        productList_.resize(fullSize_);
    }

    Cardan(const Cardan& rhs) = delete;
    Cardan& operator=(const Cardan& rhs) = delete;

    ~Cardan () {
        runEnable_ = false;
        notice_.notify_all();
        worker_->join();
    }

    // 如果不希望修改dataIn，这里的&去掉即可
    Tidx RequestAsync(std::vector<ProcessIn> &dataIn, std::vector<ProcessOut> &dataOut) {
        int inSize = dataIn.size();
        dataOut.clear();
        dataOut.reserve(inSize);

        //& 将数据入队列
        std::unique_lock<std::mutex> queLock(workQueueMux_);
        if (workQueue_.size() >= fullSize_ || sum_ >= fullSize_) { return fullSize_; }
        sum_++;

        //? 可能是分配尚未释放的挂载
        const Tidx tid = pos_;
        pos_ = (pos_ + 1) % fullSize_;
        // std::cout << __FILE__ << ":" << __LINE__ << " tid: " << tid << std::endl;

        for (auto data : dataIn) {
            ItemType<ProcessIn> item(tid, data);
            workQueue_.emplace(std::move(item));
        }

        dataIn.clear();
        dataIn.reserve(inSize);
        //& 注册结果存放，获得信号
        productList_[tid].ret = 0;
        productList_[tid].size = inSize;
        productList_[tid].inPtr = &dataIn;
        productList_[tid].outPtr = &dataOut; 

        queLock.unlock();
        notice_.notify_one();
        return tid;
    }

    int Wait(Tidx tid) {
        if (tid >= fullSize_) return CARDAN_QUEUE_FULL;

        std::unique_lock<std::mutex> lock(mutexList_[tid]);
        noticeList_[tid].wait(lock, [=] { 
            return productList_[tid].size == productList_[tid].inData.size() || productList_[tid].ret; 
        });
        swap(*(productList_[tid].inPtr), productList_[tid].inData);
        swap(*(productList_[tid].outPtr), productList_[tid].outData);
        sum_--;

        return productList_[tid].ret;
    }

    int Request(std::vector<ProcessIn> &dataIn, std::vector<ProcessOut> &dataOut) {
        return Wait(RequestAsync(dataIn, dataOut));
    }

    // 消费者，资源非空时调用
    void Process(void) {
        while (runEnable_) {
            std::vector<int> tidList;
            std::vector<ProcessIn> inList;
            std::vector<ProcessOut> outList;

            tidList.reserve(batchSize_);
            inList.reserve(batchSize_);
            outList.reserve(batchSize_);

            {   //& 获取最大一个batchsize的结果，并记录来自哪一个线程
                std::unique_lock<std::mutex> queLock(workQueueMux_);
                notice_.wait(queLock, [=] { return workQueue_.size() > 0 || !runEnable_; });
                if (!runEnable_) break;
        
                for (int i = 0; (!workQueue_.empty()) && i < batchSize_; ++i) {
                    auto &item = workQueue_.front();
                    tidList.emplace_back(item.idx);
                    inList.emplace_back(std::move(item.inData));
                    workQueue_.pop();
                }
            }

            //& 调用外部函数处理这部分数据，并记录返回值
            int ret = processFunc_(inList, outList);

            //& 输出结果写入结果列表中，写入返回值，如果该注册ID结束，发出信号通知生产者回收
            for (int i = 0; i < tidList.size(); ++i) {
                Tidx tid = tidList[i];
                productList_[tid].ret = ret;
                if (ret != 0) { 
                    noticeList_[tid].notify_one();
                } else {
                    productList_[tid].inData.emplace_back(std::move(inList[i]));
                    productList_[tid].outData.emplace_back(std::move(outList[i]));
                    if (productList_[tid].size == productList_[tid].inData.size()) {
                        noticeList_[tid].notify_one();
                    }
                }
            }
        }
    }
};