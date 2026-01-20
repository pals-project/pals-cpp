#ifndef YAML_C_WRAPPER_H
#define YAML_C_WRAPPER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* YAMLNodeHandle;

// === CREATION ===
YAMLNodeHandle yaml_create_node(void);
YAMLNodeHandle yaml_create_map(void);
YAMLNodeHandle yaml_create_sequence(void);
void yaml_delete_node(YAMLNodeHandle handle);

// === PARSING ===
YAMLNodeHandle yaml_parse(const char* yaml_str);
YAMLNodeHandle yaml_parse_file(const char* filename);

// === TYPE CHECKING ===
bool yaml_is_scalar(YAMLNodeHandle handle);
bool yaml_is_sequence(YAMLNodeHandle handle);
bool yaml_is_map(YAMLNodeHandle handle);
bool yaml_is_null(YAMLNodeHandle handle);
bool yaml_is_defined(YAMLNodeHandle handle);

// === ACCESS ===
YAMLNodeHandle yaml_get_key(YAMLNodeHandle handle, const char* key);
YAMLNodeHandle yaml_get_index(YAMLNodeHandle handle, int index);
bool yaml_has_key(YAMLNodeHandle handle, const char* key);
int yaml_size(YAMLNodeHandle handle);

// === CONVERSION ===
char* yaml_as_string(YAMLNodeHandle handle);
int yaml_as_int(YAMLNodeHandle handle);
double yaml_as_float(YAMLNodeHandle handle);
bool yaml_as_bool(YAMLNodeHandle handle);
void yaml_free_string(char* str);

// === MODIFICATION ===
void yaml_set_string(YAMLNodeHandle handle, const char* key, const char* value);
void yaml_set_int(YAMLNodeHandle handle, const char* key, int value);
void yaml_set_float(YAMLNodeHandle handle, const char* key, double value);
void yaml_set_bool(YAMLNodeHandle handle, const char* key, bool value);
void yaml_set_node(YAMLNodeHandle handle, const char* key, YAMLNodeHandle value);

void yaml_push_string(YAMLNodeHandle handle, const char* value);
void yaml_push_int(YAMLNodeHandle handle, int value);
void yaml_push_float(YAMLNodeHandle handle, double value);
void yaml_push_bool(YAMLNodeHandle handle, bool value);
void yaml_push_node(YAMLNodeHandle handle, YAMLNodeHandle value);

// === UTILITY ===
char* yaml_to_string(YAMLNodeHandle handle);
char* yaml_emit(YAMLNodeHandle handle, int indent);
YAMLNodeHandle yaml_clone(YAMLNodeHandle handle);
YAMLNodeHandle yaml_deep_copy(YAMLNodeHandle handle);
YAMLNodeHandle yaml_expand(YAMLNodeHandle handle);

bool yaml_write_file(YAMLNodeHandle handle, const char* filename);
bool yaml_write_file_formatted(YAMLNodeHandle handle, const char* filename,
                                int indent, bool flow_maps, bool flow_seqs);

// === KEY UTILITIES ===
char** yaml_get_keys(YAMLNodeHandle handle, int* out_count);
void yaml_free_keys(char** keys, int count);

#ifdef __cplusplus
}
#endif

#endif // YAML_C_WRAPPER_H