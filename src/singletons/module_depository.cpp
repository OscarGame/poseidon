// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2017, LH_Mouse. All wrongs reserved.

#include "../precompiled.hpp"
#include "module_depository.hpp"
#include <boost/type_traits/decay.hpp>
#include <dlfcn.h>
#include "main_config.hpp"
#include "../recursive_mutex.hpp"
#include "../log.hpp"
#include "../raii.hpp"
#include "../exception.hpp"
#include "../multi_index_map.hpp"
#include "../module_raii.hpp"

namespace Poseidon {

namespace {
	// 注意 dl 系列的函数都不是线程安全的。
	RecursiveMutex g_mutex;

	struct DynamicLibraryCloser {
		CONSTEXPR void *operator()() NOEXCEPT {
			return VAL_INIT;
		}
		void operator()(void *handle) NOEXCEPT {
			const RecursiveMutex::UniqueLock lock(g_mutex);
			if(::dlclose(handle) != 0){
				LOG_POSEIDON_WARNING("Error unloading dynamic library: ", ::dlerror());
			}
		}
	};
}

class Module : NONCOPYABLE {
private:
	const UniqueHandle<DynamicLibraryCloser> m_handle;
	const SharedNts m_real_path;
	void *const m_base_addr;

public:
	Module(Move<UniqueHandle<DynamicLibraryCloser> > handle, SharedNts real_path, void *base_addr)
		: m_handle(STD_MOVE(handle)), m_real_path(STD_MOVE(real_path)), m_base_addr(base_addr)
	{
		LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO, "Constructor of module: ", m_real_path);

		LOG_POSEIDON_DEBUG("Handle: ", m_handle);
		LOG_POSEIDON_DEBUG("Real path: ", m_real_path);
		LOG_POSEIDON_DEBUG("Base addr: ", m_base_addr);
	}
	~Module(){
		LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO, "Destructor of module: ", m_real_path);

		LOG_POSEIDON_DEBUG("Handle: ", m_handle);
		LOG_POSEIDON_DEBUG("Real path: ", m_real_path);
		LOG_POSEIDON_DEBUG("Base addr: ", m_base_addr);
	}

public:
	void *get_handle() const {
		return m_handle.get();
	}
	const SharedNts &get_real_path() const {
		return m_real_path;
	}
	void *get_base_addr() const {
		return m_base_addr;
	}
};

namespace {
	struct ModuleMapElement {
		boost::shared_ptr<Module> module;
		void *handle;
		SharedNts real_path;
		void *base_addr;

		boost::shared_ptr<HandleStack> handles;

		ModuleMapElement(boost::shared_ptr<Module> module_, boost::shared_ptr<HandleStack> handles_)
			: module(STD_MOVE(module_)), handle(module->get_handle())
			, real_path(module->get_real_path()), base_addr(module->get_base_addr())
			, handles(STD_MOVE(handles_))
		{ }
	};
	MULTI_INDEX_MAP(ModuleMap, ModuleMapElement,
		UNIQUE_MEMBER_INDEX(module)
		UNIQUE_MEMBER_INDEX(handle)
		MULTI_MEMBER_INDEX(real_path)
		UNIQUE_MEMBER_INDEX(base_addr)
	)
	ModuleMap g_module_map;

	struct ModuleRaiiMapElement {
		void *base_addr;

		ModuleRaiiBase *raii;
		long priority;

		ModuleRaiiMapElement(void *base_addr_,
			ModuleRaiiBase *raii_, long priority_)
			: base_addr(base_addr_)
			, raii(raii_), priority(priority_)
		{ }
	};
	MULTI_INDEX_MAP(ModuleRaiiMap, ModuleRaiiMapElement,
		UNIQUE_MEMBER_INDEX(raii)
		MULTI_MEMBER_INDEX(priority)
	)
	ModuleRaiiMap g_module_raii_map;
}

void ModuleDepository::start(){
	LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO, "Starting module depository...");
}
void ModuleDepository::stop(){
	LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO, "Unloading all modules...");

	std::vector<boost::weak_ptr<Module> > modules;
	{
		const RecursiveMutex::UniqueLock lock(g_mutex);
		modules.reserve(g_module_map.size());
		for(AUTO(it, g_module_map.begin()); it != g_module_map.end(); ++it){
			modules.push_back(it->module);
		}
		g_module_map.clear();
	}
	while(!modules.empty()){
		const AUTO(module, modules.back().lock());
		if(!module){
			modules.pop_back();
			continue;
		}
		LOG_POSEIDON_INFO("Waiting for module to unload: ", module->get_real_path());

		::timespec req;
		req.tv_sec = 0;
		req.tv_nsec = 100 * 1000 * 1000;
		::nanosleep(&req, NULLPTR);
	}
}

boost::shared_ptr<Module> ModuleDepository::load(const std::string &path){
	const RecursiveMutex::UniqueLock lock(g_mutex);

	LOG_POSEIDON_INFO("Checking whether module has already been loaded: ", path);
	UniqueHandle<DynamicLibraryCloser> handle;
	if(handle.reset(::dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD))){
		LOG_POSEIDON_DEBUG("Module already loaded, trying retrieving a shared_ptr from static map...");
		const AUTO(it, g_module_map.find<1>(handle.get()));
		if(it != g_module_map.end<1>()){
			LOG_POSEIDON_DEBUG("Got shared_ptr from loaded module: ", it->real_path);
			return it->module;
		}
		LOG_POSEIDON_DEBUG("... No module was found. Load it as a new module.");
	}

	LOG_POSEIDON_INFO("Loading new module: ", path);
	if(!handle.reset(::dlopen(path.c_str(), RTLD_NOW | RTLD_NODELETE | RTLD_DEEPBIND))){
		SharedNts error(::dlerror());
		LOG_POSEIDON_ERROR("Error loading dynamic library: ", error);
		DEBUG_THROW(Exception, STD_MOVE(error));
	}

	void *const init_sym = ::dlsym(handle.get(), "_init");
	if(!init_sym){
		SharedNts error(::dlerror());
		LOG_POSEIDON_ERROR("Error locating _init(): ", error);
		DEBUG_THROW(Exception, STD_MOVE(error));
	}
	::Dl_info info;
	if(::dladdr(init_sym, &info) == 0){
		SharedNts error(::dlerror());
		LOG_POSEIDON_ERROR("Error getting real path and base address: ", error);
		DEBUG_THROW(Exception, STD_MOVE(error));
	}
	SharedNts real_path(info.dli_fname);
	void *const base_addr = info.dli_fbase;

	AUTO(module, boost::make_shared<Module>(STD_MOVE(handle), real_path, base_addr));

	AUTO(handles, boost::make_shared<HandleStack>());
	LOG_POSEIDON_INFO("Initializing module: ", real_path);
	for(AUTO(it, g_module_raii_map.begin<1>()); it != g_module_raii_map.end<1>(); ++it){
		if(it->base_addr != base_addr){
			continue;
		}
		it->raii->init(*handles);
	}
	LOG_POSEIDON_INFO("Done initializing module: ", real_path);

	const AUTO(result, g_module_map.insert(ModuleMapElement(module, STD_MOVE(handles))));
	if(!result.second){
		LOG_POSEIDON_ERROR("Duplicate module: module = ", static_cast<void *>(module.get()),
			", handle = ", module->get_handle(), ", real_path = ", real_path, ", base_addr = ", base_addr);
		DEBUG_THROW(Exception, sslit("Duplicate module"));
	}

	return module;
}
boost::shared_ptr<Module> ModuleDepository::load_nothrow(const std::string &path)
try {
	return load(path);
} catch(Exception &e){
	LOG_POSEIDON_ERROR("Exception thrown while loading module: path = ", path, ", what = ", e.what());
	return VAL_INIT;
} catch(std::exception &e){
	LOG_POSEIDON_ERROR("std::exception thrown while loading module: path = ", path, ", what = ", e.what());
	return VAL_INIT;
} catch(...){
	LOG_POSEIDON_ERROR("Unknown exception thrown while loading module: path = ", path);
	return VAL_INIT;
}
bool ModuleDepository::unload(const boost::shared_ptr<Module> &module){
	const RecursiveMutex::UniqueLock lock(g_mutex);
	return g_module_map.erase<0>(module) > 0;
}
bool ModuleDepository::unload(void *base_addr){
	const RecursiveMutex::UniqueLock lock(g_mutex);
	return g_module_map.erase<3>(base_addr) > 0;
}

std::vector<ModuleDepository::SnapshotElement> ModuleDepository::snapshot(){
	std::vector<SnapshotElement> ret;
	{
		const RecursiveMutex::UniqueLock lock(g_mutex);
		for(AUTO(it, g_module_map.begin()); it != g_module_map.end(); ++it){
			ret.push_back(SnapshotElement());
			SnapshotElement &mi = ret.back();

			mi.real_path = it->real_path;
			mi.base_addr = it->base_addr;
			mi.ref_count = static_cast<unsigned long>(it->module.use_count()); // fvck
		}
	}
	return ret;
}

void ModuleDepository::register_module_raii(ModuleRaiiBase *raii, long priority){
	const RecursiveMutex::UniqueLock lock(g_mutex);
	::Dl_info info;
	if(::dladdr(raii, &info) == 0){
		SharedNts error(::dlerror());
		LOG_POSEIDON_ERROR("Error getting base address: ", error);
		DEBUG_THROW(Exception, STD_MOVE(error));
	}
	if(!g_module_raii_map.insert(ModuleRaiiMapElement(info.dli_fbase, raii, priority)).second){
		LOG_POSEIDON_ERROR("Duplicate ModuleRaii? fbase = ", info.dli_fbase, ", raii = ", static_cast<void *>(raii));
		DEBUG_THROW(Exception, sslit("Duplicate ModuleRaii"));
	}
}
void ModuleDepository::unregister_module_raii(ModuleRaiiBase *raii){
	const RecursiveMutex::UniqueLock lock(g_mutex);
	g_module_raii_map.erase<0>(raii);
}

}
