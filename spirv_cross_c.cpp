/*
 * Copyright 2019 Hans-Kristian Arntzen
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "spirv_cross_c.h"
#include "spirv_glsl.hpp"
#include "spirv_msl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_cpp.hpp"
#include "spirv_reflect.hpp"
#include "spirv_parser.hpp"
#include <new>
#include <memory>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
#define SPVC_BEGIN_SAFE_SCOPE try
#else
#define SPVC_BEGIN_SAFE_SCOPE
#endif

#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
#define SPVC_END_SAFE_SCOPE(context, error) catch (const std::exception &e) { (context)->last_error = e.what(); return (error); }
#else
#define SPVC_END_SAFE_SCOPE(context, error)
#endif

using namespace std;
using namespace spirv_cross;

struct ScratchMemoryAllocation
{
	virtual ~ScratchMemoryAllocation() = default;
};

struct StringAllocation : ScratchMemoryAllocation
{
	explicit StringAllocation(const char *name)
		: str(name)
	{
	}

	explicit StringAllocation(std::string name)
		: str(std::move(name))
	{
	}

	std::string str;
};

template<typename T>
struct TemporaryBuffer : ScratchMemoryAllocation
{
	std::vector<T> buffer;
};

struct spvc_context_s
{
	string last_error;
	vector<unique_ptr<ScratchMemoryAllocation>> allocations;
	const char *allocate_name(const std::string &name);
};

const char *spvc_context_s::allocate_name(const std::string &name)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto alloc = unique_ptr<StringAllocation>(new StringAllocation(name));
		auto *ret = alloc->str.c_str();
		allocations.emplace_back(std::move(alloc));
		return ret;
	}
	SPVC_END_SAFE_SCOPE(this, nullptr)
}

struct spvc_parsed_ir_s : ScratchMemoryAllocation
{
	spvc_context context = nullptr;
	ParsedIR parsed;
};

struct spvc_compiler_s : ScratchMemoryAllocation
{
	spvc_context context = nullptr;
	unique_ptr<Compiler> compiler;
	spvc_backend backend = SPVC_BACKEND_NONE;
};

struct spvc_compiler_options_s : ScratchMemoryAllocation
{
	spvc_context context = nullptr;
	uint32_t backend_flags = 0;
	CompilerGLSL::Options glsl;
	CompilerMSL::Options msl;
	CompilerHLSL::Options hlsl;
};

// Dummy-inherit to we can keep our opaque type handle type safe in C-land as well,
// and avoid just throwing void * around.
struct spvc_type_s : SPIRType
{
};

struct spvc_constant_s : SPIRConstant
{
};

struct spvc_resources_s : ScratchMemoryAllocation
{
	spvc_context context = nullptr;
	std::vector<spvc_reflected_resource> uniform_buffers;
	std::vector<spvc_reflected_resource> storage_buffers;
	std::vector<spvc_reflected_resource> stage_inputs;
	std::vector<spvc_reflected_resource> stage_outputs;
	std::vector<spvc_reflected_resource> subpass_inputs;
	std::vector<spvc_reflected_resource> storage_images;
	std::vector<spvc_reflected_resource> sampled_images;
	std::vector<spvc_reflected_resource> atomic_counters;
	std::vector<spvc_reflected_resource> push_constant_buffers;
	std::vector<spvc_reflected_resource> separate_images;
	std::vector<spvc_reflected_resource> separate_samplers;

	bool copy_resources(std::vector<spvc_reflected_resource> &outputs, const std::vector<Resource> &inputs);
	bool copy_resources(const ShaderResources &resources);
};

spvc_result spvc_create_context(spvc_context *context)
{
	auto *ctx = new(std::nothrow) spvc_context_s;
	if (!ctx)
		return SPVC_ERROR_OUT_OF_MEMORY;

	*context = ctx;
	return SPVC_SUCCESS;
}

void spvc_context_release_temporary_allocations(spvc_context context)
{
	context->allocations.clear();
}

const char *spvc_get_last_error_string(spvc_context context)
{
	return context->last_error.c_str();
}

spvc_result spvc_parse_spirv(spvc_context context, const SpvId *spirv, size_t word_count,
                            spvc_parsed_ir *parsed_ir)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		std::unique_ptr<spvc_parsed_ir_s> pir(new(std::nothrow) spvc_parsed_ir_s);
		if (!pir)
			return SPVC_ERROR_OUT_OF_MEMORY;

		pir->context = context;
		Parser parser(spirv, word_count);
		parser.parse();
		pir->parsed = move(parser.get_parsed_ir());
		*parsed_ir = pir.get();
		context->allocations.push_back(std::move(pir));
	}
	SPVC_END_SAFE_SCOPE(context, SPVC_ERROR_INVALID_SPIRV)
	return SPVC_SUCCESS;
}

spvc_result spvc_create_compiler(spvc_context context, spvc_backend backend,
                                spvc_parsed_ir parsed_ir, spvc_capture_mode mode,
                                spvc_compiler *compiler)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		std::unique_ptr<spvc_compiler_s> comp(new(std::nothrow) spvc_compiler_s);
		if (!comp)
			return SPVC_ERROR_OUT_OF_MEMORY;
		comp->backend = backend;
		comp->context = context;

		switch (backend)
		{
		case SPVC_BACKEND_NONE:
			if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
				comp->compiler.reset(new Compiler(move(parsed_ir->parsed)));
			else if (mode == SPVC_CAPTURE_MODE_COPY)
				comp->compiler.reset(new Compiler(parsed_ir->parsed));
			else
				return SPVC_ERROR_INVALID_ARGUMENT;
			break;

		case SPVC_BACKEND_GLSL:
			if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
				comp->compiler.reset(new CompilerGLSL(move(parsed_ir->parsed)));
			else if (mode == SPVC_CAPTURE_MODE_COPY)
				comp->compiler.reset(new CompilerGLSL(parsed_ir->parsed));
			else
				return SPVC_ERROR_INVALID_ARGUMENT;
			break;

		case SPVC_BACKEND_HLSL:
			if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
				comp->compiler.reset(new CompilerHLSL(move(parsed_ir->parsed)));
			else if (mode == SPVC_CAPTURE_MODE_COPY)
				comp->compiler.reset(new CompilerHLSL(parsed_ir->parsed));
			else
				return SPVC_ERROR_INVALID_ARGUMENT;
			break;

		case SPVC_BACKEND_MSL:
			if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
				comp->compiler.reset(new CompilerMSL(move(parsed_ir->parsed)));
			else if (mode == SPVC_CAPTURE_MODE_COPY)
				comp->compiler.reset(new CompilerMSL(parsed_ir->parsed));
			else
				return SPVC_ERROR_INVALID_ARGUMENT;
			break;

		case SPVC_BACKEND_CPP:
			if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
				comp->compiler.reset(new CompilerCPP(move(parsed_ir->parsed)));
			else if (mode == SPVC_CAPTURE_MODE_COPY)
				comp->compiler.reset(new CompilerCPP(parsed_ir->parsed));
			else
				return SPVC_ERROR_INVALID_ARGUMENT;
			break;

		case SPVC_BACKEND_JSON:
			if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
				comp->compiler.reset(new CompilerReflection(move(parsed_ir->parsed)));
			else if (mode == SPVC_CAPTURE_MODE_COPY)
				comp->compiler.reset(new CompilerReflection(parsed_ir->parsed));
			else
				return SPVC_ERROR_INVALID_ARGUMENT;
			break;

		default:
			return SPVC_ERROR_INVALID_ARGUMENT;
		}

		*compiler = comp.get();
		context->allocations.push_back(std::move(comp));
	}
	SPVC_END_SAFE_SCOPE(context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

spvc_result spvc_create_compiler_options(spvc_compiler compiler, spvc_compiler_options *options)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		std::unique_ptr<spvc_compiler_options_s> opt(new(std::nothrow) spvc_compiler_options_s);
		if (!opt)
			return SPVC_ERROR_OUT_OF_MEMORY;

		opt->context = compiler->context;
		opt->backend_flags = 0;
		switch (compiler->backend)
		{
		case SPVC_BACKEND_MSL:
			opt->backend_flags |= SPVC_COMPILER_OPTION_MSL_BIT | SPVC_COMPILER_OPTION_COMMON_BIT;
			break;

		case SPVC_BACKEND_HLSL:
			opt->backend_flags |= SPVC_COMPILER_OPTION_HLSL_BIT | SPVC_COMPILER_OPTION_COMMON_BIT;
			break;

		case SPVC_BACKEND_GLSL:
			opt->backend_flags |= SPVC_COMPILER_OPTION_GLSL_BIT | SPVC_COMPILER_OPTION_COMMON_BIT;
			break;

		default:
			break;
		}

		*options = opt.get();
		compiler->context->allocations.push_back(std::move(opt));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

spvc_result spvc_set_compiler_option_bool(spvc_compiler_options options,
                                         spvc_compiler_option option, spvc_bool value)
{
	return spvc_set_compiler_option_uint(options, option, value ? 1 : 0);
}

spvc_result spvc_set_compiler_option_uint(spvc_compiler_options options,
                                         spvc_compiler_option option, unsigned value)
{
	uint32_t supported_mask = options->backend_flags;
	uint32_t required_mask = option & SPVC_COMPILER_OPTION_LANG_BITS;
	if ((required_mask | supported_mask) != supported_mask)
		return SPVC_ERROR_INVALID_ARGUMENT;

	switch (option)
	{
	case SPVC_COMPILER_OPTION_FORCE_TEMPORARY:
		options->glsl.force_temporary = value != 0;
		break;
	case SPVC_COMPILER_OPTION_FLATTEN_MULTIDIMENSIONAL_ARRAYS:
		options->glsl.flatten_multidimensional_arrays = value != 0;
		break;
	case SPVC_COMPILER_OPTION_FIXUP_DEPTH_CONVENTION:
		options->glsl.vertex.fixup_clipspace = value != 0;
		break;
	case SPVC_COMPILER_OPTION_FLIP_VERTEX_Y:
		options->glsl.vertex.flip_vert_y = value != 0;
		break;
	case SPVC_COMPILER_OPTION_GLSL_SUPPORT_NONZERO_BASE_INSTANCE:
		options->glsl.vertex.support_nonzero_base_instance = value != 0;
		break;
	case SPVC_COMPILER_OPTION_GLSL_SEPARATE_SHADER_OBJECTS:
		options->glsl.separate_shader_objects = value != 0;
		break;
	case SPVC_COMPILER_OPTION_GLSL_ENABLE_420PACK_EXTENSION:
		options->glsl.enable_420pack_extension = value != 0;
		break;
	case SPVC_COMPILER_OPTION_GLSL_VERSION:
		options->glsl.version = value;
		break;
	case SPVC_COMPILER_OPTION_GLSL_ES:
		options->glsl.es = value != 0;
		break;
	case SPVC_COMPILER_OPTION_GLSL_VULKAN_SEMANTICS:
		options->glsl.vulkan_semantics = value != 0;
		break;
	case SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_FLOAT_PRECISION_HIGHP:
		options->glsl.fragment.default_float_precision =
				value != 0 ?
				CompilerGLSL::Options::Precision::Highp :
				CompilerGLSL::Options::Precision::Mediump;
		break;
	case SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_INT_PRECISION_HIGHP:
		options->glsl.fragment.default_int_precision =
				value != 0 ?
				CompilerGLSL::Options::Precision::Highp :
				CompilerGLSL::Options::Precision::Mediump;
		break;

	case SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL:
		options->hlsl.shader_model = value;
		break;

	case SPVC_COMPILER_OPTION_HLSL_POINT_SIZE_COMPAT:
		options->hlsl.point_size_compat = value != 0;
		break;

	case SPVC_COMPILER_OPTION_HLSL_POINT_COORD_COMPAT:
		options->hlsl.point_coord_compat = value != 0;
		break;

	case SPVC_COMPILER_OPTION_HLSL_SUPPORT_NONZERO_BASE_VERTEX_BASE_INSTANCE:
		options->hlsl.support_nonzero_base_vertex_base_instance = value != 0;
		break;

	case SPVC_COMPILER_OPTION_MSL_VERSION:
		options->msl.msl_version = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_TEXEL_BUFFER_TEXTURE_WIDTH:
		options->msl.texel_buffer_texture_width = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_AUX_BUFFER_INDEX:
		options->msl.aux_buffer_index = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_INDIRECT_PARAMS_BUFFER_INDEX:
		options->msl.indirect_params_buffer_index = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_SHADER_OUTPUT_BUFFER_INDEX:
		options->msl.shader_output_buffer_index = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_SHADER_PATCH_OUTPUT_BUFFER_INDEX:
		options->msl.shader_patch_output_buffer_index = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_SHADER_TESS_FACTOR_OUTPUT_BUFFER_INDEX:
		options->msl.shader_tess_factor_buffer_index = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_SHADER_INPUT_WORKGROUP_INDEX:
		options->msl.shader_input_wg_index = value;
		break;

	case SPVC_COMPILER_OPTION_MSL_ENABLE_POINT_SIZE_BUILTIN:
		options->msl.enable_point_size_builtin = value != 0;
		break;

	case SPVC_COMPILER_OPTION_MSL_DISABLE_RASTERIZATION:
		options->msl.disable_rasterization = value != 0;
		break;

	case SPVC_COMPILER_OPTION_MSL_CAPTURE_OUTPUT_TO_BUFFER:
		options->msl.capture_output_to_buffer = value != 0;
		break;

	case SPVC_COMPILER_OPTION_MSL_SWIZZLE_TEXTURE_SAMPLES:
		options->msl.swizzle_texture_samples = value != 0;
		break;

	case SPVC_COMPILER_OPTION_MSL_PAD_FRAGMENT_OUTPUT_COMPONENTS:
		options->msl.pad_fragment_output_components = value != 0;
		break;

	case SPVC_COMPILER_OPTION_MSL_PLATFORM:
		options->msl.platform = static_cast<CompilerMSL::Options::Platform>(value);
		break;

	default:
		return SPVC_ERROR_INVALID_ARGUMENT;
	}

	return SPVC_SUCCESS;
}

spvc_result spvc_install_compiler_options(spvc_compiler compiler, spvc_compiler_options options)
{
	switch (compiler->backend)
	{
	case SPVC_BACKEND_GLSL:
		static_cast<CompilerGLSL &>(*compiler->compiler).set_common_options(options->glsl);
		break;
	case SPVC_BACKEND_HLSL:
		static_cast<CompilerHLSL &>(*compiler->compiler).set_common_options(options->glsl);
		static_cast<CompilerHLSL &>(*compiler->compiler).set_hlsl_options(options->hlsl);
		break;
	case SPVC_BACKEND_MSL:
		static_cast<CompilerMSL &>(*compiler->compiler).set_common_options(options->glsl);
		static_cast<CompilerMSL &>(*compiler->compiler).set_msl_options(options->msl);
		break;
	default:
		break;
	}

	return SPVC_SUCCESS;
}

spvc_result spvc_compile(spvc_compiler compiler, const char **source)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto result = compiler->compiler->compile();
		if (result.empty())
		{
			compiler->context->last_error = "Unsupported SPIR-V.";
			return SPVC_ERROR_UNSUPPORTED_SPIRV;
		}

		*source = compiler->context->allocate_name(result);
		if (!*source)
			return SPVC_ERROR_OUT_OF_MEMORY;
		return SPVC_SUCCESS;
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_UNSUPPORTED_SPIRV)
}

bool spvc_resources_s::copy_resources(std::vector<spvc_reflected_resource> &outputs,
                                      const std::vector<Resource> &inputs)
{
	for (auto &i : inputs)
	{
		spvc_reflected_resource r;
		r.base_type_id = i.base_type_id;
		r.type_id = i.type_id;
		r.id = i.id;
		r.name = context->allocate_name(i.name);
		if (!r.name)
			return false;

		outputs.push_back(r);
	}

	return true;
}

bool spvc_resources_s::copy_resources(const ShaderResources &resources)
{
	if (!copy_resources(uniform_buffers, resources.uniform_buffers))
		return false;
	if (!copy_resources(storage_buffers, resources.storage_buffers))
		return false;
	if (!copy_resources(stage_inputs, resources.stage_inputs))
		return false;
	if (!copy_resources(stage_outputs, resources.stage_outputs))
		return false;
	if (!copy_resources(subpass_inputs, resources.subpass_inputs))
		return false;
	if (!copy_resources(storage_images, resources.storage_images))
		return false;
	if (!copy_resources(sampled_images, resources.sampled_images))
		return false;
	if (!copy_resources(atomic_counters, resources.atomic_counters))
		return false;
	if (!copy_resources(push_constant_buffers, resources.push_constant_buffers))
		return false;
	if (!copy_resources(separate_images, resources.separate_images))
		return false;
	if (!copy_resources(separate_samplers, resources.separate_samplers))
		return false;

	return true;
}

spvc_result spvc_create_statically_accessed_shader_resources(spvc_compiler compiler, spvc_resources *resources)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		std::unique_ptr<spvc_resources_s> res(new(std::nothrow) spvc_resources_s);
		if (!res)
		{
			compiler->context->last_error = "Out of memory.";
			return SPVC_ERROR_OUT_OF_MEMORY;
		}

		res->context = compiler->context;
		auto active = compiler->compiler->get_active_interface_variables();
		auto accessed_resources = compiler->compiler->get_shader_resources(active);

		if (!res->copy_resources(accessed_resources))
		{
			res->context->last_error = "Out of memory.";
			return SPVC_ERROR_OUT_OF_MEMORY;
		}
		*resources = res.get();
		compiler->context->allocations.push_back(std::move(res));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

spvc_result spvc_create_shader_resources(spvc_compiler compiler, spvc_resources *resources)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		std::unique_ptr<spvc_resources_s> res(new(std::nothrow) spvc_resources_s);
		if (!res)
		{
			compiler->context->last_error = "Out of memory.";
			return SPVC_ERROR_OUT_OF_MEMORY;
		}

		res->context = compiler->context;
		auto accessed_resources = compiler->compiler->get_shader_resources();

		if (!res->copy_resources(accessed_resources))
		{
			res->context->last_error = "Out of memory.";
			return SPVC_ERROR_OUT_OF_MEMORY;
		}

		*resources = res.get();
		compiler->context->allocations.push_back(std::move(res));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

spvc_result spvc_get_resource_list(spvc_resources resources, spvc_resource_type type,
                                  const struct spvc_reflected_resource **resource_list,
                                  size_t *resource_size)
{
	const std::vector<spvc_reflected_resource> *list = nullptr;
	switch (type)
	{
	case SPVC_RESOURCE_TYPE_UNIFORM_BUFFER:
		list = &resources->uniform_buffers;
		break;

	case SPVC_RESOURCE_TYPE_STORAGE_BUFFER:
		list = &resources->storage_buffers;
		break;

	case SPVC_RESOURCE_TYPE_STAGE_INPUT:
		list = &resources->stage_inputs;
		break;

	case SPVC_RESOURCE_TYPE_STAGE_OUTPUT:
		list = &resources->stage_outputs;
		break;

	case SPVC_RESOURCE_TYPE_SUBPASS_INPUT:
		list = &resources->subpass_inputs;
		break;

	case SPVC_RESOURCE_TYPE_STORAGE_IMAGE:
		list = &resources->storage_images;
		break;

	case SPVC_RESOURCE_TYPE_SAMPLED_IMAGE:
		list = &resources->sampled_images;
		break;

	case SPVC_RESOURCE_TYPE_ATOMIC_COUNTER:
		list = &resources->atomic_counters;
		break;

	case SPVC_RESOURCE_TYPE_PUSH_CONSTANT:
		list = &resources->push_constant_buffers;
		break;

	case SPVC_RESOURCE_TYPE_SEPARATE_IMAGE:
		list = &resources->separate_images;
		break;

	case SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS:
		list = &resources->separate_samplers;
		break;

	default:
		break;
	}

	if (!list)
	{
		resources->context->last_error = "Invalid argument.";
		return SPVC_ERROR_INVALID_ARGUMENT;
	}

	*resource_size = list->size();
	*resource_list = list->data();
	return SPVC_SUCCESS;
}

void spvc_set_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration, unsigned argument)
{
	compiler->compiler->set_decoration(id, static_cast<spv::Decoration>(decoration), argument);
}

void spvc_set_decoration_string(spvc_compiler compiler, SpvId id, SpvDecoration decoration, const char *argument)
{
	compiler->compiler->set_decoration_string(id, static_cast<spv::Decoration>(decoration), argument);
}

void spvc_set_name(spvc_compiler compiler, SpvId id, const char *argument)
{
	compiler->compiler->set_name(id, argument);
}

void spvc_set_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration, unsigned argument)
{
	compiler->compiler->set_member_decoration(id, member_index, static_cast<spv::Decoration>(decoration), argument);
}

void spvc_set_member_decoration_string(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration, const char *argument)
{
	compiler->compiler->set_member_decoration_string(id, member_index, static_cast<spv::Decoration>(decoration), argument);
}

void spvc_set_member_name(spvc_compiler compiler, spvc_type_id id, unsigned member_index, const char *argument)
{
	compiler->compiler->set_member_name(id, member_index, argument);
}

void spvc_unset_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration)
{
	compiler->compiler->unset_decoration(id, static_cast<spv::Decoration>(decoration));
}

void spvc_unset_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration)
{
	compiler->compiler->unset_member_decoration(id, member_index, static_cast<spv::Decoration>(decoration));
}

spvc_bool spvc_has_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration)
{
	return compiler->compiler->has_decoration(id, static_cast<spv::Decoration>(decoration));
}

spvc_bool spvc_has_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration)
{
	return compiler->compiler->has_member_decoration(id, member_index, static_cast<spv::Decoration>(decoration));
}

const char *spvc_get_name(spvc_compiler compiler, SpvId id)
{
	return compiler->compiler->get_name(id).c_str();
}

unsigned spvc_get_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration)
{
	return compiler->compiler->get_decoration(id, static_cast<spv::Decoration>(decoration));
}

const char *spvc_get_decoration_string(spvc_compiler compiler, SpvId id, SpvDecoration decoration)
{
	return compiler->compiler->get_decoration_string(id, static_cast<spv::Decoration>(decoration)).c_str();
}

unsigned spvc_get_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration)
{
	return compiler->compiler->get_member_decoration(id, member_index, static_cast<spv::Decoration>(decoration));
}

const char *spvc_get_member_decoration_string(spvc_compiler compiler, spvc_type_id id,
                                              unsigned member_index, SpvDecoration decoration)
{
	return compiler->compiler->get_member_decoration_string(id, member_index, static_cast<spv::Decoration>(decoration)).c_str();
}

spvc_result spvc_get_entry_points(spvc_compiler compiler, const struct spvc_entry_point **entry_points, size_t *num_entry_points)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto entries = compiler->compiler->get_entry_points_and_stages();
		std::vector<spvc_entry_point> translated;
		translated.reserve(entries.size());

		for (auto &entry : entries)
		{
			spvc_entry_point new_entry;
			new_entry.execution_model = static_cast<SpvExecutionModel>(entry.execution_model);
			new_entry.name = compiler->context->allocate_name(entry.name);
			if (!new_entry.name)
			{
				compiler->context->last_error = "Out of memory.";
				return SPVC_ERROR_OUT_OF_MEMORY;
			}
			translated.push_back(new_entry);
		}

		auto ptr = std::unique_ptr<TemporaryBuffer<spvc_entry_point>>(new TemporaryBuffer<spvc_entry_point>());
		ptr->buffer = std::move(translated);
		*entry_points = ptr->buffer.data();
		*num_entry_points = ptr->buffer.size();
		compiler->context->allocations.push_back(std::move(ptr));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

spvc_result spvc_set_entry_point(spvc_compiler compiler, const char *name, SpvExecutionModel model)
{
	compiler->compiler->set_entry_point(name, static_cast<spv::ExecutionModel>(model));
	return SPVC_SUCCESS;
}

spvc_type spvc_get_type_handle(spvc_compiler compiler, spvc_type_id id)
{
	// Should only throw if an intentionally garbage ID is passed, but the IDs are not type-safe.
	SPVC_BEGIN_SAFE_SCOPE
	{
		return static_cast<spvc_type>(&compiler->compiler->get_type(id));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, nullptr)
}

static spvc_basetype convert_basetype(SPIRType::BaseType type)
{
	// For now the enums match up.
	return static_cast<spvc_basetype>(type);
}

spvc_basetype spvc_type_get_basetype(spvc_type type)
{
	return convert_basetype(type->basetype);
}

unsigned spvc_type_get_bit_width(spvc_type type)
{
	return type->width;
}

unsigned spvc_type_get_vector_size(spvc_type type)
{
	return type->vecsize;
}

unsigned spvc_type_get_columns(spvc_type type)
{
	return type->columns;
}

unsigned spvc_type_get_num_array_dimensions(spvc_type type)
{
	return unsigned(type->array.size());
}

spvc_bool spvc_type_array_dimension_is_literal(spvc_type type, unsigned dimension)
{
	return type->array_size_literal[dimension] ? SPVC_TRUE : SPVC_FALSE;
}

SpvId spvc_type_get_array_dimension(spvc_type type, unsigned dimension)
{
	return type->array[dimension];
}

unsigned spvc_type_get_num_member_types(spvc_type type)
{
	return unsigned(type->member_types.size());
}

spvc_type_id spvc_type_get_member_type(spvc_type type, unsigned index)
{
	return type->member_types[index];
}

SpvStorageClass spvc_type_get_storage_class(spvc_type type)
{
	return static_cast<SpvStorageClass>(type->storage);
}

// Image type query.
spvc_type_id spvc_type_get_image_sampled_type(spvc_type type)
{
	return type->image.type;
}

SpvDim spvc_type_get_image_dimension(spvc_type type)
{
	return static_cast<SpvDim>(type->image.dim);
}

spvc_bool spvc_type_get_image_depth(spvc_type type)
{
	return type->image.depth ? SPVC_TRUE : SPVC_FALSE;
}

spvc_bool spvc_type_get_image_arrayed(spvc_type type)
{
	return type->image.arrayed ? SPVC_TRUE : SPVC_FALSE;
}

spvc_bool spvc_type_get_image_multisampled(spvc_type type)
{
	return type->image.ms ? SPVC_TRUE : SPVC_FALSE;
}

spvc_bool spvc_type_get_image_is_storage(spvc_type type)
{
	return type->image.sampled == 2 ? SPVC_TRUE : SPVC_FALSE;
}

SpvImageFormat spvc_type_get_image_storage_format(spvc_type type)
{
	return static_cast<SpvImageFormat>(static_cast<const SPIRType *>(type)->image.format);
}

SpvAccessQualifier spvc_type_get_image_access_qualifier(spvc_type type)
{
	return static_cast<SpvAccessQualifier>(static_cast<const SPIRType *>(type)->image.access);
}

size_t spvc_get_declared_struct_size(spvc_compiler compiler, spvc_type struct_type)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		return compiler->compiler->get_declared_struct_size(*static_cast<const SPIRType *>(struct_type));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, 0)
}

size_t spvc_get_declared_struct_size_runtime_array(spvc_compiler compiler,
                                                   spvc_type struct_type, size_t array_size)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		return compiler->compiler->get_declared_struct_size_runtime_array(*static_cast<const SPIRType *>(struct_type),
		                                                                  array_size);
	}
	SPVC_END_SAFE_SCOPE(compiler->context, 0)
}

unsigned spvc_type_struct_member_offset(spvc_compiler compiler,
                                        spvc_type type, unsigned index)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		return compiler->compiler->type_struct_member_offset(*static_cast<const SPIRType *>(type), index);
	}
	SPVC_END_SAFE_SCOPE(compiler->context, 0)
}

unsigned spvc_type_struct_member_array_stride(spvc_compiler compiler,
                                              spvc_type type, unsigned index)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		return compiler->compiler->type_struct_member_array_stride(*static_cast<const SPIRType *>(type), index);
	}
	SPVC_END_SAFE_SCOPE(compiler->context, 0)
}

unsigned spvc_type_struct_member_matrix_stride(spvc_compiler compiler,
                                               spvc_type type, unsigned index)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		return compiler->compiler->type_struct_member_matrix_stride(*static_cast<const SPIRType *>(type), index);
	}
	SPVC_END_SAFE_SCOPE(compiler->context, 0)
}

spvc_variable_id spvc_build_dummy_sampler_for_combined_images(spvc_compiler compiler)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		return compiler->compiler->build_dummy_sampler_for_combined_images();
	}
	SPVC_END_SAFE_SCOPE(compiler->context, 0)
}

spvc_result spvc_build_combined_image_samplers(spvc_compiler compiler)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		compiler->compiler->build_combined_image_samplers();
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_UNSUPPORTED_SPIRV)
	return SPVC_SUCCESS;
}

spvc_result spvc_get_combined_image_samplers(spvc_compiler compiler,
                                             const struct spvc_combined_image_sampler **samplers,
                                             size_t *num_samplers)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto combined = compiler->compiler->get_combined_image_samplers();
		std::vector<spvc_combined_image_sampler> translated;
		translated.reserve(combined.size());
		for (auto &c : combined)
		{
			spvc_combined_image_sampler trans = {c.combined_id, c.image_id, c.sampler_id};
			translated.push_back(trans);
		}

		auto ptr = std::unique_ptr<TemporaryBuffer<spvc_combined_image_sampler>>(
				new TemporaryBuffer<spvc_combined_image_sampler>());
		ptr->buffer = std::move(translated);
		*samplers = ptr->buffer.data();
		*num_samplers = ptr->buffer.size();
		compiler->context->allocations.push_back(std::move(ptr));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

spvc_result spvc_get_specialization_constants(spvc_compiler compiler,
                                              const struct spvc_specialization_constant **constants,
                                              size_t *num_constants)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto spec_constants = compiler->compiler->get_specialization_constants();
		std::vector<spvc_specialization_constant> translated;
		translated.reserve(spec_constants.size());
		for (auto &c : spec_constants)
		{
			spvc_specialization_constant trans = {c.id, c.constant_id};
			translated.push_back(trans);
		}

		auto ptr = std::unique_ptr<TemporaryBuffer<spvc_specialization_constant>>(
				new TemporaryBuffer<spvc_specialization_constant>());
		ptr->buffer = std::move(translated);
		*constants = ptr->buffer.data();
		*num_constants = ptr->buffer.size();
		compiler->context->allocations.push_back(std::move(ptr));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

spvc_constant spvc_get_constant_handle(spvc_compiler compiler,
                                       spvc_variable_id id)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		return static_cast<spvc_constant>(&compiler->compiler->get_constant(id));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, nullptr)
}

spvc_constant_id spvc_get_work_group_size_specialization_constants(spvc_compiler compiler,
                                                                   spvc_specialization_constant *x,
                                                                   spvc_specialization_constant *y,
                                                                   spvc_specialization_constant *z)
{
	SpecializationConstant tmpx = {};
	SpecializationConstant tmpy = {};
	SpecializationConstant tmpz = {};
	spvc_constant_id ret = compiler->compiler->get_work_group_size_specialization_constants(tmpx, tmpy, tmpz);
	x->id = tmpx.id;
	x->constant_id = tmpx.constant_id;
	y->id = tmpy.id;
	y->constant_id = tmpy.constant_id;
	z->id = tmpz.id;
	z->constant_id = tmpz.constant_id;
	return ret;
}

spvc_bool spvc_get_binary_offset_for_decoration(spvc_compiler compiler,
                                                spvc_variable_id id,
                                                SpvDecoration decoration,
                                                unsigned *word_offset)
{
	uint32_t off = 0;
	bool ret = compiler->compiler->get_binary_offset_for_decoration(id, static_cast<spv::Decoration>(decoration), off);
	if (ret)
	{
		*word_offset = off;
		return SPVC_TRUE;
	}
	else
		return SPVC_FALSE;
}

spvc_bool spvc_buffer_is_hlsl_counter_buffer(spvc_compiler compiler, spvc_variable_id id)
{
	return compiler->compiler->buffer_is_hlsl_counter_buffer(id) ? SPVC_TRUE : SPVC_FALSE;
}

spvc_bool spvc_buffer_get_hlsl_counter_buffer(spvc_compiler compiler, spvc_variable_id id, spvc_variable_id *counter_id)
{
	uint32_t buffer;
	bool ret = compiler->compiler->buffer_get_hlsl_counter_buffer(id, buffer);
	if (ret)
	{
		*counter_id = buffer;
		return SPVC_TRUE;
	}
	else
		return SPVC_FALSE;
}

spvc_result spvc_get_declared_capabilities(spvc_compiler compiler, const SpvCapability **capabilities, size_t *num_capabilities)
{
	auto &caps = compiler->compiler->get_declared_capabilities();
	static_assert(sizeof(SpvCapability) == sizeof(spv::Capability), "Enum size mismatch.");
	*capabilities = reinterpret_cast<const SpvCapability *>(caps.data());
	*num_capabilities = caps.size();
	return SPVC_SUCCESS;
}

spvc_result spvc_get_declared_extensions(spvc_compiler compiler, const char ***extensions, size_t *num_extensions)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto &exts = compiler->compiler->get_declared_extensions();
		std::vector<const char *> duped;
		duped.reserve(exts.size());
		for (auto &ext : exts)
			duped.push_back(compiler->context->allocate_name(ext));

		std::unique_ptr<TemporaryBuffer<const char *>> ptr(new TemporaryBuffer<const char *>);
		ptr->buffer = std::move(duped);
		*extensions = ptr->buffer.data();
		*num_extensions = ptr->buffer.size();
		compiler->context->allocations.push_back(std::move(ptr));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_OUT_OF_MEMORY)
	return SPVC_SUCCESS;
}

const char *spvc_get_remapped_declared_block_name(spvc_compiler compiler, spvc_variable_id id)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto name = compiler->compiler->get_remapped_declared_block_name(id);
		return compiler->context->allocate_name(name);
	}
	SPVC_END_SAFE_SCOPE(compiler->context, nullptr)
}

spvc_result spvc_get_buffer_block_decorations(spvc_compiler compiler, spvc_variable_id id,
                                              const SpvDecoration **decorations,
                                              size_t *num_decorations)
{
	SPVC_BEGIN_SAFE_SCOPE
	{
		auto flags = compiler->compiler->get_buffer_block_flags(id);
		std::unique_ptr<TemporaryBuffer<SpvDecoration>> bitset(new TemporaryBuffer<SpvDecoration>);

		flags.for_each_bit([&](uint32_t bit) {
			bitset->buffer.push_back(static_cast<SpvDecoration>(bit));
		});

		*decorations = bitset->buffer.data();
		*num_decorations = bitset->buffer.size();
		compiler->context->allocations.push_back(std::move(bitset));
	}
	SPVC_END_SAFE_SCOPE(compiler->context, SPVC_ERROR_INVALID_ARGUMENT)
	return SPVC_SUCCESS;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif