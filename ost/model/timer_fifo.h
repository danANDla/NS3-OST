#ifndef TIMER_FIFO_H
#define TIMER_FIFO_H
 
#define MAX_UNACK_PACKETS 10

#include <inttypes.h>
#include "ns3/event-id.h"

#include "ns3/callback.h"

/**
 * @ingroup ost
 * \defgroup Timer Timer structures and functions
 */

/**
* @ingroup Timer
* @typedef NanoSeconds
* @brief Целочисленный беззнаковый тип наносекунды
*/
typedef uint32_t nsecs_t;

/**
* @ingroup Timer
* @brief основная частота в кГц
*/
static const uint32_t CPU_FREQ = 32768; 
static const nsecs_t MAX_TIMER_DURATION = 16843000;

namespace ns3 {

/**
 * @ingroup Timer
 * @struct HardwareTimer
 * @brief Implementation (elvees 1892VM15F) of hardware timer.
 * 
 *
 * @var HardwareTimer::ITCR
 * Регистр управления (182F_5000). 5 разрядов.
 *
 * @var HardwareTimer::ITPERIOD
 * Регистр периода работы таймера (182F_5004). 32 разрядов.
 *
 * @var HardwareTimer::ITCOUNT
 * Регистр счетчика (182F_5008). 32 разрядов.
 *
 * @var HardwareTimer::ITSCALE
 * Регистр предделителя (182F_500C). 8 разрядов.
 */
typedef struct {
    uint8_t ITCR;
    uint32_t ITPERIOD;
    uint32_t ITCOUNT;
    uint8_t ITSCALE;
} HardwareTimer;

/**
 * \ingroup Timer
 * \class TimerFifo
 * \brief Data structure for helding retransmission timers.
 * 
 * FIFO oчередь хранящая значение времени через которое должен сработать
 * следующий таймер. При срабатывании прерывания удаляется верхний элемент,
 * новый таймер запускается с значением верхнего элемента очереди. При добавлении
 * нового таймера (который должен сработать через new_timer секунд) происходит
 * обращение к состоянию аппаратного таймера, определяется сколько времени прошло
 * с момента запуска таймера (time_passed). Новый таймер добавляется
 * в конец очереди с значением равным t = new_timer - timers_sum - time_passed.
 * 
 * @note Ожидается что таймеры имеют одинаковую длительность,
 * должно выполняться условие : new_timer > timers_sum - time_passed.
 *
 * @var TimerFifo::data
 * Массив времён в @ref ost::NanoSeconds "наносекундах"
 *
 * @var TimerFifo::head
 * Индекс первого элемента очереди
 *
 * @var TimerFifo::tail
 * Индекс последнего элемента очереди
 *
 * @var TimerFifo::timers_sum
 * Текущая сумма таймеров - время, через которое срабатает самый правый таймер
 * при условии что левой только что был запущен.

 * @var TimerFifo::last_timer
 * Продлжительность таймера, который сейчас тикает.
 */
class TimerFifo {
    struct Timer {
        EventId e_id;
        nsecs_t val;
    };

    public:
        typedef Callback<bool, uint8_t> TimerHandleCallback;
        TimerFifo(uint16_t win_sz);
        ~TimerFifo();

        bool is_queue_have_space();
        void Print(std::ostream& os) const;
        int8_t add_new_timer(uint8_t seq_n, nsecs_t duration);
        int8_t cancel_timer(uint8_t seq_n);
        void set_callback(TimerHandleCallback cb);
    private:
        nsecs_t get_hard_timer_left_time();
        int8_t activate_timer(const nsecs_t duration);
        nsecs_t push_timer(uint8_t seq_n, nsecs_t duration);
        nsecs_t pop_timer(uint8_t srq_n);
        void timer_interrupt_handler();

        std::pair<uint8_t, nsecs_t> data[MAX_UNACK_PACKETS];
        uint16_t head;
        uint16_t tail;
        uint16_t window_sz;
        nsecs_t timers_sum;
        struct Timer last_timer;
        HardwareTimer hw;
        TimerHandleCallback upper_handler;
};

std::ostream& operator<<(std::ostream& os, const TimerFifo& q);

}
#endif