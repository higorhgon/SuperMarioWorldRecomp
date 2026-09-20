#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_external_rom_path[1024];

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        text++;
    char *end = text + strlen(text);
    while (end > text &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
            end[-1] == '\n')) {
        *--end = '\0';
    }
    return text;
}

static int parse_quoted_value(const char *line, char *out, size_t out_size)
{
    const char *equals = strchr(line, '=');
    if (!equals || out_size == 0) return 0;
    const char *value = equals + 1;
    while (*value == ' ' || *value == '\t') value++;
    if (*value != '"') return 0;
    value++;
    size_t written = 0;
    while (*value && *value != '"' && written + 1 < out_size) {
        if (*value == '\\' && value[1]) value++;
        out[written++] = *value++;
    }
    out[written] = '\0';
    return *value == '"';
}

const char *snes_mod_external_rom_path(const char *package_id,
                                       const char *feature_id,
                                       const char *resource_id)
{
    (void)feature_id;
    const char *env = getenv("SNESRECOMP_FALCON_OWNER_ROM");
    if (env && env[0]) return env;

    FILE *file = fopen("mods/state.toml", "r");
    if (!file) return NULL;

    int in_resource = 0;
    int package_match = 0;
    int resource_match = 0;
    char line[1200];
    char value[1024];
    g_external_rom_path[0] = '\0';
    while (fgets(line, sizeof(line), file)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *text = trim(line);
        if (!text[0]) continue;
        if (strcmp(text, "[[resource]]") == 0) {
            in_resource = 1;
            package_match = 0;
            resource_match = 0;
            g_external_rom_path[0] = '\0';
            continue;
        }
        if (text[0] == '[') {
            in_resource = 0;
            continue;
        }
        if (!in_resource) continue;
        if (strncmp(text, "package_id", 10) == 0 &&
            parse_quoted_value(text, value, sizeof(value))) {
            package_match = package_id && strcmp(value, package_id) == 0;
        } else if (strncmp(text, "id", 2) == 0 &&
                   parse_quoted_value(text, value, sizeof(value))) {
            resource_match = resource_id && strcmp(value, resource_id) == 0;
        } else if (strncmp(text, "path", 4) == 0 &&
                   parse_quoted_value(text, g_external_rom_path,
                                      sizeof(g_external_rom_path))) {
            if (package_match && resource_match) {
                fclose(file);
                return g_external_rom_path[0] ? g_external_rom_path : NULL;
            }
        }
    }

    fclose(file);
    return NULL;
}
