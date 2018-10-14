#ifndef ZIMER_H
#define ZIMER_H

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>

const int32_t CONDVAR_TIMEOUT_MS = 1000;

typedef std::function<void(int32_t)> TimerEventCallback;

inline uint64_t get_current_time_ms() {
    auto tp = std::chrono::system_clock::now();
    auto td = std::chrono::time_point_cast<std::chrono::milliseconds>(tp);
    return td.time_since_epoch().count();
}

class Zimer {
public:
    Zimer() {
        _run_flag = true;
        _work_thread = std::thread(std::bind(&Zimer::work_func_, this));
    }

    virtual ~Zimer() {
        _run_flag = false;
        if (_work_thread.joinable()) {
            _work_thread.join();
        }
        while (!_tm_evt_que.empty()) {
            auto& top_evt = _tm_evt_que.top();
            if (_valid_tm_evt_keys.find(top_evt.name) != _valid_tm_evt_keys.end()) {
                top_evt.cb(1);
            }
            _tm_evt_que.pop();
        }
        _valid_tm_evt_keys.clear();
    }

    bool add(const std::string& name, const uint64_t abs_time_ms, TimerEventCallback cb) {
        auto curr_time_ms = get_current_time_ms();
        if (abs_time_ms <= curr_time_ms) {
            cb(0);
            return true;
        }
        TimerEventWrapper tm_evt;
        tm_evt.name = name;
        tm_evt.abs_time_ms = abs_time_ms;
        tm_evt.cb = cb;
        {
            std::unique_lock<std::mutex> lk(_mtx);
            if (_valid_tm_evt_keys.find(name) != _valid_tm_evt_keys.end()) {
                return false;
            }
            _valid_tm_evt_keys.insert(name);
            _tm_evt_que.push(tm_evt);
            _cv.notify_all();
        }
        return true;
    }

    void remove(const std::string& name) {
	    {
            std::unique_lock<std::mutex> lk(_mtx);
            _valid_tm_evt_keys.erase(name);
        }
    }

    template<typename Pred>
    void remove_if(Pred pred) {
        {
            std::unique_lock<std::mutex> lk(_mtx);
            auto iter = _valid_tm_evt_keys.begin();
            while (iter != _valid_tm_evt_keys.end()) {
                if (pred(*iter)) {
                    iter = _valid_tm_evt_keys.erase(iter);
                } else {
                    ++iter;
                }
            }
        }
    }

    size_t size() const {
        return _tm_evt_que.size();
    }

    void clear() {
    	{
            std::unique_lock<std::mutex> lk(_mtx);
            _valid_tm_evt_keys.clear();
            while (!_tm_evt_que.empty()) {
                _tm_evt_que.pop();
            }
        }
    }
private:
    void work_func_() {
        uint64_t to_sleep_time_ms = 0;
        while (_run_flag) {
            {
                std::unique_lock<std::mutex> lk(_mtx);
                to_sleep_time_ms = _tm_evt_que.empty() ? CONDVAR_TIMEOUT_MS : to_sleep_time_ms;
                _cv.wait_for(lk, std::chrono::milliseconds(to_sleep_time_ms),
                            [this](){return !this->_tm_evt_que.empty();});

                while (!_tm_evt_que.empty()) {
                    auto curr_time_ms = get_current_time_ms();
                    auto& top_evt = _tm_evt_que.top();
                    if (top_evt.abs_time_ms <= curr_time_ms) {
                        if (_valid_tm_evt_keys.find(top_evt.name) != _valid_tm_evt_keys.end()) {
                            top_evt.cb(0); 
                        }
                        _valid_tm_evt_keys.erase(top_evt.name);
                        _tm_evt_que.pop();
                    } else {
                        to_sleep_time_ms = top_evt.abs_time_ms - curr_time_ms;
                        break;
                    }
                }
            }
        }
    }
private:
    struct TimerEventWrapper {
        std::string name{""};
        uint64_t abs_time_ms{0LU};
        TimerEventCallback cb;
    };
    struct cmp {
        bool operator() (const TimerEventWrapper& foo, const TimerEventWrapper& bar) {
            return foo.abs_time_ms > bar.abs_time_ms;
        }
    };
    bool _run_flag;
    std::mutex _mtx;
    std::condition_variable _cv;
    std::thread _work_thread;
    std::priority_queue<TimerEventWrapper, std::vector<TimerEventWrapper>, cmp> _tm_evt_que;
    std::unordered_set<std::string> _valid_tm_evt_keys;
};

#endif
