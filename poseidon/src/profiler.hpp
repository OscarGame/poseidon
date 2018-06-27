// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2018, LH_Mouse. All wrongs reserved.

#ifndef POSEIDON_PROFILER_HPP_
#define POSEIDON_PROFILER_HPP_

#include "cxx_ver.hpp"

namespace Poseidon {

class Profiler {
public:
	static void accumulate_all_in_thread() NOEXCEPT;

	static void * begin_stack_switch() NOEXCEPT;
	static void end_stack_switch(void *opaque) NOEXCEPT;

private:
	Profiler *const m_prev;
	const char *const m_file;
	const unsigned long m_line;
	const char *const m_func;

	double m_start;
	double m_excluded;
	double m_yielded_since;

public:
	Profiler(const char *file, unsigned long line, const char *func) NOEXCEPT;
	~Profiler() NOEXCEPT;

	Profiler(const Profiler &) = delete;
	Profiler &operator=(const Profiler &) = delete;

private:
	void accumulate(double now, bool new_sample) NOEXCEPT;
};

}

#define POSEIDON_PROFILE_ME  const ::Poseidon::Profiler POSEIDON_UNIQUE_NAME(__FILE__, __LINE__, __PRETTY_FUNCTION__)

#endif
