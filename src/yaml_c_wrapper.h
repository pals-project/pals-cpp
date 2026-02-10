#ifndef YAML_C_WRAPPER_H
#define YAML_C_WRAPPER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* YAMLNodeHandle;

// === CREATION ===
YAMLNodeHandle create_node(void);
YAMLNodeHandle create_map(void);
YAMLNodeHandle create_sequence(void);
YAMLNodeHandle create_scalar();
void delete_node(YAMLNodeHandle handle);

// === PARSING ===
YAMLNodeHandle parse_string(const char* yaml_str);
YAMLNodeHandle parse_file(const char* filename);

// === TYPE CHECKING ===
bool is_scalar(YAMLNodeHandle handle);
bool is_sequence(YAMLNodeHandle handle);
bool is_map(YAMLNodeHandle handle);
bool is_null(YAMLNodeHandle handle);

// === ACCESS ===
YAMLNodeHandle get_key(YAMLNodeHandle handle, const char* key);
YAMLNodeHandle get_index(YAMLNodeHandle handle, int index);
bool has_key(YAMLNodeHandle handle, const char* key);
int size(YAMLNodeHandle handle);
char** get_keys(YAMLNodeHandle handle, int* out_count);
void yaml_free_keys(char** keys, int count);

// === CONVERSION ===
char* as_string(YAMLNodeHandle handle);
int as_int(YAMLNodeHandle handle);
double as_float(YAMLNodeHandle handle);
bool as_bool(YAMLNodeHandle handle);
void yaml_free_string(char* str);

// === MODIFICATION ===
void set_value_string(YAMLNodeHandle handle, const char* key,
                      const char* value);
void set_value_int(YAMLNodeHandle handle, const char* key, int value);
void set_value_float(YAMLNodeHandle handle, const char* key, double value);
void set_value_bool(YAMLNodeHandle handle, const char* key, bool value);
void set_value_node(YAMLNodeHandle handle, const char* key,
                    YAMLNodeHandle value);

void set_scalar_string(YAMLNodeHandle handle, const char* value);
void set_scalar_int(YAMLNodeHandle handle, int value);
void set_scalar_float(YAMLNodeHandle handle, double value);
void set_scalar_bool(YAMLNodeHandle handle, bool value);

void set_at_index(YAMLNodeHandle handle, int index, YAMLNodeHandle value);
void push_string(YAMLNodeHandle handle, const char* value);
void push_int(YAMLNodeHandle handle, int value);
void push_float(YAMLNodeHandle handle, double value);
void yaml_push_bool(YAMLNodeHandle handle, bool value);
void push_node(YAMLNodeHandle handle, YAMLNodeHandle value);

// === UTILITY ===
char* yaml_to_string(YAMLNodeHandle handle);
char* yaml_emit(YAMLNodeHandle handle, int indent);
YAMLNodeHandle yaml_clone(YAMLNodeHandle handle);
YAMLNodeHandle lattice_expand(YAMLNodeHandle handle);

bool write_file(YAMLNodeHandle handle, const char* filename);
bool write_file_formatted(YAMLNodeHandle handle, const char* filename,
                          int indent, bool flow_maps, bool flow_seqs);
bool write_file_advanced(YAMLNodeHandle handle, const char* filename, int indent, bool flow_maps,
    bool flow_seqs, int bool_format, int null_format, int string_format);

#ifdef __cplusplus
}
#endif

#endif  // YAML_C_WRAPPER_H