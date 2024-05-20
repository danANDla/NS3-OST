#include "timer_fifo.h"

#include <format>
#include "ns3/simulator.h"
#include "ns3/ost_node.h"

namespace ns3 {
    NS_LOG_COMPONENT_DEFINE("TimerFifo");

    TimerFifo::TimerFifo() :
        head(0),
        tail(0),
        timers_sum(0)
    {}    

    TimerFifo::~TimerFifo() {}    

    void TimerFifo::set_callback(TimerHandleCallback cb) {upper_handler = cb;}

    void TimerFifo::init_hw_timer() { ; /*register and enable timer interrupt*/ } 

    bool TimerFifo::is_queue_have_space() {
        return ((head + 1) % (MAX_UNACK_PACKETS + 1)) != tail;
    }

    int8_t TimerFifo::get_number_of_timers() {

    }

    void TimerFifo::move_head() {
        head = (head + 1) % (MAX_UNACK_PACKETS + 1);
    }

    void TimerFifo::rmove_head() {
        if(head == 0) head = MAX_UNACK_PACKETS;
        else head -= 1;
    }

    void TimerFifo::move_tail() {
        tail = (tail + 1) % (MAX_UNACK_PACKETS + 1);
    }



    int8_t TimerFifo::pop_timer(uint8_t seq_n, micros_t& duration_to_set) {
        NS_LOG_FUNCTION("pop timer " << std::to_string(seq_n));

        if (tail == head) {
            return -1;
        }

        if(seq_n == data[tail].first) {
            NS_LOG_DEBUG("timer is on tail");
            if(((tail > head) && tail == MAX_UNACK_PACKETS && head == 0) || head - tail == 1) {
                NS_LOG_DEBUG("timer is the only");
                move_tail();
                duration_to_set = 0;
            } else {
                micros_t r = get_hard_timer_left_time();
                move_tail();
                timers_sum -= data[tail].second;
                data[tail].second += r;
                duration_to_set = data[tail].second;
                NS_LOG_DEBUG("there is another, left in hw: " << std::to_string(r) << ", to set: " << std::to_string(duration_to_set));
            }
        } else {
            NS_LOG_DEBUG("timer is smwhere else");
            duration_to_set = 0;

            uint8_t t_id = tail;

            while(data[t_id].first != seq_n && t_id != head) t_id = (t_id + 1) % (MAX_UNACK_PACKETS + 1);
            if(t_id == head) return -1;

            micros_t r = data[t_id].second;
            uint8_t i = t_id;
            while((i + 1) % (MAX_UNACK_PACKETS + 1) != head) {
                data[i] = data[(i + 1) % (MAX_UNACK_PACKETS + 1)];
                i = (i + 1) % (MAX_UNACK_PACKETS + 1);
            }
            rmove_head();
            if(t_id == head) {
                timers_sum -= r;
            }
            else {
                data[t_id].second += r;
            }
        }
        return 0;
    }

    int8_t TimerFifo::push_timer(uint8_t seq_n, micros_t duration, micros_t& duration_to_set) {
        if (!is_queue_have_space() || duration > MAX_TIMER_DURATION) {
            return -1;
        }
        if (head == tail) {
            data[head] = std::make_pair(seq_n, duration);
            duration_to_set = duration;
        } else {
            micros_t left = get_hard_timer_left_time();
            data[head] = std::make_pair(seq_n, duration - left - timers_sum);
            timers_sum += data[head].second;
            duration_to_set = 0;
        }
        micros_t r = data[head].second;
        move_head();
        return 0;
    }

    void TimerFifo::Print(std::ostream& os) const {
        for(uint16_t i = 0; i < MAX_UNACK_PACKETS + 1; ++i) {
            if(i == tail && i == head) os << "[]";
            else if(i == tail) os << "[";

            if((tail > head && (i >= tail || i < head)) || (head > tail && i >= tail && i < head)) {
                os << std::to_string(data[i].second) << "{" << std::to_string(data[i].first)<< "} "; 
            }
            else {
                if(i == head && i != tail) os << "] ";
                else if (i != MAX_UNACK_PACKETS) os <<  " NaN ";
            }

        }
        micros_t left = Simulator::GetDelayLeft(last_timer.e_id).GetMicroSeconds();
        os << "\n tail=" << std::to_string(tail) << ", head=" << std::to_string(head) <<  ", sum=" << std::to_string(timers_sum) << ", left_in_hw=" << std::to_string(left) << "\n";
    }

    std::ostream&
    operator<<(std::ostream& os, const TimerFifo& q)
    {
        q.Print(os);
        return os;
    }

    micros_t TimerFifo::get_hard_timer_left_time() {
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
       
        int64_t nanos = Simulator::GetDelayLeft(last_timer.e_id).GetNanoSeconds();
        int64_t micros = Simulator::GetDelayLeft(last_timer.e_id).GetMicroSeconds();
        if(nanos % 1000 > 500) return micros + 1;
        return micros;
    }

    int8_t TimerFifo::add_new_timer(uint8_t seq_n, const micros_t duration) {
        micros_t to_set;
        int8_t r = push_timer(seq_n, duration, to_set);
        if(r != 0) return -1;
        if(to_set != 0) {
            activate_timer(to_set);
        }
        NS_LOG_LOGIC("added new timer{" << std::to_string(seq_n) <<  "} for " << std::to_string(to_set) << " microseconds \n" << *this);
        return 0;
    }

    int8_t TimerFifo::cancel_timer(uint8_t seq_n) {
        int8_t was_in_hw = seq_n == data[tail].first; // if is timer that was on hw, remove from hw first
        micros_t to_set;
        int8_t r = pop_timer(seq_n, to_set);
        if(r != 0) return -1;
        NS_LOG_LOGIC("NODE[] canceled timer{" << std::to_string(seq_n) <<  "} for " << std::to_string(last_timer.val) << " microseconds: \n" << *this);
        if(was_in_hw) {
            Simulator::Cancel(last_timer.e_id);
        }
        if(to_set != 0) {
            activate_timer(to_set);
        }
        return 0;
    }

    void TimerFifo::timer_interrupt_handler() {
        uint8_t seq_n = data[tail].first;
        NS_LOG_LOGIC("timer is up (" << std::to_string(seq_n) <<  ")");
        micros_t to_set;
        int8_t r = pop_timer(seq_n, to_set);
        if(r == 0 && to_set != 0) {
            activate_timer(to_set);
        }
        upper_handler(seq_n);
    }

    int8_t TimerFifo::activate_timer(const micros_t duration) {
        last_timer.e_id = Simulator::Schedule(
                MicroSeconds(duration),
                &TimerFifo::timer_interrupt_handler,
                this
        );
        last_timer.val = duration;
        NS_LOG_LOGIC("ACTIVATED timer{" << std::to_string(data[tail].first) <<  "} for " << std::to_string(last_timer.val) << " microseconds \n" << *this);
        return 0;
    }
}