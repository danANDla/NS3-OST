#include "timer_fifo.h"

#include <format>
#include "ns3/simulator.h"
#include "ns3/ost_node.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("TimerFifo");

    TimerFifo::TimerFifo(uint16_t w_sz) :
        head(0),
        tail(0),
        window_sz(w_sz),
        timers_sum(0)
    {}    

    TimerFifo::~TimerFifo() {}    

    void TimerFifo::set_callback(TimerHandleCallback cb) {upper_handler = cb;}

    bool TimerFifo::is_queue_have_space() {
        return ((head + 1) % MAX_UNACK_PACKETS) != tail;
    }

    nsecs_t TimerFifo::pop_timer(uint8_t seq_n) {
        NS_LOG_FUNCTION("pop timer " << std::to_string(seq_n));

        if (tail == head) {
            return -1;
        }

        if(seq_n == data[tail].first) {
            NS_LOG_DEBUG("timer is on tail");
            if(((tail > head) && tail == MAX_UNACK_PACKETS && head == 0) || head - tail == 1) {
                NS_LOG_DEBUG("timer is the only");
                tail = (tail + 1) % MAX_UNACK_PACKETS;
                return 0;
            }
            NS_LOG_DEBUG("there is another");
            nsecs_t r = get_hard_timer_left_time();
            tail = (tail + 1) % MAX_UNACK_PACKETS;
            timers_sum -= data[tail].second;
            data[tail].second += r;
            return r;
        } else {
            NS_LOG_DEBUG("timer is smwhere else");

            uint8_t t_id = tail;
            while(data[t_id].first != seq_n) t_id = (t_id + 1) % MAX_UNACK_PACKETS;

            nsecs_t r = data[t_id].second;
            if(tail > head) {
                for(uint16_t i = t_id; i < MAX_UNACK_PACKETS - 1; ++i) {
                    data[i] = data[i + 1];
                }
                if(head > 0)
                    data[MAX_UNACK_PACKETS - 1] = data[0];
                for(uint16_t i = 0; i < head - 1; ++i) {
                    data[i] = data[i + 1];
                }
                data[t_id].second += r;
            } else {
                for(uint16_t i = t_id; i < head - 1; ++i) {
                    data[i] = data[i + 1];
                }
            }
            if((seq_n + 1 ) % MAX_UNACK_PACKETS) timers_sum -= data[t_id].second;
            
            if(head == 0) head = MAX_UNACK_PACKETS - 1;
            else head -= 1;
            return 0;
        }
    }

    nsecs_t TimerFifo::push_timer(uint8_t seq_n, nsecs_t duration) {
        if (((head + 1) % MAX_UNACK_PACKETS) == tail ||
            duration > MAX_TIMER_DURATION) {
            return 0;
        }
        if (head == tail) {
            data[head] = std::make_pair(seq_n, duration);
        } else {
            nsecs_t left = get_hard_timer_left_time();
            data[head] = std::make_pair(seq_n, duration - left - timers_sum);
            timers_sum += data[head].second;
        }
        nsecs_t r = data[head].second;
        head = (head + 1) % MAX_UNACK_PACKETS;
        return r;
    }

    void TimerFifo::Print(std::ostream& os) const {
        for(uint16_t i = 0; i < MAX_UNACK_PACKETS; ++i) {
            if(i == tail) os << "[";

            if((tail > head && (i >= tail || i < head)) || (head > tail && i >= tail && i < head)) {
                os << std::to_string(data[i].second) << "{" << std::to_string(data[i].first)<< "}"; 
                if(i == head) os << "] ";
            }
            else {
                if(i == head) os << "] ";
                os <<  " NaN ";
            }

        }
        os << "\n tail=" << std::to_string(tail) << ", head=" << std::to_string(head) <<  ", sum=" << std::to_string(timers_sum) << "\n";
    }

    std::ostream&
    operator<<(std::ostream& os, const TimerFifo& q)
    {
        q.Print(os);
        return os;
    }

    nsecs_t TimerFifo::get_hard_timer_left_time() {
        /**
        * @note Can not be implemented in 1892VM15F, because there isn't way to get state
        * of SCOUNT in interval timers.
        *
        * Instead can use timer for discrete time interrupts where will check smth
        * it could be a okay solution, cause protocol works as spin-waiting-event
        * alghorithm so it will check for an event every ?interrupt?
        * But sounds a bit uneffective and harmful for CPU performance: interrupts
        * could be too frequent
        *
        * Although it is possible in the simulator.
        */
        return Simulator::GetDelayLeft(last_timer.e_id).GetMicroSeconds();
    }

    int8_t TimerFifo::add_new_timer(uint8_t seq_n, const nsecs_t duration) {
        if(!is_queue_have_space()) return -1;
        nsecs_t r = push_timer(seq_n, duration);
        if(r == 0) {
            return -1;
        }
        last_timer.e_id = Simulator::Schedule(
                MicroSeconds(duration),
                &TimerFifo::timer_interrupt_handler,
                this
        );
        NS_LOG_LOGIC("NODE[] started timer(" << std::to_string(last_timer.e_id.GetUid()) <<  ") for " << std::to_string(last_timer.val) << " microseconds \n" << *this);
        last_timer.val = duration;
        return 0;
    }

    int8_t TimerFifo::cancel_timer(uint8_t seq_n) {
        if(seq_n == data[tail].first) {
            Simulator::Cancel(last_timer.e_id);
        }
        nsecs_t r = pop_timer(seq_n);
        NS_LOG_LOGIC("NODE[] canceled timer(" << std::to_string(last_timer.e_id.GetUid()) <<  ") for " << std::to_string(last_timer.val) << " microseconds: \n" << *this);
        if(r != -1) {
            if(r != 0) {
                last_timer.e_id = Simulator::Schedule(
                        MicroSeconds(r),
                        &TimerFifo::timer_interrupt_handler,
                        this
                );
                NS_LOG_LOGIC("NODE[] started timer(" << std::to_string(last_timer.e_id.GetUid()) <<  ") for " << std::to_string(last_timer.val) << " microseconds \n" << *this);
                last_timer.val = r;
            }
        }
        return 0;
    }

    void TimerFifo::timer_interrupt_handler() {
        uint8_t seq_n = data[tail].first;
        uint8_t r = pop_timer(seq_n);
        if(r != -1 && r != 0) {
            last_timer.e_id = Simulator::Schedule(
                    MicroSeconds(r),
                    &TimerFifo::timer_interrupt_handler,
                    this
            );
            NS_LOG_LOGIC("NODE[] started timer(" << std::to_string(last_timer.e_id.GetUid()) <<  ") for " << std::to_string(last_timer.val) << " microseconds \n" << *this);
            last_timer.val = r;
        }
        upper_handler(seq_n);
    }
}