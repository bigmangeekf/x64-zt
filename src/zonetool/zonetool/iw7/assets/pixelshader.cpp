#include <std_include.hpp>
#include "pixelshader.hpp"

#include <utils/cryptography.hpp>

namespace zonetool::iw7
{
	MaterialPixelShader* pixel_shader::parse(const std::string& name, zone_memory* mem)
	{
		const auto path = get_shader_path(name, pixelshader);

		filesystem::file file(path);
		file.open("rb");
		if (!file.get_fp())
		{
			return nullptr;
		}

		const auto buffer_size = file.size();
		const auto buffer = file.read_bytes(buffer_size);

		auto* asset = mem->allocate<MaterialPixelShader>();
		asset->name = mem->duplicate_string(name);
		asset->prog.loadDef.programSize = static_cast<unsigned int>(buffer_size);
		asset->prog.loadDef.program = mem->allocate<unsigned char>(buffer_size);

		std::memcpy(asset->prog.loadDef.program, buffer.data(), buffer_size);

		asset->prog.loadDef.microCodeCrc = utils::cryptography::crc32::compute(asset->prog.loadDef.program, asset->prog.loadDef.programSize);

		file.close();

		asset->debugName = shader::parse_debug_name(asset->name, mem);

		return asset;
	}

	void pixel_shader::init(const std::string& name, zone_memory* mem)
	{
		this->name_ = name;

		if (this->referenced())
		{
			this->asset_ = mem->allocate<typename std::remove_reference<decltype(*this->asset_)>::type>();
			this->asset_->name = mem->duplicate_string(name);
			return;
		}

		this->asset_ = this->parse(name, mem);
		if (!this->asset_)
		{
			ZONETOOL_FATAL("pixelshader \"%s\" not found.", name.data());
		}
	}

	void pixel_shader::prepare(zone_buffer* buf, zone_memory* mem)
	{
	}

	void pixel_shader::load_depending(zone_base* zone)
	{
	}

	std::string pixel_shader::name()
	{
		return this->name_;
	}

	std::int32_t pixel_shader::type()
	{
		return ASSET_TYPE_PIXELSHADER;
	}

	void pixel_shader::write(zone_base* zone, zone_buffer* buf)
	{
		auto data = this->asset_;
		auto dest = buf->write(data);

		buf->push_stream(XFILE_BLOCK_VIRTUAL);
		dest->name = buf->write_str(this->name());

		if (data->debugName)
		{
			dest->debugName = buf->write_str(data->debugName);
		}

		buf->push_stream(XFILE_BLOCK_TEMP);
		if (data->prog.loadDef.program)
		{
			buf->align(3);
			buf->write(data->prog.loadDef.program, data->prog.loadDef.programSize);
			buf->clear_pointer(&dest->prog.loadDef.program);
		}
		buf->pop_stream();

		buf->pop_stream();
	}

	void pixel_shader::dump(MaterialPixelShader* asset)
	{
		if (filesystem::get_fastfile().starts_with("paris_enemy_"))
		{
			if (!asset || !asset->name) throw std::runtime_error("IW7 enemy pixel shader has no name");
			const auto name = std::string(asset->name);
			if (name.empty() || name.size() > 240 || name == "." || name == ".." ||
				!std::all_of(name.begin(), name.end(), [](unsigned char c)
					{ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
						(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; }))
				throw std::runtime_error("Unsafe IW7 enemy pixel shader name");
			const auto source_path = std::filesystem::path(filesystem::get_dump_path()) / get_shader_path(name, pixelshader);
			const auto size = asset->prog.loadDef.programSize;
			const auto* program = asset->prog.loadDef.program;
			const bool empty_null = name == "null.hlsl" && size == 0 && !program;
			const bool valid = program && size >= 32 && std::memcmp(program, "DXBC", 4) == 0;
			if (!empty_null && !valid)
			{
				if (std::filesystem::exists(source_path))
				{
					const auto existing = utils::io::read_file(source_path.string());
					if (existing.size() >= 32 && existing.compare(0, 4, "DXBC") == 0) return;
				}
				const auto bootstrap_path = std::filesystem::path("dump") / "paris_enemy_shader_bootstrap" / "techsets" / "ps" / (name + ".cso");
				if (std::filesystem::exists(bootstrap_path))
				{
					const auto bootstrap = utils::io::read_file(bootstrap_path.string());
					if (bootstrap.size() >= 32 && bootstrap.compare(0, 4, "DXBC") == 0)
					{
						std::filesystem::create_directories(source_path.parent_path());
						std::filesystem::copy_file(bootstrap_path, source_path, std::filesystem::copy_options::overwrite_existing);
						return;
					}
				}
				throw std::runtime_error("IW7 enemy pixel shader is empty or invalid DXBC: " + name);
			}
		}
		const auto path = get_shader_path(asset->name, pixelshader);

		filesystem::file file(path);
		file.open("wb");
		file.write(asset->prog.loadDef.program, asset->prog.loadDef.programSize);
		file.close();

		if (asset->debugName)
		{
			shader::dump_debug_name(asset->name, asset->debugName);
		}
	}
}
