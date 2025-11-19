#include <iostream>
#include "ctpl.h"
#include <thread>
#include <chrono>

using namespace std;

int main() {
    ctpl::thread_pool pool(2);
    
    // 测试基本功能
    pool.push([](int id) {
        cout << "Task 1 executed by thread " << id << endl;
        this_thread::sleep_for(chrono::milliseconds(100));
    });
    
    pool.push([](int id) {
        cout << "Task 2 executed by thread " << id << endl;
        this_thread::sleep_for(chrono::milliseconds(100));
    });
    
    this_thread::sleep_for(chrono::seconds(1));
    cout << "All tasks completed!" << endl;
    
    return 0;
}