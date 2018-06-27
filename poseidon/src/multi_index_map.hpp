// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2018, LH_Mouse. All wrongs reserved.

#ifndef POSEIDON_MULTI_INDEX_MAP_HPP_
#define POSEIDON_MULTI_INDEX_MAP_HPP_

/* 定义时需要指定索引。
 *
 *   using Element = ::std::pair<int, ::std::string>;
 *   POSEIDON_MULTI_INDEX_MAP(Container, Element,
 *     POSEIDON_UNIQUE_MEMBER_INDEX(first)
 *     POSEIDON_MULTI_MEMBER_INDEX(second)
 *     POSEIDON_SEQUENCE_INDEX()
 *   );
 *
 * 基本用法和 ::std::map 类似，只是 find, count, lower_bound, upper_bound, equal_range 等
 * 成员函数需要带一个非类型模板参数，指定使用第几个索引。
 *
 *   Container c;
 *   c.insert(Element(1, "abc"));
 *   c.insert(Element(2, "def"));
 *   std::cout <<c.find<0>(1)->second <<std::endl;   // "abc";
 *   assert(c.upper_bound<1>("zzz") == c.end<1>());  // 通过。
 */

#include "cxx_ver.hpp"
#include "pp_util.hpp"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

#define POSEIDON_MULTI_INDEX_MAP(Class_name_, Value_type_, indices_)	\
	class Class_name_ {	\
	private:	\
		template<typename ValT>	\
		class Key_setter {	\
		private:	\
			ValT m_val;	\
			\
		public:	\
			Key_setter(::Poseidon::Move<ValT> val)	\
				: m_val(STD_MOVE_IDN(val))	\
			{ }	\
			\
		public:	\
			template<typename TargetT>	\
			void operator()(TargetT &target) NOEXCEPT {	\
				target = STD_MOVE(m_val);	\
			}	\
		};	\
		\
	public:	\
		typedef Value_type_ value_type;	\
		typedef ::boost::multi_index::multi_index_container<value_type, ::boost::multi_index::indexed_by<POSEIDON_REST(void indices_)> > base_container;	\
		\
		typedef typename base_container::const_iterator const_iterator;	\
		typedef typename base_container::iterator iterator;	\
		typedef typename base_container::const_reverse_iterator const_reverse_iterator;	\
		typedef typename base_container::reverse_iterator reverse_iterator;	\
		\
	private:	\
		base_container m_elements;	\
		\
	public:	\
		template<int indexT>	\
		const typename base_container::nth_index<indexT>::type & get_index() const {	\
			return m_elements.get<indexT>();	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type & get_index(){	\
			return m_elements.get<indexT>();	\
		}	\
		\
		const_iterator begin() const {	\
			return m_elements.begin();	\
		}	\
		iterator begin(){	\
			return m_elements.begin();	\
		}	\
		const_iterator end() const {	\
			return m_elements.end();	\
		}	\
		iterator end(){	\
			return m_elements.end();	\
		}	\
		\
		const_reverse_iterator rbegin() const {	\
			return m_elements.rbegin();	\
		}	\
		reverse_iterator rbegin(){	\
			return m_elements.rbegin();	\
		}	\
		const_reverse_iterator rend() const {	\
			return m_elements.rend();	\
		}	\
		reverse_iterator rend(){	\
			return m_elements.rend();	\
		}	\
		\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::const_iterator begin() const {	\
			return get_index<indexT>().begin();	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator begin(){	\
			return get_index<indexT>().begin();	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::const_iterator end() const {	\
			return get_index<indexT>().end();	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator end(){	\
			return get_index<indexT>().end();	\
		}	\
		\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::const_reverse_iterator rbegin() const {	\
			return get_index<indexT>().rbegin();	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::reverse_iterator rbegin(){	\
			return get_index<indexT>().rbegin();	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::const_reverse_iterator rend() const {	\
			return get_index<indexT>().rend();	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::reverse_iterator rend(){	\
			return get_index<indexT>().rend();	\
		}	\
		\
		bool empty() const {	\
			return m_elements.empty();	\
		}	\
		::std::size_t size() const {	\
			return m_elements.size();	\
		}	\
		void clear(){	\
			m_elements.clear();	\
		}	\
		\
		void swap(Class_name_ &rhs) NOEXCEPT {	\
			m_elements.swap(rhs.m_elements);	\
		}	\
		\
		::std::pair<iterator, bool> insert(const value_type &val){	\
			return m_elements.insert(val);	\
		}	\
	ENABLE_IF_CXX11(	\
		::std::pair<iterator, bool> insert(value_type &&val){	\
			return m_elements.insert(::std::move(val));	\
		}	\
	)	\
		iterator insert(iterator pos, const value_type &val){	\
			return m_elements.insert(pos, val);	\
		}	\
	ENABLE_IF_CXX11(	\
		iterator insert(iterator pos, value_type &&val){	\
			return m_elements.insert(pos, ::std::move(val));	\
		}	\
	)	\
		\
		iterator erase(const_iterator pos){	\
			return m_elements.erase(pos);	\
		}	\
		iterator erase(const_iterator from, const_iterator to){	\
			return m_elements.erase(from, to);	\
		}	\
		\
		bool replace(iterator pos, const value_type &val){	\
			return m_elements.replace(pos, val);	\
		}	\
	ENABLE_IF_CXX11(	\
		bool replace(iterator pos, value_type &&val){	\
			return m_elements.replace(pos, ::std::move(val));	\
		}	\
	)	\
		\
		template<int indexT>	\
		::std::pair<typename base_container::nth_index<indexT>::type::iterator, bool>	insert(const value_type &val){	\
			return get_index<indexT>().insert(val);	\
		}	\
	ENABLE_IF_CXX11(	\
		template<int indexT>	\
		::std::pair<typename base_container::nth_index<indexT>::type::iterator, bool> insert(value_type &&val){	\
			return get_index<indexT>().insert(::std::move(val));	\
		}	\
	)	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator insert(typename base_container::nth_index<indexT>::type::iterator hint, const value_type &val){	\
			return get_index<indexT>().insert(hint, val);	\
		}	\
	ENABLE_IF_CXX11(	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator insert(typename base_container::nth_index<indexT>::type::iterator hint, value_type &&val){	\
			return get_index<indexT>().insert(hint, ::std::move(val));	\
		}	\
	)	\
		\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator erase(typename base_container::nth_index<indexT>::type::const_iterator pos){	\
			return get_index<indexT>().erase(pos);	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator erase(typename base_container::nth_index<indexT>::type::const_iterator from, typename base_container::nth_index<indexT>::type::const_iterator to){	\
			return get_index<indexT>().erase(from, to);	\
		}	\
		template<int indexT>	\
		std::size_t erase(const typename base_container::nth_index<indexT>::type::key_type &key){	\
			return get_index<indexT>().erase(key);	\
		}	\
		\
		template<int indexT>	\
		bool replace(typename base_container::nth_index<indexT>::type::iterator pos, const value_type &val){	\
			return get_index<indexT>().replace(pos, val);	\
		}	\
	ENABLE_IF_CXX11(	\
		template<int indexT>	\
		bool replace(typename base_container::nth_index<indexT>::type::iterator pos, value_type &&val){	\
			return get_index<indexT>().replace(pos, ::std::move(val));	\
		}	\
	)	\
		\
		template<int to_indexT>	\
		typename base_container::nth_index<to_indexT>::type::const_iterator project(const_iterator pos) const {	\
			return m_elements.project<to_indexT>(pos);	\
		}	\
		template<int to_indexT>	\
		typename base_container::nth_index<to_indexT>::type::iterator project(iterator pos){	\
			return m_elements.project<to_indexT>(pos);	\
		}	\
		\
		template<int to_indexT, int from_indexT>	\
		typename base_container::nth_index<to_indexT>::type::const_iterator project(typename base_container::nth_index<from_indexT>::type::const_iterator pos) const {	\
			return m_elements.project<to_indexT>(pos);	\
		}	\
		template<int to_indexT, int from_indexT>	\
		typename base_container::nth_index<to_indexT>::type::iterator project(typename base_container::nth_index<from_indexT>::type::iterator pos){	\
			return m_elements.project<to_indexT>(pos);	\
		}	\
		\
		template<int index_to_setT>	\
		bool set_key(iterator pos, typename base_container::nth_index<index_to_setT>::type::key_type key){	\
			return get_index<index_to_setT>().modify_key(m_elements.project<index_to_setT>(pos), Key_setter<VALUE_TYPE(key)>(STD_MOVE(key)));	\
		}	\
		template<int indexT, int index_to_setT>	\
		bool set_key(typename base_container::nth_index<indexT>::type::iterator pos, typename base_container::nth_index<index_to_setT>::type::key_type key){	\
			return get_index<index_to_setT>().modify_key(m_elements.project<index_to_setT>(pos), Key_setter<VALUE_TYPE(key)>(STD_MOVE(key)));	\
		}	\
		\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::const_iterator find(const typename base_container::nth_index<indexT>::type::key_type &key) const {	\
			return get_index<indexT>().find(key);	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator find(const typename base_container::nth_index<indexT>::type::key_type &key){	\
			return get_index<indexT>().find(key);	\
		}	\
		\
		template<int indexT>	\
		::std::size_t count(const typename base_container::nth_index<indexT>::type::key_type &key) const {	\
			return get_index<indexT>().count(key);	\
		}	\
		\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::const_iterator lower_bound(const typename base_container::nth_index<indexT>::type::key_type &key) const {	\
			return get_index<indexT>().lower_bound(key);	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator lower_bound(const typename base_container::nth_index<indexT>::type::key_type &key){	\
			return get_index<indexT>().lower_bound(key);	\
		}	\
		\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::const_iterator upper_bound(const typename base_container::nth_index<indexT>::type::key_type &key) const {	\
			return get_index<indexT>().upper_bound(key);	\
		}	\
		template<int indexT>	\
		typename base_container::nth_index<indexT>::type::iterator upper_bound(const typename base_container::nth_index<indexT>::type::key_type &key){	\
			return get_index<indexT>().upper_bound(key);	\
		}	\
		\
		template<int indexT>	\
		::std::pair<typename base_container::nth_index<indexT>::type::const_iterator, typename base_container::nth_index<indexT>::type::const_iterator> equal_range(const typename base_container::nth_index<indexT>::type::key_type &key) const {	\
			return get_index<indexT>().equal_range(key);	\
		}	\
		template<int indexT>	\
		::std::pair<typename base_container::nth_index<indexT>::type::iterator, typename base_container::nth_index<indexT>::type::iterator> equal_range(const typename base_container::nth_index<indexT>::type::key_type &key){	\
			return get_index<indexT>().equal_range(key);	\
		}	\
	}

#define POSEIDON_UNIQUE_INDEX(...)                   , ::boost::multi_index::ordered_unique< ::boost::multi_index::identity<value_type>, ## __VA_ARGS__>
#define POSEIDON_UNIQUE_MEMBER_INDEX(member_, ...)   , ::boost::multi_index::ordered_unique< ::boost::multi_index::member<value_type, CV_VALUE_TYPE(DECLREF(value_type).member_), &value_type::member_>, ## __VA_ARGS__>
#define POSEIDON_MULTI_INDEX(...)                    , ::boost::multi_index::ordered_non_unique< ::boost::multi_index::identity<value_type>, ## __VA_ARGS__>
#define POSEIDON_MULTI_MEMBER_INDEX(member_, ...)    , ::boost::multi_index::ordered_non_unique< ::boost::multi_index::member<value_type, CV_VALUE_TYPE(DECLREF(value_type).member_), &value_type::member_>, ## __VA_ARGS__>
#define POSEIDON_SEQUENCE_INDEX()                    , ::boost::multi_index::sequenced<>

#endif
