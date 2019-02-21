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

using namespace std;
using namespace spirv_cross;

struct spvc_context_s
{
	string last_error;
};

struct spvc_parsed_ir_s
{
	spvc_context context = nullptr;
	ParsedIR parsed;
};

struct NameAllocator
{
	const char *allocate_name(const std::string &name);
	std::vector<std::unique_ptr<char[]>> string_storage;
};

struct spvc_compiler_s : NameAllocator
{
	spvc_context context = nullptr;
	unique_ptr<Compiler> compiler;
	spvc_backend backend = SPVC_BACKEND_NONE;

	std::vector<spvc_entry_point> entry_points;
};

struct spvc_compiler_options_s
{
	spvc_context context = nullptr;
	uint32_t backend_flags = 0;
	CompilerGLSL::Options glsl;
	CompilerMSL::Options msl;
	CompilerHLSL::Options hlsl;
};

struct spvc_resources_s : NameAllocator
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

spvc_error spvc_create_context(spvc_context *context)
{
	auto *ctx = new(std::nothrow) spvc_context_s;
	if (!ctx)
		return SPVC_ERROR_OUT_OF_MEMORY;

	*context = ctx;
	return SPVC_SUCCESS;
}

spvc_error spvc_destroy_context(spvc_context context)
{
	delete context;
	return SPVC_SUCCESS;
}

const char *spvc_get_last_error_string(spvc_context context)
{
	return context->last_error.c_str();
}

spvc_error spvc_parse_spirv(spvc_context context, const SpvId *spirv, size_t word_count,
                            spvc_parsed_ir *parsed_ir)
{
	auto *pir = new(std::nothrow) spvc_parsed_ir_s;
	if (!pir)
		return SPVC_ERROR_OUT_OF_MEMORY;

	pir->context = context;
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
	try
	{
#endif
		Parser parser(spirv, word_count);
		parser.parse();
		pir->parsed = move(parser.get_parsed_ir());
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
	}
	catch (const exception &e)
	{
		pir->context->last_error = e.what();
		delete pir;
		return SPVC_ERROR_INVALID_SPIRV;
	}
#endif

	*parsed_ir = pir;
	return SPVC_SUCCESS;
}

void spvc_destroy_parsed_ir(spvc_parsed_ir parsed_ir)
{
	delete parsed_ir;
}

spvc_error spvc_create_compiler(spvc_context context, spvc_backend backend,
                                spvc_parsed_ir parsed_ir, spvc_capture_mode mode,
                                spvc_compiler *compiler)
{
	auto *comp = new(std::nothrow) spvc_compiler_s;
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
		{
			delete comp;
			return SPVC_ERROR_INVALID_ARGUMENT;
		}
		break;

	case SPVC_BACKEND_GLSL:
		if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
			comp->compiler.reset(new CompilerGLSL(move(parsed_ir->parsed)));
		else if (mode == SPVC_CAPTURE_MODE_COPY)
			comp->compiler.reset(new CompilerGLSL(parsed_ir->parsed));
		else
		{
			delete comp;
			return SPVC_ERROR_INVALID_ARGUMENT;
		}
		break;

	case SPVC_BACKEND_HLSL:
		if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
			comp->compiler.reset(new CompilerHLSL(move(parsed_ir->parsed)));
		else if (mode == SPVC_CAPTURE_MODE_COPY)
			comp->compiler.reset(new CompilerHLSL(parsed_ir->parsed));
		else
		{
			delete comp;
			return SPVC_ERROR_INVALID_ARGUMENT;
		}
		break;

	case SPVC_BACKEND_MSL:
		if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
			comp->compiler.reset(new CompilerMSL(move(parsed_ir->parsed)));
		else if (mode == SPVC_CAPTURE_MODE_COPY)
			comp->compiler.reset(new CompilerMSL(parsed_ir->parsed));
		else
		{
			delete comp;
			return SPVC_ERROR_INVALID_ARGUMENT;
		}
		break;

	case SPVC_BACKEND_CPP:
		if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
			comp->compiler.reset(new CompilerCPP(move(parsed_ir->parsed)));
		else if (mode == SPVC_CAPTURE_MODE_COPY)
			comp->compiler.reset(new CompilerCPP(parsed_ir->parsed));
		else
		{
			delete comp;
			return SPVC_ERROR_INVALID_ARGUMENT;
		}
		break;

	case SPVC_BACKEND_JSON:
		if (mode == SPVC_CAPTURE_MODE_TAKE_OWNERSHIP)
			comp->compiler.reset(new CompilerReflection(move(parsed_ir->parsed)));
		else if (mode == SPVC_CAPTURE_MODE_COPY)
			comp->compiler.reset(new CompilerReflection(parsed_ir->parsed));
		else
		{
			delete comp;
			return SPVC_ERROR_INVALID_ARGUMENT;
		}
		break;

	default:
		delete comp;
		return SPVC_ERROR_INVALID_ARGUMENT;
	}

	*compiler = comp;
	return SPVC_SUCCESS;
}

void spvc_destroy_compiler(spvc_compiler compiler)
{
	delete compiler;
}

spvc_error spvc_create_compiler_options(spvc_compiler compiler, spvc_compiler_options *options)
{
	auto *opt = new (std::nothrow) spvc_compiler_options_s;
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

	*options = opt;
	return SPVC_SUCCESS;
}

spvc_error spvc_set_compiler_option_bool(spvc_compiler_options options,
                                         spvc_compiler_option option, spvc_bool value)
{
	return spvc_set_compiler_option_uint(options, option, value ? 1 : 0);
}

spvc_error spvc_set_compiler_option_uint(spvc_compiler_options options,
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

spvc_error spvc_install_compiler_options(spvc_compiler compiler, spvc_compiler_options options)
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

void spvc_destroy_compiler_options(spvc_compiler_options options)
{
	delete options;
}

spvc_error spvc_compile(spvc_compiler compiler, char **source)
{
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
	try
#endif
	{
		auto result = compiler->compiler->compile();
		if (result.empty())
		{
			compiler->context->last_error = "Unsupported SPIR-V.";
			return SPVC_ERROR_UNSUPPORTED_SPIRV;
		}

		char *new_string = new char[result.size() + 1];
		strcpy(new_string, result.c_str());
		*source = new_string;
		return SPVC_SUCCESS;
	}
#ifndef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
	catch (const std::exception &e)
	{
		compiler->context->last_error = e.what();
		return SPVC_ERROR_UNSUPPORTED_SPIRV;
	}
#endif
}

void spvc_destroy_string(char *source)
{
	delete[] source;
}

const char *NameAllocator::allocate_name(const std::string &name)
{
	char *dup = new (std::nothrow) char[name.size() + 1];
	if (dup)
	{
		string_storage.emplace_back(dup);
		strcpy(dup, name.c_str());
	}
	return dup;
}

bool spvc_resources_s::copy_resources(std::vector<spvc_reflected_resource> &outputs,
                                      const std::vector<Resource> &inputs)
{
	for (auto &i : inputs)
	{
		spvc_reflected_resource r = {};
		r.base_type_id = i.base_type_id;
		r.type_id = i.type_id;
		r.id = i.id;
		r.name = allocate_name(i.name);
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

spvc_error spvc_create_statically_accessed_shader_resources(spvc_compiler compiler, spvc_resources *resources)
{
	auto *res = new (std::nothrow) spvc_resources_s();
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
		delete res;
		return SPVC_ERROR_OUT_OF_MEMORY;
	}

	*resources = res;
	return SPVC_SUCCESS;
}

spvc_error spvc_create_shader_resources(spvc_compiler compiler, spvc_resources *resources)
{
	auto *res = new (std::nothrow) spvc_resources_s();
	if (!res)
		return SPVC_ERROR_OUT_OF_MEMORY;

	res->context = compiler->context;
	auto accessed_resources = compiler->compiler->get_shader_resources();

	if (!res->copy_resources(accessed_resources))
	{
		res->context->last_error = "Out of memory.";
		delete res;
		return SPVC_ERROR_OUT_OF_MEMORY;
	}

	*resources = res;
	return SPVC_SUCCESS;
}

spvc_error spvc_get_resource_list(spvc_resources resources, spvc_resource_type type,
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

void spvc_destroy_shader_resources(spvc_resources resources)
{
	delete resources;
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

spvc_error spvc_get_entry_points(spvc_compiler compiler, const struct spvc_entry_point **entry_points, size_t *num_entry_points)
{
	compiler->string_storage.clear();
	compiler->entry_points.clear();

	auto entries = compiler->compiler->get_entry_points_and_stages();
	for (auto &entry : entries)
	{
		spvc_entry_point new_entry = {};
		new_entry.execution_model = static_cast<SpvExecutionModel>(entry.execution_model);
		new_entry.name = compiler->allocate_name(entry.name);
		if (!new_entry.name)
		{
			compiler->context->last_error = "Out of memory.";
			return SPVC_ERROR_OUT_OF_MEMORY;
		}
		compiler->entry_points.push_back(new_entry);
	}

	*num_entry_points = compiler->entry_points.size();
	*entry_points = compiler->entry_points.data();
	return SPVC_SUCCESS;
}

spvc_error spvc_set_entry_point(spvc_compiler compiler, const char *name, SpvExecutionModel model)
{
	compiler->compiler->set_entry_point(name, static_cast<spv::ExecutionModel>(model));
	return SPVC_SUCCESS;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif