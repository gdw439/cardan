#include <iostream>
#include <vector>
#include <thread>
#include <random>
#include <assert.h>
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
    assert(xx.size() == yy.size());
    cout << "cnt: " << cnt << endl;
    return 0;
}

std::function<int(std::vector<int>&, std::vector<int>&)> func = 
        std::bind(&Process, std::placeholders::_1, std::placeholders::_2);
Cardan<int, int> demo(16, func);


void Request(int cnt) {
    vector<int> in, out;
    for (int i = 0; i < cnt; ++i) {
        in.push_back(i);
    }

    demo.Producer(in, out);

    if (out.size() != cnt) {
        cout << "bad lens" << endl;
    }

    for (int i = 0; i < cnt; ++i) {
        if (in[i] != i+1) {
            cout << "bad input" << endl;
        }

        if (out[i] != i +2) {
            cout << "bad output" << endl;
        }
    }
}

int main(void) {
    const int num = 128;
    vector<thread> insts(num);
    vector<int> yy(num, 0), ret(num, 0);

    for (int i = 0; i < num; ++i) {
        insts[i] = std::thread(Request, i);
    }
    for (int i = 0; i < num; ++i) {
        insts[i].join();
    }

    return 0;
} 