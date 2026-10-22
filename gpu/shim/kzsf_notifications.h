#pragma once
#include <deque>
#include <string>
struct KzsfNotifications {
    struct Notice {std::string text;float remaining=5.0f;};
    std::deque<std::string> waiting;
    std::deque<Notice> visible;
    void Push(std::string text) {if(waiting.size()<128)waiting.push_back(std::move(text));}
    void Tick(float seconds,bool showing) {
        if(!showing)return;
        for(auto& notice:visible)notice.remaining-=seconds;
        while(!visible.empty() && visible.front().remaining<=0)visible.pop_front();
        while(visible.size()<5 && !waiting.empty()) {
            visible.push_back({std::move(waiting.front()),5.0f});waiting.pop_front();
        }
    }
    bool Empty() const {return waiting.empty() && visible.empty();}
};
