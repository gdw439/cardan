#pragma once

#include <unistd.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <atomic>

#define CARDAN_QUEUE_FULL      12306

// 定义queue队列格式
template<class InType>
struct ItemType {
    int idx;
    InType inData;
    ItemType(int x, InType y) : idx(x), inData(y) {}
};


// 定义queue队列格式
template<class InType, class OutType>
struct DataType {
    int ret;
    int size;
    std::vector<InType> inData;
    std::vector<OutType> outData;
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

    std::atomic<int> pos_;
    std::vector<std::condition_variable> noticeList_;           // 存放每个线程的信号
    std::vector<DataType<ProcessIn, ProcessOut>> productList_;  // 存放处理后的结果

    std::function<int(std::vector<ProcessIn>&, std::vector<ProcessOut>&)> processFunc_;

    Cardan (
        int batchSize,
        std::function<int(std::vector<ProcessIn>&, std::vector<ProcessOut>&)> processFunc
    ): batchSize_(batchSize), pos_(0), runEnable_(true), fullSize_(10240), processFunc_(processFunc) {
        worker_.reset(new std::thread(&Cardan::Consumer, this));
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

    // 生产者，外部调用
    int Producer(std::vector<ProcessIn> &dataIn, std::vector<ProcessOut> &dataOut) {
        if (dataIn.size() == 0) return 0;
        //& 将数据入队列
        std::unique_lock<std::mutex> queLock(workQueueMux_);
        if (workQueue_.size() > fullSize_) { return CARDAN_QUEUE_FULL; }

        const int tid = pos_;
        pos_ = (pos_ + 1) % fullSize_;
        std::cout << __FILE__ << ":" << __LINE__ << " tid: " << tid << std::endl;

        for (auto &data : dataIn) {
            ItemType<ProcessIn> item(tid, data);
            workQueue_.emplace(std::move(item));
        }
        queLock.unlock();

        //& 注册结果存放，获得信号
        productList_[tid].ret = 0;
        productList_[tid].size = dataIn.size();
        productList_[tid].inData.reserve(dataIn.size());
        productList_[tid].outData.reserve(dataIn.size());
        notice_.notify_one();

        //& 等待处理完成，注销存放数据
        std::mutex mut;
        std::unique_lock<std::mutex> lock(mut);
        noticeList_[tid].wait(lock);

        swap(productList_[tid].inData, dataIn);
        swap(productList_[tid].outData, dataOut);

        return productList_[tid].ret;
    }


    // 消费者，资源非空时调用
    void Consumer(void) {
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
                int tid = tidList[i];
                productList_[tid].ret = ret;
                productList_[tid].inData.emplace_back(std::move(inList[i]));
                productList_[tid].outData.emplace_back(std::move(outList[i]));
                if (productList_[tid].size == productList_[tid].inData.size() || productList_[tid].ret) {
                    noticeList_[tid].notify_one();
                }
            }
        }
    }
};