#pragma once

#include "registry.hpp"
#include "d3d12/d3d12_enums.hpp"
#include "util/named_type.hpp"

namespace wr
{
	struct Shader
	{

	};


	struct ShaderDescription
	{
		using Path  = util::NamedType<std::string>;
		using Entry = util::NamedType<std::string>;
		using Type  = util::NamedType<ShaderType>;

		Path path;
		Entry entry;
		Type type;
	};

	class ShaderRegistry : public internal::Registry<ShaderRegistry, Shader, ShaderDescription>
	{
	public:
		ShaderRegistry();
		virtual ~ShaderRegistry() = default;

		RegistryHandle Register(ShaderDescription description);
	};

} /* wr */