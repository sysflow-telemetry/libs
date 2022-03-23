/*
Copyright (C) 2021 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#ifndef _WIN32
#include <dlfcn.h>
// This makes inttypes.h define PRIu32 (ISO C99 plus older g++ versions)
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <string.h>
#include <vector>
#include <set>
#include <sstream>
#endif
#include <numeric>
#include <json/json.h>
#include <valijson/adapters/jsoncpp_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validator.hpp>

#include "sinsp.h"
#include "sinsp_int.h"
#include "filter.h"
#include "filterchecks.h"
#include "plugin.h"

#include <third-party/tinydir.h>

using namespace std;

///////////////////////////////////////////////////////////////////////////////
// source_plugin filter check implementation
// This class implements a dynamic filter check that acts as a bridge to the
// plugin simplified field extraction implementations
///////////////////////////////////////////////////////////////////////////////

static std::set<uint16_t> s_all_plugin_event_types = {PPME_PLUGINEVENT_E};

static bool check_is_index(std::string& str)
{
	int length = str.length();
	if(length == 0 || (length > 1 && str[0] == '0'))
	{
		return false;
	}
	for(int j = 0; j < length; j++)
	{
		if(!isdigit(str[j]))
		{
			return false;
		}
	}
	return true;
}

class sinsp_filter_check_plugin : public sinsp_filter_check
{
public:
	sinsp_filter_check_plugin()
	{
		m_info.m_name = "plugin";
		m_info.m_fields = NULL;
		m_info.m_nfields = 0;
		m_info.m_flags = filter_check_info::FL_NONE;
		m_cnt = 0;
	}

	sinsp_filter_check_plugin(std::shared_ptr<sinsp_plugin> plugin)
		: m_plugin(plugin)
	{
		m_info.m_name = plugin->name() + string(" (plugin)");
		m_info.m_fields = plugin->fields();
		m_info.m_nfields = plugin->nfields();
		m_info.m_flags = filter_check_info::FL_NONE;
		m_cnt = 0;
	}

	sinsp_filter_check_plugin(const sinsp_filter_check_plugin &p)
	{
		m_plugin = p.m_plugin;
		m_info = p.m_info;
	}

	virtual ~sinsp_filter_check_plugin()
	{
	}

	const std::set<uint16_t> &evttypes()
	{
		return s_all_plugin_event_types;
	}

	int32_t parse_field_name(const char* str, bool alloc_state, bool needed_for_filtering)
	{
		int32_t res = sinsp_filter_check::parse_field_name(str, alloc_state, needed_for_filtering);

		if(res != -1)
		{
			m_arg_present = false;
			m_arg_key = NULL;
			m_arg_index = 0;
			// Read from str to the end-of-string, or first space
			string val(str);
			size_t val_end = val.find_first_of(' ', 0);
			if(val_end != string::npos)
			{
				val = val.substr(0, val_end);
			}

			size_t pos1 = val.find_first_of('[', 0);
			if(pos1 != string::npos)
			{
				size_t argstart = pos1 + 1;
				if(argstart < val.size())
				{
					m_argstr = val.substr(argstart);
					size_t pos2 = m_argstr.find_first_of(']', 0);
					if(pos2 != string::npos)
					{
						m_argstr = m_argstr.substr(0, pos2);
						if (!(m_info.m_fields[m_field_id].m_flags & filtercheck_field_flags::EPF_ARG_ALLOWED
								|| m_info.m_fields[m_field_id].m_flags & filtercheck_field_flags::EPF_ARG_REQUIRED))
						{
							throw sinsp_exception(string("filter ") + string(str) + string(" ")
								+ m_field->m_name + string(" does not allow nor require an argument but one is provided: " + m_argstr));
						}

						m_arg_present = true;

						if(m_info.m_fields[m_field_id].m_flags & filtercheck_field_flags::EPF_ARG_INDEX)
						{
							if(check_is_index(m_argstr))
							{
								try
								{
									m_arg_index = std::stoul(m_argstr);
								}
								catch(...)
								{
									throw sinsp_exception(string("filter ") + string(str) + string(" ")
										+ m_field->m_name + string(" has a numeric argument not representable on 64 bit: " + m_argstr));
								}
							}
							else
							{
								throw sinsp_exception(string("filter ") + string(str) + string(" ")
									+ m_field->m_name + string(" needs a numeric argument. '") + m_argstr + string("' is not numeric."));
							}
						}

						if(m_info.m_fields[m_field_id].m_flags & filtercheck_field_flags::EPF_ARG_KEY)
						{
							m_arg_key = (char*)m_argstr.c_str();
						}

						return pos1 + pos2 + 2;
					}
				}
				throw sinsp_exception(string("filter ") + string(str) + string(" ") + m_field->m_name + string(" has a badly-formatted argument"));
			}
			if (m_info.m_fields[m_field_id].m_flags & filtercheck_field_flags::EPF_ARG_REQUIRED)
			{
				throw sinsp_exception(string("filter ") + string(str) + string(" ") + m_field->m_name + string(" requires an argument but none provided"));
			}
		}

		return res;
	}

	sinsp_filter_check* allocate_new()
	{
		return new sinsp_filter_check_plugin(*this);
	}

	bool extract(sinsp_evt *evt, OUT vector<extract_value_t>& values, bool sanitize_strings = true)
	{
		//
		// Reject any event that is not generated by a plugin
		//
		if(evt->get_type() != PPME_PLUGINEVENT_E)
		{
			return false;
		}

		//
		// If this is a source plugin, reject events that have
		// not been generated by a plugin with this id specifically.
		//
		// XXX/mstemm this should probably check the version as well.
		//
		sinsp_evt_param *parinfo;
		if(m_plugin->type() == TYPE_SOURCE_PLUGIN)
		{
			sinsp_source_plugin *splugin = static_cast<sinsp_source_plugin *>(m_plugin.get());
			parinfo = evt->get_param(0);
			ASSERT(parinfo->m_len == sizeof(int32_t));
			uint32_t pgid = *(int32_t *)parinfo->m_val;
			if(pgid != splugin->id())
			{
				return false;
			}
		}

		//
		// If this is an extractor plugin, only attempt to
		// extract if the source is compatible with the event
		// source.
		//
		if(m_plugin->type() == TYPE_EXTRACTOR_PLUGIN)
		{
			sinsp_extractor_plugin *eplugin = static_cast<sinsp_extractor_plugin *>(m_plugin.get());
			parinfo = evt->get_param(0);
			ASSERT(parinfo->m_len == sizeof(int32_t));
			uint32_t pgid = *(int32_t *)parinfo->m_val;

			std::shared_ptr<sinsp_plugin> plugin = m_inspector->get_plugin_by_id(pgid);

			if(!plugin)
			{
				return false;
			}

			sinsp_source_plugin *splugin = static_cast<sinsp_source_plugin *>(plugin.get());

			if(!eplugin->source_compatible(splugin->event_source()))
			{
				return false;
			}
		}

		//
		// Get the event payload
		//
		parinfo = evt->get_param(1);

		ppm_param_type type = m_info.m_fields[m_field_id].m_type;

		ss_plugin_event pevt;
		pevt.evtnum = evt->get_num();
		pevt.data = (uint8_t *) parinfo->m_val;
		pevt.datalen = parinfo->m_len;
		pevt.ts = evt->get_ts();

		uint32_t num_fields = 1;
		ss_plugin_extract_field efield;
		efield.field_id = m_field_id;
		efield.field = m_info.m_fields[m_field_id].m_name;
		efield.arg_key = m_arg_key;
		efield.arg_index = m_arg_index;
		efield.arg_present = m_arg_present;
		efield.ftype = type;
		efield.flist = m_info.m_fields[m_field_id].m_flags & EPF_IS_LIST;
		if (!m_plugin->extract_fields(pevt, num_fields, &efield) || efield.res_len == 0)
		{
			return false;
		}

		values.clear();
		switch(type)
		{
			case PT_CHARBUF:
			{
				if (m_res_str_storage.size() < efield.res_len)
				{
					m_res_str_storage.resize(efield.res_len);
				}
				break;
			}
			case PT_UINT64:
			{
				if (m_res_u64_storage.size() < efield.res_len)
				{
					m_res_u64_storage.resize(efield.res_len);
				}
				break;
			}
			default:
				break;
		}
		for (uint32_t i = 0; i < efield.res_len; ++i)
		{
			extract_value_t res;
			switch(type)
			{
				case PT_CHARBUF:
				{
					m_res_str_storage[i] = efield.res.str[i];
					res.len = m_res_str_storage[i].size();
					res.ptr = (uint8_t*) m_res_str_storage[i].c_str();
					break;
				}
				case PT_UINT64:
				{
					m_res_u64_storage[i] = efield.res.u64[i];
					res.len = sizeof(uint64_t);
					res.ptr = (uint8_t*) &m_res_u64_storage[i];
					break;
				}
				default:
					ASSERT(false);
					throw sinsp_exception("plugin extract error: unsupported field type " + to_string(type));
					break;
			}
			values.push_back(res);
		}
		
		return true;
	}

	// XXX/mstemm m_cnt unused so far.
	uint64_t m_cnt;
	string m_argstr;
	char* m_arg_key;
	uint64_t m_arg_index;
	bool m_arg_present;

	vector<std::string> m_res_str_storage;
	vector<uint64_t> m_res_u64_storage;

	std::shared_ptr<sinsp_plugin> m_plugin;
};

///////////////////////////////////////////////////////////////////////////////
// sinsp_plugin implementation
///////////////////////////////////////////////////////////////////////////////
sinsp_plugin::version::version()
	: m_valid(false)
{
}

sinsp_plugin::version::version(const std::string &version_str)
	: m_valid(false)
{
	m_valid = (sscanf(version_str.c_str(), "%" PRIu32 ".%" PRIu32 ".%" PRIu32,
			  &m_version_major, &m_version_minor, &m_version_patch) == 3);
}

sinsp_plugin::version::~version()
{
}

std::string sinsp_plugin::version::as_string() const
{
	return std::to_string(m_version_major) + "." +
		std::to_string(m_version_minor) + "." +
		std::to_string(m_version_patch);
}

bool sinsp_plugin::version::check(version &requested) const
{
	if(this->m_version_major != requested.m_version_major)
	{
		// major numbers disagree
		return false;
	}

	if(this->m_version_minor < requested.m_version_minor)
	{
		// framework's minor version is < requested one
		return false;
	}
	if(this->m_version_minor == requested.m_version_minor && this->m_version_patch < requested.m_version_patch)
	{
		// framework's patch level is < requested one
		return false;
	}
	return true;
}

std::shared_ptr<sinsp_plugin> sinsp_plugin::register_plugin(sinsp* inspector,
							    string filepath,
							    const char* config,
							    filter_check_list &available_checks)
{
	string errstr;
	std::shared_ptr<sinsp_plugin> plugin = create_plugin(filepath, config, errstr);

	if (!plugin)
	{
		throw sinsp_exception("cannot load plugin " + filepath + ": " + errstr.c_str());
	}

	try
	{
		inspector->add_plugin(plugin);
	}
	catch(sinsp_exception const& e)
	{
		throw sinsp_exception("cannot add plugin " + filepath + " to inspector: " + e.what());
	}

	//
	// Create and register the filter checks associated to this plugin
	//

	// Only add the gen_event filter checks for source
	// plugins. Extractor plugins don't deal with event
	// timestamps/etc and don't need these checks (They were
	// probably added by the associated source plugins anyway).
	if(plugin->type() == TYPE_SOURCE_PLUGIN)
	{
		auto evt_filtercheck = new sinsp_filter_check_gen_event();
		available_checks.add_filter_check(evt_filtercheck);
	}

	auto filtercheck = new sinsp_filter_check_plugin(plugin);
	available_checks.add_filter_check(filtercheck);

	return plugin;
}

std::shared_ptr<sinsp_plugin> sinsp_plugin::create_plugin(string &filepath, const char* config, std::string &errstr)
{
	std::shared_ptr<sinsp_plugin> ret;

#ifdef _WIN32
	sinsp_plugin_handle handle = LoadLibrary(filepath.c_str());

	if(handle == NULL)
	{
		errstr = "error loading plugin " + filepath + ": ";
		DWORD flg = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
		LPTSTR msg_buf = 0;
		if(FormatMessageA(flg, 0, GetLastError(), 0, (LPTSTR)&msg_buf, 0, NULL))
		if(msg_buf)
		{
			errstr.append(msg_buf, strlen(msg_buf));
			LocalFree(msg_buf);
		}
		return ret;
	}
#else
	sinsp_plugin_handle handle = dlopen(filepath.c_str(), RTLD_LAZY);

	if(handle == NULL)
	{
		errstr = "error loading plugin " + filepath + ": " + dlerror();
		return ret;
	}
#endif

	// Before doing anything else, check the required api
	// version. If it doesn't match, return an error.

	// The pointer indirection and reference is because c++ doesn't
	// strictly allow casting void * to a function pointer. (See
	// http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#195).
	char * (*get_required_api_version)();
	*(void **) (&get_required_api_version) = getsym(handle, "plugin_get_required_api_version", errstr);
	if(get_required_api_version == NULL)
	{
		errstr = string("Could not resolve plugin_get_required_api_version function");
		return ret;
	}

	char *version_cstr = get_required_api_version();
	std::string version_str = version_cstr;
	version requestedVers(version_str);
	if(!requestedVers.m_valid)
	{
		errstr = string("Could not parse version string from ") + version_str;
		return ret;
	}
	// This is always valid
	version frameworkVers(PLUGIN_API_VERSION_STR);
	if(!frameworkVers.check(requestedVers))
	{
		errstr = string("Unsupported plugin required api version ") + version_str;
		return ret;
	}

	ss_plugin_type (*get_type)();
	*(void **) (&get_type) = getsym(handle, "plugin_get_type", errstr);
	if(get_type == NULL)
	{
		errstr = string("Could not resolve plugin_get_type function");
		return ret;
	}

	ss_plugin_type plugin_type = get_type();

	sinsp_source_plugin *splugin;
	sinsp_extractor_plugin *eplugin;

	switch(plugin_type)
	{
	case TYPE_SOURCE_PLUGIN:
		splugin = new sinsp_source_plugin(handle);
		if(!splugin->resolve_dylib_symbols(errstr))
		{
			delete splugin;
			return ret;
		}
		ret.reset(splugin);
		break;
	case TYPE_EXTRACTOR_PLUGIN:
		eplugin = new sinsp_extractor_plugin(handle);
		if(!eplugin->resolve_dylib_symbols(errstr))
		{
			delete eplugin;
			return ret;
		}
		ret.reset(eplugin);
		break;
	default:
		errstr = string("Wrong plugin type.");
		destroy_handle(handle);
		return ret;
	}

	errstr = "";

	// Initialize the plugin
	std::string conf = ret->str_from_alloc_charbuf(config);
	ret->validate_init_config(conf);
	if (!ret->init(conf.c_str()))
	{
		errstr = string("Could not initialize plugin: " + ret->get_last_error());
		ret = NULL;
	}

	return ret;
}

std::list<sinsp_plugin::info> sinsp_plugin::plugin_infos(sinsp* inspector)
{
	std::list<sinsp_plugin::info> ret;

	for(auto p : inspector->get_plugins())
	{
		sinsp_plugin::info info;
		info.name = p->name();
		info.description = p->description();
		info.contact = p->contact();
		info.plugin_version = p->plugin_version();
		info.required_api_version = p->required_api_version();
		info.type = p->type();

		if(info.type == TYPE_SOURCE_PLUGIN)
		{
			sinsp_source_plugin *sp = static_cast<sinsp_source_plugin *>(p.get());
			info.id = sp->id();
		}
		ret.push_back(info);
	}

	return ret;
}

bool sinsp_plugin::is_plugin_loaded(std::string &filepath)
{
#ifdef _WIN32
	/*
	 * LoadLibrary maps the module into the address space of the calling process, if necessary,
	 * and increments the modules reference count, if it is already mapped.
	 * GetModuleHandle, however, returns the handle to a mapped module
	 * without incrementing its reference count.
	 *
	 * This returns an HMODULE indeed, but they are the same thing
	 */
	sinsp_plugin_handle handle = (HINSTANCE)GetModuleHandle(filepath.c_str());
#else
	/*
	 * RTLD_NOLOAD (since glibc 2.2)
	 *	Don't load the shared object. This can be used to test if
	 *	the object is already resident (dlopen() returns NULL if
	 *	it is not, or the object's handle if it is resident).
	 *	This does not increment dlobject reference count.
	 */
	sinsp_plugin_handle handle = dlopen(filepath.c_str(), RTLD_LAZY | RTLD_NOLOAD);
#endif
	return handle != NULL;
}

sinsp_plugin::sinsp_plugin(sinsp_plugin_handle handle)
	: m_handle(handle), m_nfields(0)
{
}

sinsp_plugin::~sinsp_plugin()
{
	destroy_handle(m_handle);
}

bool sinsp_plugin::init(const char *config)
{
	if (!m_plugin_info.init)
	{
		return false;
	}

	ss_plugin_rc rc;

	ss_plugin_t *state = m_plugin_info.init(config, &rc);
	if (state != NULL)
	{
		// Plugins can return a state even if the result code is
		// SS_PLUGIN_FAILURE, which can be useful to set an init
		// error that can later be retrieved through get_last_error().
		set_plugin_state(state);
	}

	return rc == SS_PLUGIN_SUCCESS;
}

void sinsp_plugin::destroy()
{
	if(plugin_state() && m_plugin_info.destroy)
	{
		m_plugin_info.destroy(plugin_state());
		set_plugin_state(NULL);
	}
}

std::string sinsp_plugin::get_last_error()
{
	std::string ret;

	if(plugin_state() && m_plugin_info.get_last_error)
	{
		ret = str_from_alloc_charbuf(m_plugin_info.get_last_error(plugin_state()));
	}
	else
	{
		ret = "Plugin handle or get_last_error function not defined";
	}

	return ret;
}

const std::string &sinsp_plugin::name()
{
	return m_name;
}

const std::string &sinsp_plugin::description()
{
	return m_description;
}

const std::string &sinsp_plugin::contact()
{
	return m_contact;
}

const sinsp_plugin::version &sinsp_plugin::plugin_version()
{
	return m_plugin_version;
}

const sinsp_plugin::version &sinsp_plugin::required_api_version()
{
	return m_required_api_version;
}

const filtercheck_field_info *sinsp_plugin::fields()
{
	return m_fields.get();
}

uint32_t sinsp_plugin::nfields()
{
	return m_nfields;
}

bool sinsp_plugin::extract_fields(ss_plugin_event &evt, uint32_t num_fields, ss_plugin_extract_field *fields)
{
	if(!m_plugin_info.extract_fields || !plugin_state())
	{
		return false;
	}

	return m_plugin_info.extract_fields(plugin_state(), &evt, num_fields, fields) == SS_PLUGIN_SUCCESS;
}
void* sinsp_plugin::getsym(sinsp_plugin_handle handle, const char* name, std::string &errstr)
{
	void *ret;

#ifdef _WIN32
	ret = GetProcAddress(handle, name);
#else
	ret = dlsym(handle, name);
#endif

	if(ret == NULL)
	{
		errstr = string("Dynamic library symbol ") + name + " not present";
	} else {
		errstr = "";
	}

	return ret;
}

// Used below--set a std::string from the provided allocated charbuf and free() the charbuf.
std::string sinsp_plugin::str_from_alloc_charbuf(const char* charbuf)
{
	std::string str;

	if(charbuf != NULL)
	{
		str = charbuf;
	}

	return str;
}

void sinsp_plugin::resolve_dylib_field_arg(Json::Value root, filtercheck_field_info &tf)
{
	if (root.isNull())
	{
		return;
	}

	const Json::Value &isRequired = root.get("isRequired", Json::Value::null);
	if (!isRequired.isNull())
	{
		if (!isRequired.isBool())
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": field " + tf.m_name + " isRequired property is not boolean");
		}

		if (isRequired.asBool() == true)
		{
			// All the extra casting is because this is the one flags value
			// that is strongly typed and not just an int.
			tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_ARG_REQUIRED);
		}
	}

	const Json::Value &isIndex = root.get("isIndex", Json::Value::null);
	if (!isIndex.isNull())
	{
		if (!isIndex.isBool())
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": field " + tf.m_name + " isIndex property is not boolean");
		}

		if (isIndex.asBool() == true)
		{
			// We set `EPF_ARG_ALLOWED` implicitly.
			tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_ARG_INDEX);
			tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_ARG_ALLOWED);
		}
	}

	const Json::Value &isKey = root.get("isKey", Json::Value::null);
	if (!isKey.isNull())
	{
		if (!isKey.isBool())
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": field " + tf.m_name + " isKey property is not boolean");
		}

		if (isKey.asBool() == true)
		{
			// We set `EPF_ARG_ALLOWED` implicitly.
			tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_ARG_KEY);
			tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_ARG_ALLOWED);
		}
	}

	if((tf.m_flags & filtercheck_field_flags::EPF_ARG_REQUIRED) 
		&& !(tf.m_flags & filtercheck_field_flags::EPF_ARG_INDEX 
			|| tf.m_flags & filtercheck_field_flags::EPF_ARG_KEY))
	{
		throw sinsp_exception(string("error in plugin ") + m_name + ": field " + tf.m_name + " arg has isRequired true, but none of isKey nor isIndex is true");
	}
	return;
}

bool sinsp_plugin::resolve_dylib_symbols(std::string &errstr)
{
	// Some functions are required and return false if not found.
	if((*(void **) (&(m_plugin_info.get_required_api_version)) = getsym(m_handle, "plugin_get_required_api_version", errstr)) == NULL ||
	   (*(void **) (&(m_plugin_info.get_last_error)) = getsym(m_handle, "plugin_get_last_error", errstr)) == NULL ||
	   (*(void **) (&(m_plugin_info.get_name)) = getsym(m_handle, "plugin_get_name", errstr)) == NULL ||
	   (*(void **) (&(m_plugin_info.get_description)) = getsym(m_handle, "plugin_get_description", errstr)) == NULL ||
	   (*(void **) (&(m_plugin_info.get_contact)) = getsym(m_handle, "plugin_get_contact", errstr)) == NULL ||
	   (*(void **) (&(m_plugin_info.get_version)) = getsym(m_handle, "plugin_get_version", errstr)) == NULL)
	{
		return false;
	}

	// Others are not and the values will be checked when needed.
	(*(void **) (&m_plugin_info.init)) = getsym(m_handle, "plugin_init", errstr);
	(*(void **) (&m_plugin_info.destroy)) = getsym(m_handle, "plugin_destroy", errstr);
	(*(void **) (&m_plugin_info.get_fields)) = getsym(m_handle, "plugin_get_fields", errstr);
	(*(void **) (&m_plugin_info.extract_fields)) = getsym(m_handle, "plugin_extract_fields", errstr);
	(*(void **) (&m_plugin_info.get_init_schema)) = getsym(m_handle, "plugin_get_init_schema", errstr);

	m_name = str_from_alloc_charbuf(m_plugin_info.get_name());
	m_description = str_from_alloc_charbuf(m_plugin_info.get_description());
	m_contact = str_from_alloc_charbuf(m_plugin_info.get_contact());
	std::string version_str = str_from_alloc_charbuf(m_plugin_info.get_version());
	m_plugin_version = sinsp_plugin::version(version_str);
	if(!m_plugin_version.m_valid)
	{
		errstr = string("Could not parse version string from ") + version_str;
		return false;
	}

	// The required api version was already checked in
	// create_plugin to be valid and compatible. This just saves it for info/debugging.
	version_str = str_from_alloc_charbuf(m_plugin_info.get_required_api_version());
	m_required_api_version = sinsp_plugin::version(version_str);

	//
	// If filter fields are exported by the plugin, get the json from get_fields(),
	// parse it, create our list of fields, and create a filtercheck from the fields.
	//
	if(m_plugin_info.get_fields)
	{
		const char* sfields = m_plugin_info.get_fields();
		if(sfields == NULL)
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": get_fields returned a null string");
		}
		string json(sfields);
		SINSP_DEBUG("Parsing Fields JSON=%s", json.c_str());
		Json::Value root;
		if(Json::Reader().parse(json, root) == false || root.type() != Json::arrayValue)
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": get_fields returned an invalid JSON");
		}

		filtercheck_field_info *fields = new filtercheck_field_info[root.size()];
		if(fields == NULL)
		{
			throw sinsp_exception(string("error in plugin ") + m_name + ": could not allocate memory");
		}

		// Take ownership of the pointer right away so it can't be leaked.
		m_fields.reset(fields);
		m_nfields = root.size();

		for(Json::Value::ArrayIndex j = 0; j < root.size(); j++)
		{
			filtercheck_field_info &tf = m_fields.get()[j];
			tf.m_flags = EPF_NONE;

			const Json::Value &jvtype = root[j]["type"];
			string ftype = jvtype.asString();
			if(ftype == "")
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": field JSON entry has no type");
			}
			const Json::Value &jvname = root[j]["name"];
			string fname = jvname.asString();
			if(fname == "")
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": field JSON entry has no name");
			}
			const Json::Value &jvdisplay = root[j]["display"];
			string fdisplay = jvdisplay.asString();
			const Json::Value &jvdesc = root[j]["desc"];
			string fdesc = jvdesc.asString();
			if(fdesc == "")
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": field JSON entry has no desc");
			}

			strlcpy(tf.m_name, fname.c_str(), sizeof(tf.m_name));
			strlcpy(tf.m_display, fdisplay.c_str(), sizeof(tf.m_display));
			strlcpy(tf.m_description, fdesc.c_str(), sizeof(tf.m_description));
			tf.m_print_format = PF_DEC;

			if(ftype == "string")
			{
				tf.m_type = PT_CHARBUF;
			}
			else if(ftype == "uint64")
			{
				tf.m_type = PT_UINT64;
			}
			else
			{
				throw sinsp_exception(string("error in plugin ") + m_name + ": invalid field type " + ftype);
			}

			const Json::Value &jvIsList = root[j].get("isList", Json::Value::null);
			if (!jvIsList.isNull())
			{
				if (!jvIsList.isBool())
				{
					throw sinsp_exception(string("error in plugin ") + m_name + ": field " + fname + " isList property is not boolean ");
				}

				if (jvIsList.asBool() == true)
				{
					tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_IS_LIST);
				}
			}

			resolve_dylib_field_arg(root[j].get("arg", Json::Value::null), tf);

			const Json::Value &jvProperties = root[j].get("properties", Json::Value::null);
			if (!jvProperties.isNull())
			{
				if (!jvProperties.isArray())
				{
					throw sinsp_exception(string("error in plugin ") + m_name + ": field " + fname + " properties property is not array ");
				}

				for(Json::Value::ArrayIndex k = 0; k < jvProperties.size(); k++)
				{
					const Json::Value &prop = jvProperties[k];

					if (!prop.isString())
					{
						throw sinsp_exception(string("error in plugin ") + m_name + ": field " + fname + " properties value is not string ");
					}

					const std::string &str = prop.asString();

					// "hidden" is used inside and outside libs. "info" and "conversation" are used outside libs.
					if(str == "hidden")
					{
						tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_TABLE_ONLY);
					}
					else if(str == "info")
					{
						tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_INFO);
					}
					else if(str == "conversation")
					{
						tf.m_flags = (filtercheck_field_flags) ((int) tf.m_flags | (int) filtercheck_field_flags::EPF_CONVERSATION);
					}
				}
			}
		}

	}

	return true;
}

std::string sinsp_plugin::get_init_schema(ss_plugin_schema_type& schema_type)
{
	schema_type = SS_PLUGIN_SCHEMA_NONE;
	if (m_plugin_info.get_init_schema != NULL)
	{
		return str_from_alloc_charbuf(m_plugin_info.get_init_schema(&schema_type));
	}
	return std::string("");
}

void sinsp_plugin::validate_init_config(std::string& config)
{
	ss_plugin_schema_type schema_type;
	std::string schema = get_init_schema(schema_type);
	if (schema.size() > 0 && schema_type != SS_PLUGIN_SCHEMA_NONE)
	{
		switch (schema_type)
		{
			case SS_PLUGIN_SCHEMA_JSON:
				validate_init_config_json_schema(config, schema);
				break;
			default:
				ASSERT(false);
				throw sinsp_exception(
					string("error in plugin ")
					+ name()
					+ ": get_init_schema returned an unknown schema type "
					+ to_string(schema_type));
		}
	}
}

void sinsp_plugin::validate_init_config_json_schema(std::string& config, std::string &schema)
{
	Json::Value schemaJson;
	if(!Json::Reader().parse(schema, schemaJson) || schemaJson.type() != Json::objectValue)
	{
		throw sinsp_exception(
			string("error in plugin ")
			+ name()
			+ ": get_init_schema did not return a json object");
	}

	// stub empty configs to an empty json object
	if (config.size() == 0)
	{
		config = "{}";
	}
	Json::Value configJson;
	if(!Json::Reader().parse(config, configJson))
	{
		throw sinsp_exception(
			string("error in plugin ")
			+ name()
			+ ": init config is not a valid json");
	}

	// validate config with json schema
	valijson::Schema schemaDef;
	valijson::SchemaParser schemaParser;
	valijson::Validator validator;
	valijson::ValidationResults validationResults;
	valijson::adapters::JsonCppAdapter configAdapter(configJson);
	valijson::adapters::JsonCppAdapter schemaAdapter(schemaJson);
	schemaParser.populateSchema(schemaAdapter, schemaDef);
	if (!validator.validate(schemaDef, configAdapter, &validationResults))
	{
		valijson::ValidationResults::Error error;
		// report only the top-most error
		if (validationResults.popError(error))
		{
			throw sinsp_exception(
				string("error in plugin ")
				+ name()
				+ " init config: In "
				+ std::accumulate(error.context.begin(), error.context.end(), std::string(""))
				+ ", "
				+ error.description);
		}
		// validation failed with no specific error
		throw sinsp_exception(
			string("error in plugin ")
			+ name()
			+ " init config: failed parsing with provided schema");
	}
}

void sinsp_plugin::destroy_handle(sinsp_plugin_handle handle) {
#ifdef _WIN32
	FreeLibrary(handle);
#else
	dlclose(handle);
#endif
}

sinsp_source_plugin::sinsp_source_plugin(sinsp_plugin_handle handle) :
	sinsp_plugin(handle)
{
	memset(&m_source_plugin_info, 0, sizeof(m_source_plugin_info));
}

sinsp_source_plugin::~sinsp_source_plugin()
{
	close();
	destroy();
}

uint32_t sinsp_source_plugin::id()
{
	return m_id;
}

const std::string &sinsp_source_plugin::event_source()
{
	return m_event_source;
}

source_plugin_info *sinsp_source_plugin::plugin_info()
{
	return &m_source_plugin_info;
}

bool sinsp_source_plugin::open(const char *params, ss_plugin_rc &rc)
{
	ss_plugin_rc orc;

	if(!plugin_state())
	{
		return false;
	}

	m_source_plugin_info.handle = m_source_plugin_info.open(plugin_state(), params, &orc);

	rc = orc;

	return (m_source_plugin_info.handle != NULL);
}

void sinsp_source_plugin::close()
{
	if(!plugin_state() || !m_source_plugin_info.handle)
	{
		return;
	}

	m_source_plugin_info.close(plugin_state(), m_source_plugin_info.handle);
	m_source_plugin_info.handle = NULL;
}

std::string sinsp_source_plugin::get_progress(uint32_t &progress_pct)
{
	std::string ret;
	progress_pct = 0;

	if(!m_source_plugin_info.get_progress || !m_source_plugin_info.handle)
	{
		return ret;
	}

	uint32_t ppct;
	ret = str_from_alloc_charbuf(m_source_plugin_info.get_progress(plugin_state(), m_source_plugin_info.handle, &ppct));

	progress_pct = ppct;

	return ret;
}

std::string sinsp_source_plugin::event_to_string(const uint8_t *data, uint32_t datalen)
{
	std::string ret = "<NA>";

	if (!m_source_plugin_info.event_to_string)
	{
		return ret;
	}

	ret = str_from_alloc_charbuf(m_source_plugin_info.event_to_string(plugin_state(), data, datalen));

	return ret;
}

std::vector<sinsp_source_plugin::open_param> sinsp_source_plugin::list_open_params()
{
	std::vector<sinsp_source_plugin::open_param> list;
	if(plugin_state() && m_source_plugin_info.list_open_params)
	{
		ss_plugin_rc rc;
		string jsonString = str_from_alloc_charbuf(m_source_plugin_info.list_open_params(plugin_state(), &rc));
		if (rc != SS_PLUGIN_SUCCESS)
		{
			throw sinsp_exception(string("error in plugin ") + name() + ": list_open_params has error " + get_last_error());
		}

		if (jsonString.size() > 0)
		{
			Json::Value root;
			if(Json::Reader().parse(jsonString, root) == false || root.type() != Json::arrayValue)
			{
				throw sinsp_exception(string("error in plugin ") + name() + ": list_open_params returned a non-array JSON");
			}
			for(Json::Value::ArrayIndex i = 0; i < root.size(); i++)
			{
				sinsp_source_plugin::open_param param;
				param.value = root[i]["value"].asString();
				if(param.value == "")
				{
					throw sinsp_exception(string("error in plugin ") + name() + ": list_open_params has entry with no value");
				}
				param.desc = root[i]["desc"].asString();
				list.push_back(param);
			}
		}
	}

	return list;
}

void sinsp_source_plugin::set_plugin_state(ss_plugin_t *state)
{
	m_source_plugin_info.state = state;
}

ss_plugin_t *sinsp_source_plugin::plugin_state()
{
	return m_source_plugin_info.state;
}

bool sinsp_source_plugin::resolve_dylib_symbols(std::string &errstr)
{
	if (!sinsp_plugin::resolve_dylib_symbols(errstr))
	{
		return false;
	}

	// We resolve every symbol, even those that are not actually
	// used by this derived class, just to ensure that
	// m_source_plugin_info is complete. (The struct can be passed
	// down to libscap when reading/writing capture files).
	//
	// Some functions are required and return false if not found.
	if((*(void **) (&(m_source_plugin_info.get_required_api_version)) = getsym(m_handle, "plugin_get_required_api_version", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.init)) = getsym(m_handle, "plugin_init", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.destroy)) = getsym(m_handle, "plugin_destroy", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_last_error)) = getsym(m_handle, "plugin_get_last_error", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_type)) = getsym(m_handle, "plugin_get_type", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_id)) = getsym(m_handle, "plugin_get_id", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_name)) = getsym(m_handle, "plugin_get_name", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_description)) = getsym(m_handle, "plugin_get_description", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_contact)) = getsym(m_handle, "plugin_get_contact", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_version)) = getsym(m_handle, "plugin_get_version", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.get_event_source)) = getsym(m_handle, "plugin_get_event_source", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.open)) = getsym(m_handle, "plugin_open", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.close)) = getsym(m_handle, "plugin_close", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.next_batch)) = getsym(m_handle, "plugin_next_batch", errstr)) == NULL ||
	   (*(void **) (&(m_source_plugin_info.event_to_string)) = getsym(m_handle, "plugin_event_to_string", errstr)) == NULL)
	{
		return false;
	}

	// Others are not.
	(*(void **) (&m_source_plugin_info.get_fields)) = getsym(m_handle, "plugin_get_fields", errstr);
	(*(void **) (&m_source_plugin_info.get_progress)) = getsym(m_handle, "plugin_get_progress", errstr);
	(*(void **) (&m_source_plugin_info.event_to_string)) = getsym(m_handle, "plugin_event_to_string", errstr);
	(*(void **) (&m_source_plugin_info.extract_fields)) = getsym(m_handle, "plugin_extract_fields", errstr);
	(*(void **) (&m_source_plugin_info.list_open_params)) = getsym(m_handle, "plugin_list_open_params", errstr);
	(*(void **) (&m_source_plugin_info.get_init_schema)) = getsym(m_handle, "plugin_get_init_schema", errstr);

	m_id = m_source_plugin_info.get_id();
	m_event_source = str_from_alloc_charbuf(m_source_plugin_info.get_event_source());

	return true;
}

sinsp_extractor_plugin::sinsp_extractor_plugin(sinsp_plugin_handle handle) :
	sinsp_plugin(handle)
{
	memset(&m_extractor_plugin_info, 0, sizeof(m_extractor_plugin_info));
}

sinsp_extractor_plugin::~sinsp_extractor_plugin()
{
	destroy();
}

const std::set<std::string> &sinsp_extractor_plugin::extract_event_sources()
{
	return m_extract_event_sources;
}

bool sinsp_extractor_plugin::source_compatible(const std::string &source)
{
	return(m_extract_event_sources.size() == 0 ||
	       m_extract_event_sources.find(source) != m_extract_event_sources.end());
}

void sinsp_extractor_plugin::set_plugin_state(ss_plugin_t *state)
{
	m_extractor_plugin_info.state = state;
}

ss_plugin_t *sinsp_extractor_plugin::plugin_state()
{
	return m_extractor_plugin_info.state;
}

bool sinsp_extractor_plugin::resolve_dylib_symbols(std::string &errstr)
{
	if (!sinsp_plugin::resolve_dylib_symbols(errstr))
	{
		return false;
	}

	// We resolve every symbol, even those that are not actually
	// used by this derived class, just to ensure that
	// m_extractor_plugin_info is complete. (The struct can be passed
	// down to libscap when reading/writing capture files).
	//
	// Some functions are required and return false if not found.
	if((*(void **) (&(m_extractor_plugin_info.get_required_api_version)) = getsym(m_handle, "plugin_get_required_api_version", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.init)) = getsym(m_handle, "plugin_init", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.destroy)) = getsym(m_handle, "plugin_destroy", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.get_last_error)) = getsym(m_handle, "plugin_get_last_error", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.get_type)) = getsym(m_handle, "plugin_get_type", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.get_name)) = getsym(m_handle, "plugin_get_name", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.get_description)) = getsym(m_handle, "plugin_get_description", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.get_contact)) = getsym(m_handle, "plugin_get_contact", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.get_version)) = getsym(m_handle, "plugin_get_version", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.get_fields)) = getsym(m_handle, "plugin_get_fields", errstr)) == NULL ||
	   (*(void **) (&(m_extractor_plugin_info.extract_fields)) = getsym(m_handle, "plugin_extract_fields", errstr)) == NULL)
	{
		return false;
	}

	// Others are not.
	(*(void **) (&m_extractor_plugin_info.get_extract_event_sources)) = getsym(m_handle, "plugin_get_extract_event_sources", errstr);
	(*(void **) (&m_extractor_plugin_info.get_init_schema)) = getsym(m_handle, "plugin_get_init_schema", errstr);


	if (m_extractor_plugin_info.get_extract_event_sources != NULL)
	{
		std::string esources = str_from_alloc_charbuf(m_extractor_plugin_info.get_extract_event_sources());

		if (esources.length() == 0)
		{
			throw sinsp_exception(string("error in plugin ") + name() + ": get_extract_event_sources returned an empty string");
		}

		Json::Value root;
		if(Json::Reader().parse(esources, root) == false || root.type() != Json::arrayValue)
		{
			throw sinsp_exception(string("error in plugin ") + name() + ": get_extract_event_sources did not return a json array");
		}

		for(Json::Value::ArrayIndex j = 0; j < root.size(); j++)
		{
			if(! root[j].isConvertibleTo(Json::stringValue))
			{
				throw sinsp_exception(string("error in plugin ") + name() + ": get_extract_event_sources did not return a json array");
			}

			m_extract_event_sources.insert(root[j].asString());
		}
	}

	return true;
}


