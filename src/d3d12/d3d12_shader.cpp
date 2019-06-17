// Copyright 2019 Breda University of Applied Sciences and Team Wisp (Viktor Zoutman, Emilio Laiso, Jens Hagen, Meine Zeinstra, Tahar Meijs, Koen Buitenhuis, Niels Brunekreef, Darius Bouma, Florian Schut)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "d3d12_functions.hpp"

#include <D3Dcompiler.h>
#include <dxcapi.h>

#include "../util/log.hpp"
#include "d3d12_defines.hpp"
#include <sstream>

namespace wr::d3d12
{

	namespace internal
	{

		std::string ShaderTypeToString(ShaderType type)
		{
			std::string prefix = "unknown";

			switch (type)
			{
			case ShaderType::VERTEX_SHADER:
				prefix = "vs_";
				break;
			case ShaderType::PIXEL_SHADER:
				prefix = "ps_";
				break;
			case ShaderType::DOMAIN_SHADER:
				prefix = "ds_";
				break;
			case ShaderType::GEOMETRY_SHADER:
				prefix = "gs_";
				break;
			case ShaderType::HULL_SHADER:
				prefix = "hs_";
				break;
			case ShaderType::DIRECT_COMPUTE_SHADER:
				prefix = "cs_";
				break;
			case ShaderType::LIBRARY_SHADER:
				prefix = "lib_";
				break;
			}

			return prefix + std::string(d3d12::settings::default_shader_model);
		}

	} /* internal */

	std::variant<Shader*, std::string> LoadShader(Device* device, ShaderType type, std::string const & path, std::string const & entry, std::vector<std::pair<std::wstring, std::wstring>> user_defines)
	{
		auto shader = new Shader();

		shader->m_entry = entry;
		shader->m_path = path;
		shader->m_type = type;
		shader->m_defines = user_defines;

		std::wstring wpath(path.begin(), path.end());
		std::wstring wentry(entry.begin(), entry.end());
		std::string temp_shader_type(internal::ShaderTypeToString(type));
		std::wstring wshader_type(temp_shader_type.begin(), temp_shader_type.end());

		IDxcLibrary* library = nullptr;
		DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void **)&library);

		IDxcBlobEncoding* source;
		std::uint32_t code_page = CP_ACP;
		library->CreateBlobFromFile(wpath.c_str(), &code_page, &source);

		IDxcIncludeHandler* include_handler;
		TRY_M(library->CreateIncludeHandler(&include_handler), "Failed to create default include handler.");

		std::vector<DxcDefine> defines;
		if (GetRaytracingType(device) == RaytracingType::FALLBACK)
		{
			defines.push_back({L"FALLBACK", L"1"});
		}

		for (auto& define : user_defines)
		{
			defines.push_back({ define.first.c_str(), define.second.c_str() });
		}

		IDxcOperationResult* result;
		HRESULT hr = Device::m_compiler->Compile(
			source,          // program text
			wpath.c_str(),   // file name, mostly for error messages
			wentry.c_str(),         // entry point function
			wshader_type.c_str(),   // target profile
#ifdef _DEBUG
			d3d12::settings::debug_shader_args.data(), static_cast<uint32_t>(d3d12::settings::debug_shader_args.size()),
#else
			d3d12::settings::release_shader_args.data(), static_cast<uint32_t>(d3d12::settings::release_shader_args.size()),
#endif
			defines.data(), static_cast<std::uint32_t>(defines.size()),       // name/value defines and their count
			include_handler,          // handler for #include directives
			&result);

		if (FAILED(hr))
		{
			std::string error_msg = "Failed to compile " + path + " (entry: " + entry + "), Incorrect entry or path?";
			return error_msg;
		}

		// compiler output
		result->GetStatus(&hr);
		if (FAILED(hr))
		{
			delete shader;

			IDxcBlobEncoding* error;
			result->GetErrorBuffer(&error);
			return std::string((char*)error->GetBufferPointer());
		}

		result->GetResult(&shader->m_native);

		return shader;
	}

	void Destroy(Shader* shader)
	{
		SAFE_RELEASE(shader->m_native);
		delete shader;
	}

} /* wr::d3d12 */
