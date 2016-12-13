// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2016, LH_Mouse. All wrongs reserved.

#ifndef POSEIDON_ASYNC_JOB_HPP_
#define POSEIDON_ASYNC_JOB_HPP_

#include "cxx_ver.hpp"
#include "cxx_util.hpp"
#include "job_base.hpp"
#include "job_promise.hpp"
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

namespace Poseidon {

template<typename ResultT>
class AsyncJob : public JobBase {
private:
	const boost::weak_ptr<const void> m_category;
	const boost::shared_ptr<JobPromiseContainer<ResultT> > m_promised_result;
	const boost::function<ResultT ()> m_proc;

public:
	AsyncJob(boost::weak_ptr<const void> category,
		boost::shared_ptr<JobPromiseContainer<ResultT> > promised_result, boost::function<ResultT ()> proc)
		: m_category(STD_MOVE_IDN(category))
		, m_promised_result(STD_MOVE_IDN(promised_result)), m_proc(STD_MOVE_IDN(proc))
	{
	}

protected:
	boost::weak_ptr<const void> get_category() const OVERRIDE {
		return m_category;
	}
	void perform() OVERRIDE
	try {
		m_promised_result->set_success(m_proc());
	} catch(std::exception &e){
#ifdef POSEIDON_CXX11
		m_promised_result->set_exception(std::current_exception());
#else
		m_promised_result->set_exception(boost::copy_exception(std::runtime_error(e.what())));
#endif
	} catch(...){
#ifdef POSEIDON_CXX11
		m_promised_result->set_exception(std::current_exception());
#else
		m_promised_result->set_exception(boost::copy_exception(std::bad_exception()));
#endif
	}
};

template<>
class AsyncJob<void> : public JobBase {
private:
	const boost::weak_ptr<const void> m_category;
	const boost::shared_ptr<JobPromise> m_promised_result;
	const boost::function<void ()> m_proc;

public:
	AsyncJob(boost::weak_ptr<const void> category,
		boost::shared_ptr<JobPromise> promised_result, boost::function<void ()> proc)
		: m_category(STD_MOVE_IDN(category))
		, m_promised_result(STD_MOVE_IDN(promised_result)), m_proc(STD_MOVE_IDN(proc))
	{
	}

protected:
	boost::weak_ptr<const void> get_category() const OVERRIDE {
		return m_category;
	}
	void perform() OVERRIDE
	try {
		m_proc();
		m_promised_result->set_success();
	} catch(std::exception &e){
#ifdef POSEIDON_CXX11
		m_promised_result->set_exception(std::current_exception());
#else
		m_promised_result->set_exception(boost::copy_exception(std::runtime_error(e.what())));
#endif
	} catch(...){
#ifdef POSEIDON_CXX11
		m_promised_result->set_exception(std::current_exception());
#else
		m_promised_result->set_exception(boost::copy_exception(std::bad_exception()));
#endif
	}
};

template<typename ProcT>
boost::shared_ptr<const JobPromiseContainer<VALUE_TYPE(DECLREF(ProcT)())> > enqueue_async_job(
	boost::weak_ptr<const void> category, ProcT proc, boost::shared_ptr<const bool> withdrawn = boost::shared_ptr<const bool>())
{
	typedef VALUE_TYPE(DECLREF(ProcT)()) result_type;
	AUTO(promised_result, boost::make_shared<JobPromiseContainer<result_type> >());
	enqueue(boost::make_shared<AsyncJob<result_type> >(STD_MOVE(category), promised_result, STD_MOVE_IDN(proc)), STD_MOVE(withdrawn));
	return STD_MOVE_IDN(promised_result);
}
template<typename ProcT>
boost::shared_ptr<const JobPromiseContainer<VALUE_TYPE(DECLREF(ProcT)())> > enqueue_async_job(
	ProcT proc, boost::shared_ptr<const bool> withdrawn = boost::shared_ptr<const bool>())
{
	return enqueue_async_job(boost::weak_ptr<const void>(), STD_MOVE_IDN(proc), STD_MOVE(withdrawn));
}

}

#endif
