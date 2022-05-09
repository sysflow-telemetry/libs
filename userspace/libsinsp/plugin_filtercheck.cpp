/*
Copyright (C) 2022 The Falco Authors.

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

using namespace std;

#include "plugin_filtercheck.h"
#include "plugin_manager.h"

sinsp_filter_check_plugin::sinsp_filter_check_plugin()
{
	m_info.m_name = "plugin";
	m_info.m_fields = NULL;
	m_info.m_nfields = 0;
	m_info.m_flags = filter_check_info::FL_NONE;
	m_eplugin = nullptr;
	m_compatible_sources = NULL;
}

sinsp_filter_check_plugin::sinsp_filter_check_plugin(std::shared_ptr<sinsp_plugin> plugin)
{
	m_info.m_name = plugin->name() + string(" (plugin)");
	if (!(plugin->caps() & CAP_EXTRACTION))
	{
		throw sinsp_exception("Creating a sinsp_filter_check_plugin with a non extraction-capable plugin.");
	}

	m_eplugin = plugin;
	m_info.m_fields = &m_eplugin->fields()[0]; // we use a vector so this should be safe
	m_info.m_nfields = m_eplugin->fields().size();
	m_info.m_flags = filter_check_info::FL_NONE;
	m_compatible_sources = NULL;
}

sinsp_filter_check_plugin::sinsp_filter_check_plugin(const sinsp_filter_check_plugin &p)
{
	m_eplugin = p.m_eplugin;
	m_compatible_sources = NULL;
	if (p.m_compatible_sources)
	{
		m_compatible_sources = new std::set<size_t>(*p.m_compatible_sources);
	}
	m_info = p.m_info;
}

sinsp_filter_check_plugin::~sinsp_filter_check_plugin()
{
	if (m_compatible_sources)
	{
		delete m_compatible_sources;
	}
}

int32_t sinsp_filter_check_plugin::parse_field_name(const char* str, bool alloc_state, bool needed_for_filtering)
{
	int32_t res = sinsp_filter_check::parse_field_name(str, alloc_state, needed_for_filtering);

	m_argstr.clear();

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
						extract_arg_index(str);
					}

					if(m_info.m_fields[m_field_id].m_flags & filtercheck_field_flags::EPF_ARG_KEY)
					{
						extract_arg_key();
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

sinsp_filter_check* sinsp_filter_check_plugin::allocate_new()
{
	return new sinsp_filter_check_plugin(*this);
}

bool sinsp_filter_check_plugin::extract(sinsp_evt *evt, OUT vector<extract_value_t>& values, bool sanitize_strings)
{
	//
	// Reject any event that is not generated by a plugin
	//
	if(evt->get_type() != PPME_PLUGINEVENT_E)
	{
		return false;
	}

	sinsp_evt_param *parinfo;

	// Check that the current plugin is source compatible with the event source plugin
	parinfo = evt->get_param(0);
	ASSERT(parinfo->m_len == sizeof(int32_t));
	uint32_t pgid = *(int32_t *) parinfo->m_val;

	// We know that plugin has source capabilities because it has an id and is sending events
	bool pfound = false;
	auto psource = m_inspector->get_plugin_manager()->source_idx_by_plugin_id(pgid, pfound);
	if (!pfound)
	{
		return false;
	}
	if (!m_compatible_sources)
	{
		m_compatible_sources = new std::set<size_t>();
		auto sources = m_inspector->get_plugin_manager()->sources();
		for (auto &src : m_eplugin->extract_event_sources())
		{
			auto it = std::find(sources.begin(), sources.end(), src);
			if (it != sources.end())
			{
				m_compatible_sources->insert(std::distance(sources.begin(), it));
			}
		}
	}
	if (!m_compatible_sources->empty()
		&& m_compatible_sources->find(psource) == m_compatible_sources->end())
	{
		return false;
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
	if (!m_eplugin->extract_fields(pevt, num_fields, &efield) || efield.res_len == 0)
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

void sinsp_filter_check_plugin::extract_arg_index(const char* full_field_name)
{
	int length = m_argstr.length();
	bool is_valid = true;
	std::string message = "";
	
	// Please note that numbers starting with `0` (`01`, `02`, `0003`, ...) are not indexes. 
	if(length == 0 || (length > 1 && m_argstr[0] == '0'))
	{
		is_valid = false;
		message = " has an invalid index argument starting with 0: ";
	}
	
	// The index must be composed only by digits (0-9).
	for(int j = 0; j < length; j++)
	{
		if(!isdigit(m_argstr[j]))
		{
			is_valid = false;
			message = " has an invalid index argument not composed only by digits: ";
			break;
		}
	}

	// If the argument is valid we can convert it with `stoul`.
	// Please note that `stoul` alone is not enough, since it also consider as valid 
	// strings like "0123 i'm a number", converting them into '0123'. This is why in the 
	// previous step we check that every character is a digit.
	if(is_valid)
	{
		try
		{
			m_arg_index = std::stoul(m_argstr);
			return;
		} 
		catch(...)
		{
			message = " has an invalid index argument not representable on 64 bit: ";
		}
	}
	throw sinsp_exception(string("filter ") + string(full_field_name) + string(" ")
									+ m_field->m_name + message + m_argstr);
}

// extract_arg_key() extracts a valid string from the argument. If we pass
// a numeric argument, it will be converted to string. 
void sinsp_filter_check_plugin::extract_arg_key()
{
	m_arg_key = (char*)m_argstr.c_str();
}
