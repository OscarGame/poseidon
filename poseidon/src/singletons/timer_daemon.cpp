// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2018, LH_Mouse. All wrongs reserved.

#include "../precompiled.hpp"
#include "timer_daemon.hpp"
#include "job_dispatcher.hpp"
#include "../log.hpp"
#include "../atomic.hpp"
#include "../exception.hpp"
#include "../time.hpp"
#include "../job_base.hpp"
#include "../profiler.hpp"
#include "../checked_arithmetic.hpp"
#include <condition_variable>
#include <thread>

namespace Poseidon {

typedef Timer_daemon::Timer_callback Timer_callback;

class Timer {
private:
	std::uint64_t m_period;
	unsigned long m_stamp;
	Timer_callback m_callback;
	bool m_low_level;

public:
	Timer(std::uint64_t period, Timer_callback callback, bool low_level)
		: m_period(period), m_stamp(0), m_callback(STD_MOVE_IDN(callback)), m_low_level(low_level)
	{ }

	Timer(const Timer &) = delete;
	Timer &operator=(const Timer &) = delete;

public:
	std::uint64_t get_period() const {
		return m_period;
	}
	unsigned long get_stamp() const {
		return m_stamp;
	}
	const Timer_callback & get_callback() const {
		return m_callback;
	}
	bool is_low_level() const {
		return m_low_level;
	}

	unsigned long set_period(std::uint64_t period){
		if(period != Timer_daemon::period_intact){
			m_period = period;
		}
		return ++m_stamp;
	}
};

namespace {
	enum {
		ms_per_hour = 1000ull * 3600,
		ms_per_day  = ms_per_hour * 24,
		ms_per_week = ms_per_day * 7,
	};

	class Timer_job : public Job_base {
	private:
		const boost::weak_ptr<Timer> m_weak_timer;
		const std::uint64_t m_now;
		const std::uint64_t m_period;

	public:
		Timer_job(boost::weak_ptr<Timer> weak_timer, std::uint64_t now, std::uint64_t period)
			: m_weak_timer(STD_MOVE(weak_timer)), m_now(now), m_period(period)
		{
			//
		}

	public:
		boost::weak_ptr<const void> get_category() const OVERRIDE {
			return m_weak_timer;
		}
		void perform() OVERRIDE {
			POSEIDON_PROFILE_ME;

			const AUTO(timer, m_weak_timer.lock());
			if(!timer){
				return;
			}
			timer->get_callback()(timer, m_now, m_period);
		}
	};

	struct Timer_queue_element {
		boost::weak_ptr<Timer> timer;
		std::uint64_t next;
		unsigned long stamp;
	};

	bool operator<(const Timer_queue_element &lhs, const Timer_queue_element &rhs){
		return lhs.next > rhs.next;
	}

	volatile bool g_running = false;
	std::thread g_thread;

	std::mutex g_mutex;
	std::condition_variable g_new_timer;
	boost::container::vector<Timer_queue_element> g_timers;

	bool pump_one_element() NOEXCEPT {
		POSEIDON_PROFILE_ME;

		const AUTO(now, get_fast_mono_clock());

		boost::shared_ptr<Timer> timer;
		std::uint64_t period;
		{
			const std::lock_guard<std::mutex> lock(g_mutex);
		_pick_next:
			if(g_timers.empty()){
				return false;
			}
			if(now < g_timers.front().next){
				return false;
			}
			std::pop_heap(g_timers.begin(), g_timers.end());
			timer = g_timers.back().timer.lock();
			if(!timer || (timer->get_stamp() != g_timers.back().stamp)){
				g_timers.pop_back();
				goto _pick_next;
			}
			period = timer->get_period();
			if(period == 0){
				g_timers.pop_back();
			} else {
				g_timers.back().next = saturated_add(g_timers.back().next, period);
				std::push_heap(g_timers.begin(), g_timers.end());
			}
		}

		try {
			if(timer->is_low_level()){
				POSEIDON_LOG_TRACE("Dispatching low level timer: timer = ", timer);
				timer->get_callback()(timer, now, timer->get_period());
			} else {
				POSEIDON_LOG_TRACE("Preparing a timer job for dispatching: timer = ", timer);
				Job_dispatcher::enqueue(boost::make_shared<Timer_job>(timer, now, period), VAL_INIT);
			}
		} catch(std::exception &e){
			POSEIDON_LOG_WARNING("std::exception thrown while dispatching timer job, what = ", e.what());
		} catch(...){
			POSEIDON_LOG_WARNING("Unknown exception thrown while dispatching timer job.");
		}
		return true;
	}

	void thread_proc(){
		POSEIDON_PROFILE_ME;

		Logger::set_thread_tag("  T ");
		POSEIDON_LOG(Logger::special_major | Logger::level_info, "Timer daemon started.");

		int timeout = 0;
		for(;;){
			bool busy;
			do {
				busy = pump_one_element();
				timeout = std::min(timeout * 2 + 1, (1 - busy) * 128);
			} while(busy);

			std::unique_lock<std::mutex> lock(g_mutex);
			if(!atomic_load(g_running, memory_order_consume)){
				break;
			}
			g_new_timer.wait_for(lock, std::chrono::milliseconds(timeout));
		}

		POSEIDON_LOG(Logger::special_major | Logger::level_info, "Timer daemon stopped.");
	}
}

void Timer_daemon::start(){
	if(atomic_exchange(g_running, true, memory_order_acq_rel) != false){
		POSEIDON_LOG_FATAL("Only one daemon is allowed at the same time.");
		std::terminate();
	}
	POSEIDON_LOG(Logger::special_major | Logger::level_info, "Starting timer daemon...");

	g_thread = std::thread(&thread_proc);
}
void Timer_daemon::stop(){
	if(atomic_exchange(g_running, false, memory_order_acq_rel) == false){
		return;
	}
	POSEIDON_LOG(Logger::special_major | Logger::level_info, "Stopping timer daemon...");

	if(g_thread.joinable()){
		g_thread.join();
	}

	const std::lock_guard<std::mutex> lock(g_mutex);
	g_timers.clear();
}

boost::shared_ptr<Timer> Timer_daemon::register_absolute_timer(std::uint64_t first, std::uint64_t period, Timer_callback callback){
	POSEIDON_PROFILE_ME;

	AUTO(timer, boost::make_shared<Timer>(period, STD_MOVE_IDN(callback), false));
	{
		const std::lock_guard<std::mutex> lock(g_mutex);
		Timer_queue_element elem = { timer, first, timer->get_stamp() };
		g_timers.push_back(STD_MOVE(elem));
		std::push_heap(g_timers.begin(), g_timers.end());
		g_new_timer.notify_one();
	}
	POSEIDON_LOG_DEBUG("Created a timer which will be triggered ", saturated_sub(first, get_fast_mono_clock()), " microsecond(s) later and has a period of ", timer->get_period(), " microsecond(s).");
	return timer;
}
boost::shared_ptr<Timer> Timer_daemon::register_timer(std::uint64_t delta_first, std::uint64_t period, Timer_callback callback){
	const AUTO(now, get_fast_mono_clock());
	return register_absolute_timer(saturated_add(now, delta_first), period, STD_MOVE(callback));
}

boost::shared_ptr<Timer> Timer_daemon::register_hourly_timer(unsigned minute, unsigned second, Timer_callback callback, bool utc){
	const AUTO(virt_now, utc ? get_utc_time() : get_local_time());
	const AUTO(delta, checked_sub<std::uint64_t>(virt_now, (minute * 60ull + second) * 1000ull));
	return register_timer(ms_per_hour - delta % ms_per_hour, ms_per_hour, STD_MOVE(callback));
}
boost::shared_ptr<Timer> Timer_daemon::register_daily_timer(unsigned hour, unsigned minute, unsigned second, Timer_callback callback, bool utc){
	const AUTO(virt_now, utc ? get_utc_time() : get_local_time());
	const AUTO(delta, checked_sub<std::uint64_t>(virt_now, (hour * 3600ull + minute * 60ull + second) * 1000ull));
	return register_timer(ms_per_day - delta % ms_per_day, ms_per_day, STD_MOVE(callback));
}
boost::shared_ptr<Timer> Timer_daemon::register_weekly_timer(unsigned day_of_week, unsigned hour, unsigned minute, unsigned second, Timer_callback callback, bool utc){
	// 注意 1970-01-01 是星期四。
	const AUTO(virt_now, utc ? get_utc_time() : get_local_time());
	const AUTO(delta, checked_sub<std::uint64_t>(virt_now, ((day_of_week + 3ull) * 86400ull + hour * 3600ull + minute * 60ull + second) * 1000ull));
	return register_timer(ms_per_week - delta % ms_per_week, ms_per_week, STD_MOVE(callback));
}

boost::shared_ptr<Timer> Timer_daemon::register_low_level_absolute_timer(std::uint64_t first, std::uint64_t period, Timer_callback callback){
	POSEIDON_PROFILE_ME;

	AUTO(timer, boost::make_shared<Timer>(period, STD_MOVE_IDN(callback), true));
	{
		const std::lock_guard<std::mutex> lock(g_mutex);
		Timer_queue_element elem = { timer, first, timer->get_stamp() };
		g_timers.push_back(STD_MOVE(elem));
		std::push_heap(g_timers.begin(), g_timers.end());
		g_new_timer.notify_one();
	}
	POSEIDON_LOG_DEBUG("Created a low level timer which will be triggered ", saturated_sub(first, get_fast_mono_clock()), " microsecond(s) later and has a period of ", timer->get_period(), " microsecond(s).");
	return timer;
}
boost::shared_ptr<Timer> Timer_daemon::register_low_level_timer(std::uint64_t delta_first, std::uint64_t period, Timer_callback callback){
	const AUTO(now, get_fast_mono_clock());
	return register_low_level_absolute_timer(saturated_add(now, delta_first), period, STD_MOVE(callback));
}

void Timer_daemon::set_absolute_time(const boost::shared_ptr<Timer> &timer, std::uint64_t first, std::uint64_t period){
	POSEIDON_PROFILE_ME;

	const std::lock_guard<std::mutex> lock(g_mutex);
	g_timers.emplace_back(); // This may throw std::bad_alloc.
	Timer_queue_element elem = { timer, first, timer->set_period(period) }; // This throws no exception.
	g_timers.back() = STD_MOVE_IDN(elem); // This does not throw an exception, either.
	std::push_heap(g_timers.begin(), g_timers.end());
	g_new_timer.notify_one();
}
void Timer_daemon::set_time(const boost::shared_ptr<Timer> &timer, std::uint64_t delta_first, std::uint64_t period){
	const AUTO(now, get_fast_mono_clock());
	return set_absolute_time(timer, saturated_add(now, delta_first), period);
}

}
