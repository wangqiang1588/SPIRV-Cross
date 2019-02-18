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

#ifndef SPIRV_CROSS_C_API_H
#define SPIRV_CROSS_C_API_H

#include <stddef.h>
#include "spirv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPVC_PUBLIC_API

typedef struct spvc_context_s *spvc_context;
typedef struct spvc_parsed_ir_s *spvc_parsed_ir;
typedef struct spvc_compiler_s *spvc_compiler;
typedef struct spvc_type_s *spvc_type;
typedef struct spvc_compiler_options_s *spvc_compiler_options;
typedef struct spvc_resources_s *spvc_resources;

typedef SpvId spvc_type_id;
typedef SpvId spvc_variable_id;
typedef SpvId spvc_constant_id;
typedef SpvId spvc_id;

struct spvc_reflected_resource
{
	spvc_variable_id id;
	spvc_type_id base_type_id;
	spvc_type_id type_id;
	const char *name;
};

struct spvc_entry_point
{
	SpvExecutionModel execution_model;
	const char *name;
};

typedef int spvc_bool;
#define SPVC_TRUE ((spvc_bool)1)
#define SPVC_FALSE ((spvc_bool)0)

typedef enum spvc_error
{
	// Success.
	SPVC_SUCCESS = 0,

	// The SPIR-V is invalid.
	SPVC_ERROR_INVALID_SPIRV = -1,

	// The SPIR-V might be valid, but SPIRV-Cross currently cannot correctly translate this to your target language.
	SPVC_ERROR_UNSUPPORTED_SPIRV = -2,

	// If for some reason we hit this.
	SPVC_ERROR_OUT_OF_MEMORY = -3,

	// Invalid API argument.
	SPVC_ERROR_INVALID_ARGUMENT = -4,

	SPVC_ERROR_INT_MAX = 0x7fffffff
} spvc_error;

typedef enum spvc_capture_mode
{
	// The Parsed IR will be copied, and the handle can be reused.
	SPVC_CAPTURE_MODE_COPY = 0,

	// The payload will now be owned by the compiler.
	// parsed_ir should now be considered a dead blob, and the only thing to do is to destroy it.
	// This is optimal for performance.
	SPVC_CAPTURE_MODE_TAKE_OWNERSHIP = 1,

	SPVC_CAPTURE_MODE_INT_MAX = 0x7fffffff
} spvc_capture_mode;

typedef enum spvc_backend
{
	// This backend can only perform reflection, no compiler options are supported.
	SPVC_BACKEND_NONE = 0,
	SPVC_BACKEND_GLSL = 1,
	SPVC_BACKEND_HLSL = 2,
	SPVC_BACKEND_MSL = 3,
	SPVC_BACKEND_CPP = 4,
	SPVC_BACKEND_JSON = 5,
	SPVC_BACKEND_INT_MAX = 0x7fffffff
} spvc_backend;

typedef enum spvc_resource_type
{
	SPVC_RESOURCE_TYPE_UNKNOWN = 0,
	SPVC_RESOURCE_TYPE_UNIFORM_BUFFER = 1,
	SPVC_RESOURCE_TYPE_STORAGE_BUFFER = 2,
	SPVC_RESOURCE_TYPE_STAGE_INPUT = 3,
	SPVC_RESOURCE_TYPE_STAGE_OUTPUT = 4,
	SPVC_RESOURCE_TYPE_SUBPASS_INPUT = 5,
	SPVC_RESOURCE_TYPE_STORAGE_IMAGE = 6,
	SPVC_RESOURCE_TYPE_SAMPLED_IMAGE = 7,
	SPVC_RESOURCE_TYPE_ATOMIC_COUNTER = 8,
	SPVC_RESOURCE_TYPE_PUSH_CONSTANT = 9,
	SPVC_RESOURCE_TYPE_SEPARATE_IMAGE = 10,
	SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS = 11,
	SPVC_RESOURCE_TYPE_INT_MAX = 0x7fffffff
} spvc_resource_type;

#define SPVC_COMPILER_OPTION_COMMON_BIT 0x1000000
#define SPVC_COMPILER_OPTION_GLSL_BIT 0x2000000
#define SPVC_COMPILER_OPTION_HLSL_BIT 0x4000000
#define SPVC_COMPILER_OPTION_MSL_BIT 0x8000000
#define SPVC_COMPILER_OPTION_LANG_BITS 0x0f000000
#define SPVC_COMPILER_OPTION_ENUM_BITS 0xffffff

#define SPVC_MAKE_MSL_VERSION(major, minor, patch) ((major) * 10000 + (minor) * 100 + (patch))

enum spvc_msl_platform
{
	SPVC_MSL_PLATFORM_IOS = 0,
	SPVC_MSL_PLATFORM_MACOS = 1,
	SPVC_MSL_PLATFORM_MAX_INT = 0x7fffffff
};

typedef enum spvc_compiler_option
{
	SPVC_COMPILER_OPTION_UNKNOWN = 0,

	SPVC_COMPILER_OPTION_FORCE_TEMPORARY = 1 | SPVC_COMPILER_OPTION_COMMON_BIT,
	SPVC_COMPILER_OPTION_FLATTEN_MULTIDIMENSIONAL_ARRAYS = 2 | SPVC_COMPILER_OPTION_COMMON_BIT,
	SPVC_COMPILER_OPTION_FIXUP_DEPTH_CONVENTION = 3 | SPVC_COMPILER_OPTION_COMMON_BIT,
	SPVC_COMPILER_OPTION_FLIP_VERTEX_Y = 4 | SPVC_COMPILER_OPTION_COMMON_BIT,

	SPVC_COMPILER_OPTION_GLSL_SUPPORT_NONZERO_BASE_INSTANCE = 5 | SPVC_COMPILER_OPTION_GLSL_BIT,
	SPVC_COMPILER_OPTION_GLSL_SEPARATE_SHADER_OBJECTS = 6 | SPVC_COMPILER_OPTION_GLSL_BIT,
	SPVC_COMPILER_OPTION_GLSL_ENABLE_420PACK_EXTENSION = 7 | SPVC_COMPILER_OPTION_GLSL_BIT,
	SPVC_COMPILER_OPTION_GLSL_VERSION = 8 | SPVC_COMPILER_OPTION_GLSL_BIT,
	SPVC_COMPILER_OPTION_GLSL_ES = 9 | SPVC_COMPILER_OPTION_GLSL_BIT,
	SPVC_COMPILER_OPTION_GLSL_VULKAN_SEMANTICS = 10 | SPVC_COMPILER_OPTION_GLSL_BIT,
	SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_FLOAT_PRECISION_HIGHP = 11 | SPVC_COMPILER_OPTION_GLSL_BIT,
	SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_INT_PRECISION_HIGHP = 12 | SPVC_COMPILER_OPTION_GLSL_BIT,

	SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL = 13 | SPVC_COMPILER_OPTION_HLSL_BIT,
	SPVC_COMPILER_OPTION_HLSL_POINT_SIZE_COMPAT = 14 | SPVC_COMPILER_OPTION_HLSL_BIT,
	SPVC_COMPILER_OPTION_HLSL_POINT_COORD_COMPAT = 15 | SPVC_COMPILER_OPTION_HLSL_BIT,
	SPVC_COMPILER_OPTION_HLSL_SUPPORT_NONZERO_BASE_VERTEX_BASE_INSTANCE = 16 | SPVC_COMPILER_OPTION_HLSL_BIT,

	SPVC_COMPILER_OPTION_MSL_VERSION = 17 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_TEXEL_BUFFER_TEXTURE_WIDTH = 18 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_AUX_BUFFER_INDEX = 19 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_INDIRECT_PARAMS_BUFFER_INDEX = 20 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_SHADER_OUTPUT_BUFFER_INDEX = 21 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_SHADER_PATCH_OUTPUT_BUFFER_INDEX = 22 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_SHADER_TESS_FACTOR_OUTPUT_BUFFER_INDEX = 23 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_SHADER_INPUT_WORKGROUP_INDEX = 24 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_ENABLE_POINT_SIZE_BUILTIN = 25 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_DISABLE_RASTERIZATION = 26 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_CAPTURE_OUTPUT_TO_BUFFER = 27 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_SWIZZLE_TEXTURE_SAMPLES = 28 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_PAD_FRAGMENT_OUTPUT_COMPONENTS = 29 | SPVC_COMPILER_OPTION_MSL_BIT,
	SPVC_COMPILER_OPTION_MSL_PLATFORM = 30 | SPVC_COMPILER_OPTION_MSL_BIT,

	SPVC_COMPILER_OPTION_INT_MAX = 0x7fffffff
} spvc_compiler_option;

// Context is the highest-level API construct.
SPVC_PUBLIC_API spvc_error spvc_create_context(spvc_context *context);
SPVC_PUBLIC_API spvc_error spvc_destroy_context(spvc_context context);
SPVC_PUBLIC_API const char *spvc_get_last_error_string(spvc_context context);

// SPIR-V parsing interface.
SPVC_PUBLIC_API spvc_error spvc_parse_spirv(spvc_context context, const SpvId *spirv, size_t word_count,
                                            spvc_parsed_ir *parsed_ir);
SPVC_PUBLIC_API void spvc_destroy_parsed_ir(spvc_parsed_ir parsed_ir);

// Create a compiler backend
SPVC_PUBLIC_API spvc_error spvc_create_compiler(spvc_context context, spvc_backend backend,
                                                spvc_parsed_ir parsed_ir, spvc_capture_mode mode,
                                                spvc_compiler *compiler);
SPVC_PUBLIC_API void spvc_destroy_compiler(spvc_compiler compiler);

// Set options.
SPVC_PUBLIC_API spvc_error spvc_create_compiler_options(spvc_compiler compiler, spvc_compiler_options *options);
SPVC_PUBLIC_API spvc_error spvc_set_compiler_option_bool(spvc_compiler_options options,
                                                         spvc_compiler_option, spvc_bool value);
SPVC_PUBLIC_API spvc_error spvc_set_compiler_option_uint(spvc_compiler_options options,
                                                         spvc_compiler_option, unsigned value);
SPVC_PUBLIC_API spvc_error spvc_install_compiler_options(spvc_compiler compiler, spvc_compiler_options options);
SPVC_PUBLIC_API void spvc_destroy_compiler_options(spvc_compiler_options options);

// Compile IR into a string.
SPVC_PUBLIC_API spvc_error spvc_compile(spvc_compiler compiler, char **string);
SPVC_PUBLIC_API void spvc_destroy_string(char *string);

// Reflect resources.
SPVC_PUBLIC_API spvc_error spvc_create_statically_accessed_shader_resources(spvc_compiler compiler, spvc_resources *resources);
SPVC_PUBLIC_API spvc_error spvc_create_shader_resources(spvc_compiler compiler, spvc_resources *resources);
SPVC_PUBLIC_API spvc_error spvc_get_resource_list(spvc_resources resources, spvc_resource_type type,
                                                  const struct spvc_reflected_resource **resource_list,
                                                  size_t *resource_size);
SPVC_PUBLIC_API void spvc_destroy_shader_resources(spvc_resources resources);

// Decorations
SPVC_PUBLIC_API void spvc_set_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration, unsigned argument);
SPVC_PUBLIC_API void spvc_set_decoration_string(spvc_compiler compiler, SpvId id, SpvDecoration decoration, const char *argument);
SPVC_PUBLIC_API void spvc_set_name(spvc_compiler compiler, SpvId id, const char *argument);
SPVC_PUBLIC_API void spvc_set_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration, unsigned argument);
SPVC_PUBLIC_API void spvc_set_member_decoration_string(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration, const char *argument);
SPVC_PUBLIC_API void spvc_set_member_name(spvc_compiler compiler, spvc_type_id id, unsigned member_index, const char *argument);
SPVC_PUBLIC_API void spvc_unset_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration);
SPVC_PUBLIC_API void spvc_unset_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration);

SPVC_PUBLIC_API spvc_bool spvc_has_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration);
SPVC_PUBLIC_API spvc_bool spvc_has_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration);
SPVC_PUBLIC_API const char *spvc_get_name(spvc_compiler compiler, SpvId id);
SPVC_PUBLIC_API unsigned spvc_get_decoration(spvc_compiler compiler, SpvId id, SpvDecoration decoration);
SPVC_PUBLIC_API const char *spvc_get_decoration_string(spvc_compiler compiler, SpvId id, SpvDecoration decoration);
SPVC_PUBLIC_API unsigned spvc_get_member_decoration(spvc_compiler compiler, spvc_type_id id, unsigned member_index, SpvDecoration decoration);
SPVC_PUBLIC_API const char *spvc_get_member_decoration_string(spvc_compiler compiler, spvc_type_id id,
                                                              unsigned member_index, SpvDecoration decoration);

// Entry points.
SPVC_PUBLIC_API spvc_error spvc_get_entry_points(spvc_compiler compiler, const struct spvc_entry_points **entry_points, size_t *num_entry_points);
SPVC_PUBLIC_API spvc_error spvc_set_entry_point(spvc_compiler compiler, const char *name, SpvExecutionModel model);

#ifdef __cplusplus
}
#endif
#endif
