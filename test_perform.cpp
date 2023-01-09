#include <iostream>
#include <vector>
#include <thread>
#include <unistd.h>
#include "Cardan.hpp"
using namespace std;


int Process(vector<int> &xx, vector<int> &yy) {
    static int cnt = 0;
    yy.reserve(xx.size());
    for (auto & x : xx) {
        // if (x == 1024) return -1;
        x = x + 1;
        yy.emplace_back(x + 1);
        usleep(1000);
    }
    
    cnt += xx.size();
    // cout << "cnt: " << cnt << endl;
    return 0;
}

std::function<int(std::vector<int>&, std::vector<int>&)> func = 
        std::bind(&Process, std::placeholders::_1, std::placeholders::_2);
Cardan<int, int> demo(12, func);


void Request(int i) {
    vector<int> in = {i}, out;
    demo.Request(in, out);
}

int main(void) {
    const int num = 2048;
    vector<thread> insts(num);
    vector<int> yy(num, 0), ret(num, 0);

    cout << "Test 1: BruteForce Process" << endl;
    auto t1 = std::chrono::system_clock::now();

    for (int i = 0; i < num; ++i) {
        Request(i);
    }
    auto t2 = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - t1).count();
    cout << "Test 1 Spend: " << t2 << endl;


    cout << "Test 2: QueueTrans Process" << endl;
    auto t3 = std::chrono::system_clock::now();

    for (int i = 0; i < num; ++i) {
        insts[i] = std::thread(Request, i);
    }
    for (int i = 0; i < num; ++i) {
        insts[i].join();
    }
    auto t4 = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - t3).count();
    cout << "Test 2 Spend: " << t4 << endl;

    return 0;
} 