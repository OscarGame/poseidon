// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2018, LH_Mouse. All wrongs reserved.

#include "../precompiled.hpp"
#include "dns_daemon.hpp"
#include "../log.hpp"
#include "../atomic.hpp"
#include "../exception.hpp"
#include "../ip_port.hpp"
#include "../raii.hpp"
#include "../profiler.hpp"
#include <condition_variable>
#include <thread>
#include <netdb.h>
#include <unistd.h>

namespace Poseidon {

template class Promise_container<Sock_addr>;

namespace {
	struct Addrinfo_freeer {
		CONSTEXPR ::addrinfo * operator()() const NOEXCEPT {
			return NULLPTR;
		}
		void operator()(::addrinfo *res) const NOEXCEPT {
			::freeaddrinfo(res);
		}
	};

	Sock_addr real_dns_look_up(const std::string &host_raw, std::uint16_t port_raw, bool prefer_ipv4){
		Unique_handle<Addrinfo_freeer> res;
		std::string host;
		if(!host_raw.empty() && (host_raw.begin()[0] == '[') && (host_raw.end()[-1] == ']')){
			host.assign(host_raw.begin() + 1, host_raw.end() - 1);
		} else {
			host.assign(host_raw.begin(), host_raw.end());
		}
		char port[16];
		std::sprintf(port, "%u", port_raw);
		::addrinfo *res_ptr;
		const int gai_code = ::getaddrinfo(host.c_str(), port, NULLPTR, &res_ptr);
		if(gai_code != 0){
			const char *const err_msg = ::gai_strerror(gai_code);
			POSEIDON_LOG_DEBUG("DNS lookup failure: host:port = ", host, ":", port, ", gai_code = ", gai_code, ", err_msg = ", err_msg);
			POSEIDON_THROW(Exception, Rcnts(err_msg));
		}
		POSEIDON_THROW_ASSERT(res.reset(res_ptr));

		::addrinfo *res_ptr_ipv4 = NULLPTR;
		::addrinfo *res_ptr_ipv6 = NULLPTR;
		while(res_ptr){
			switch(res_ptr->ai_family){
			case AF_INET:
				res_ptr_ipv4 = res_ptr;
				break;
			case AF_INET6:
				res_ptr_ipv6 = res_ptr;
				break;
			}
			res_ptr = res_ptr->ai_next;
		}
		if(prefer_ipv4){
			res_ptr = res_ptr_ipv4;
		} else {
			res_ptr = res_ptr_ipv6;
		}
		if(!res_ptr){
			res_ptr = res.get();
		}
		Sock_addr sock_addr(res_ptr->ai_addr, res_ptr->ai_addrlen);
		POSEIDON_LOG_DEBUG("DNS lookup success: host:port = ", host, ":", port, ", result = ", Ip_port(sock_addr));
		return sock_addr;
	}

	volatile bool g_running = false;
	std::thread g_thread;

	struct Request_element {
		boost::weak_ptr<Promise_container<Sock_addr> > weak_promise;
		std::string host;
		std::uint16_t port;
		bool prefer_ipv4;
	};

	std::mutex g_mutex;
	std::condition_variable g_new_request;
	boost::container::deque<Request_element> g_queue;

	bool pump_one_element() NOEXCEPT {
		POSEIDON_PROFILE_ME;

		Request_element *elem;
		{
			const std::lock_guard<std::mutex> lock(g_mutex);
			if(g_queue.empty()){
				return false;
			}
			if(g_queue.front().weak_promise.expired()){
				g_queue.pop_front();
				return true;
			}
			elem = &g_queue.front();
		}
		Sock_addr sock_addr;
		STD_EXCEPTION_PTR except;
		try {
			sock_addr = real_dns_look_up(elem->host, elem->port, elem->prefer_ipv4);
		} catch(std::exception &e){
			POSEIDON_LOG_WARNING("std::exception thrown: what = ", e.what());
			except = STD_CURRENT_EXCEPTION();
		} catch(...){
			POSEIDON_LOG_WARNING("Unknown exception thrown.");
			except = STD_CURRENT_EXCEPTION();
		}
		const AUTO(promise, elem->weak_promise.lock());
		if(promise){
			if(except){
				promise->set_exception(STD_MOVE(except), false);
			} else {
				promise->set_success(STD_MOVE(sock_addr), false);
			}
		}
		const std::lock_guard<std::mutex> lock(g_mutex);
		g_queue.pop_front();
		return true;
	}

	void thread_proc(){
		POSEIDON_PROFILE_ME;

		Logger::set_thread_tag("   D");
		POSEIDON_LOG(Logger::special_major | Logger::level_info, "DNS daemon started.");

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
			g_new_request.wait_for(lock, std::chrono::milliseconds(timeout));
		}

		POSEIDON_LOG(Logger::special_major | Logger::level_info, "DNS daemon stopped.");
	}
}

void Dns_daemon::start(){
	if(atomic_exchange(g_running, true, memory_order_acq_rel) != false){
		POSEIDON_LOG_FATAL("Only one daemon is allowed at the same time.");
		std::terminate();
	}
	POSEIDON_LOG(Logger::special_major | Logger::level_info, "Starting DNS daemon...");

	g_thread = std::thread(&thread_proc);
}
void Dns_daemon::stop(){
	if(atomic_exchange(g_running, false, memory_order_acq_rel) == false){
		return;
	}
	POSEIDON_LOG(Logger::special_major | Logger::level_info, "Stopping DNS daemon...");

	if(g_thread.joinable()){
		g_thread.join();
	}

	const std::lock_guard<std::mutex> lock(g_mutex);
	g_queue.clear();
}

Sock_addr Dns_daemon::look_up(const std::string &host, std::uint16_t port, bool prefer_ipv4){
	POSEIDON_PROFILE_ME;

	return real_dns_look_up(host, port, prefer_ipv4);
}

boost::shared_ptr<const Promise_container<Sock_addr> > Dns_daemon::enqueue_for_looking_up(std::string host, std::uint16_t port, bool prefer_ipv4){
	POSEIDON_PROFILE_ME;

	AUTO(promise, boost::make_shared<Promise_container<Sock_addr> >());
	{
		const std::lock_guard<std::mutex> lock(g_mutex);
		Request_element elem = { promise, STD_MOVE(host), port, prefer_ipv4 };
		g_queue.push_back(STD_MOVE(elem));
		g_new_request.notify_one();
	}
	return STD_MOVE_IDN(promise);
}

}
