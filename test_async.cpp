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
        x = x + 1;
        yy.emplace_back(x + 1);
        usleep(1000);
    }
    
    cnt += xx.size();
    assert(xx.size() == yy.size());
    // cout << "cnt: " << cnt << endl;
    return 0;
}


int main(void) {
    const int num = 128;
    vector<Tidx> tid(num, 0);
    vector<vector<int>> input, output, input_bak;
    for (int i = 0; i < num; ++i) {
        vector<int> tmp;
        output.push_back(tmp);
        for (int j = 0; j < i; ++j) tmp.push_back(i);
        input.push_back(tmp);
    }
    input_bak = input;

    std::function<int(std::vector<int>&, std::vector<int>&)> func = 
            std::bind(&Process, std::placeholders::_1, std::placeholders::_2);
    Cardan<int, int> demo(32, func);

    auto t1 = std::chrono::system_clock::now();
    for (int i = 0; i < num; ++i) {
        tid[i] = demo.RequestAsync(input[i], output[i]);
    }
    auto t2 = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - t1).count();
    cout << "Test Spend: " << t2 << endl;

    for (int i = 0; i < num; ++i) {
        demo.Wait(tid[i]);
    }

    for (int i = 0; i < num; ++i) {
        for (int j = 0; j < input[i].size(); ++j) {
            if (input_bak[i][j] + 1 != input[i][j])  cout << "Bad input: " << endl;
            if (input_bak[i][j] + 2 != output[i][j])  cout << "Bad output: " << endl;
        }
    }

    return 0;
} 