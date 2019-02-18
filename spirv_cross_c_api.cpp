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

#include "spirv_cross_c_api.h"
#include "spirv_glsl.hpp"
#include "spirv_msl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_cpp.hpp"
#include "spirv_reflect.hpp"
#include "spirv_parser.hpp"
#include <new>
#include <memory>

using namespace std;
using namespace spirv_cross;

struct spvc_context_s
{
	string last_error;
};

struct spvc_parsed_ir_s
{
	spvc_context context;
	ParsedIR parsed;
};

struct spvc_compiler_s
{
	spvc_context context;
	unique_ptr<Compiler> compiler;
	spvc_backend backend;
};

struct spvc_compiler_options_s
{
	spvc_context context;
	uint32_t backend_flags;
	CompilerGLSL::Options glsl;
	CompilerMSL::Options msl;
	CompilerHLSL::Options hlsl;
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

