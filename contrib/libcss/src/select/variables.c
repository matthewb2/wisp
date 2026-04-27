/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "select/variables.h"
#include "utils/utils.h"
#include "utils/libcss_log.h"
#include "lex/lex.h"
#include "utils/parserutilserror.h"
#include "utils/css_utils.h"
#include "bytecode/bytecode.h"
#include "select/dispatch.h"
#include "select/select.h"

#include <parserutils/utils/vector.h>
#include <libcss/functypes.h>
#include <libcss/stylesheet.h>
#include <libcss/properties.h>
#include "parse/properties/properties.h"

/* css_prop_entry struct and lookup function from the gperf table
 * (defined in language.c via prop_hash_table.inc). */
struct css_prop_entry {
    const char *name;
    css_prop_handler handler;
    int opcode;
};
const struct css_prop_entry *css_prop_lookup(
    const char *str, size_t len);

#define VAR_CTX_INITIAL_CAPACITY 4

static css_error css__resolve_token_vector(
    const parserutils_vector *value,
    css_var_context *var_ctx,
    parserutils_vector *out_tokens,
    int depth);

static void css__tokens_truncate(
    parserutils_vector *tokens,
    size_t keep_len)
{
    size_t current_len = 0;

    parserutils_vector_get_length(tokens, &current_len);
    while (current_len > keep_len) {
        const css_token *tok = parserutils_vector_peek(tokens, current_len - 1);
        if (tok != NULL && tok->idata != NULL)
            lwc_string_unref(tok->idata);
        parserutils_vector_remove_last(tokens);
        current_len--;
    }
}

static inline const char *css__var_token_get_str(
    const css_token *token,
    size_t *len)
{
    if (token->idata != NULL) {
        *len = lwc_string_length(token->idata);
        return lwc_string_data(token->idata);
    }

    if (token->data.data != NULL) {
        *len = token->data.len;
        return (const char *)token->data.data;
    }

    switch (token->type) {
    case CSS_TOKEN_S:
        *len = 1;
        return " ";
    case CSS_TOKEN_CDO:
        *len = 4;
        return "<!--";
    case CSS_TOKEN_CDC:
        *len = 3;
        return "-->";
    case CSS_TOKEN_INCLUDES:
        *len = 2;
        return "~=";
    case CSS_TOKEN_DASHMATCH:
        *len = 2;
        return "|=";
    case CSS_TOKEN_PREFIXMATCH:
        *len = 2;
        return "^=";
    case CSS_TOKEN_SUFFIXMATCH:
        *len = 2;
        return "$=";
    case CSS_TOKEN_SUBSTRINGMATCH:
        *len = 2;
        return "*=";
    default:
        *len = 0;
        return "";
    }
}

struct css_var_value {
    uint32_t refcnt;
    lwc_string *raw;
    parserutils_vector *tokens;
    lwc_string **refs;
    uint32_t ref_count;
    uint32_t ref_capacity;
};

struct css_var_lookup {
    lwc_string *name;
    uint32_t index;
};

struct css_var_resolved {
    lwc_string *name;
    parserutils_vector *tokens;
};

static uint32_t css__var_lookup_capacity_for_count(uint32_t count)
{
    uint32_t capacity = 8;

    while (capacity < count * 2)
        capacity <<= 1;

    return capacity;
}

static bool css__var_lookup_find(
    const css_var_lookup *lookup,
    uint32_t capacity,
    lwc_string *name,
    uint32_t *index)
{
    uint32_t slot;
    uint32_t mask;

    if (lookup == NULL || capacity == 0)
        return false;

    mask = capacity - 1;
    slot = lwc_string_hash_value(name) & mask;

    while (lookup[slot].name != NULL) {
        if (lookup[slot].name == name) {
            if (index != NULL)
                *index = lookup[slot].index;
            return true;
        }
        slot = (slot + 1) & mask;
    }

    return false;
}

static void css__var_lookup_insert(
    css_var_lookup *lookup,
    uint32_t capacity,
    lwc_string *name,
    uint32_t index)
{
    uint32_t slot = lwc_string_hash_value(name) & (capacity - 1);

    while (lookup[slot].name != NULL && lookup[slot].name != name)
        slot = (slot + 1) & (capacity - 1);

    lookup[slot].name = name;
    lookup[slot].index = index;
}

static bool css__is_var_function_token(const css_token *token)
{
    size_t len = 0;
    const char *data;

    if (token->type != CSS_TOKEN_FUNCTION)
        return false;

    data = css__var_token_get_str(token, &len);
    return len == 3 && strncasecmp(data, "var", 3) == 0;
}

static void css__tokens_destroy(parserutils_vector *tokens)
{
    int32_t ctx = 0;
    const css_token *tok;

    if (tokens == NULL)
        return;

    while ((tok = parserutils_vector_iterate(tokens, &ctx)) != NULL) {
        if (tok->idata != NULL)
            lwc_string_unref(tok->idata);
    }

    parserutils_vector_destroy(tokens);
}

static css_error css__tokens_append_ref(
    parserutils_vector *tokens,
    const css_token *token)
{
    parserutils_error perr;
    css_token copy = *token;

    if (copy.idata != NULL)
        copy.idata = lwc_string_ref(copy.idata);

    perr = parserutils_vector_append(tokens, &copy);
    if (perr != PARSERUTILS_OK) {
        if (copy.idata != NULL)
            lwc_string_unref(copy.idata);
        return css_error_from_parserutils_error(perr);
    }

    return CSS_OK;
}

static css_error css__tokens_append_vector_ref(
    parserutils_vector *out_tokens,
    const parserutils_vector *source)
{
    css_error error;
    int32_t ctx = 0;
    const css_token *token;

    while ((token = parserutils_vector_iterate(source, &ctx)) != NULL) {
        error = css__tokens_append_ref(out_tokens, token);
        if (error != CSS_OK)
            return error;
    }

    return CSS_OK;
}

static css_error css__var_value_add_reference(
    css_var_value *value,
    lwc_string *name)
{
    lwc_string **new_refs;
    uint32_t new_cap;

    for (uint32_t i = 0; i < value->ref_count; i++) {
        if (value->refs[i] == name)
            return CSS_OK;
    }

    if (value->ref_count >= value->ref_capacity) {
        new_cap = value->ref_capacity == 0 ? 4 : value->ref_capacity * 2;
        new_refs = realloc(value->refs, new_cap * sizeof(lwc_string *));
        if (new_refs == NULL)
            return CSS_NOMEM;

        value->refs = new_refs;
        value->ref_capacity = new_cap;
    }

    value->refs[value->ref_count++] = lwc_string_ref(name);
    return CSS_OK;
}

static css_error css__tokenise_value(
    lwc_string *raw,
    parserutils_vector **out,
    css_var_value *value)
{
    parserutils_inputstream *input = NULL;
    css_lexer *lexer = NULL;
    parserutils_vector *tokens = NULL;
    parserutils_error perr;
    css_error error;
    bool expect_var_name = false;

    perr = parserutils_vector_create(sizeof(css_token), 16, &tokens);
    if (perr != PARSERUTILS_OK)
        return css_error_from_parserutils_error(perr);

    perr = parserutils_inputstream_create("UTF-8", 0, NULL, &input);
    if (perr != PARSERUTILS_OK) {
        error = css_error_from_parserutils_error(perr);
        goto cleanup;
    }

    perr = parserutils_inputstream_append(input,
        (const uint8_t *)lwc_string_data(raw), lwc_string_length(raw));
    if (perr != PARSERUTILS_OK) {
        error = css_error_from_parserutils_error(perr);
        goto cleanup;
    }

    perr = parserutils_inputstream_append(input, NULL, 0);
    if (perr != PARSERUTILS_OK) {
        error = css_error_from_parserutils_error(perr);
        goto cleanup;
    }

    error = css__lexer_create(input, &lexer);
    if (error != CSS_OK)
        goto cleanup;

    while (1) {
        css_token *token = NULL;
        css_token copy;

        error = css__lexer_get_token(lexer, &token);
        if (error != CSS_OK)
            goto cleanup;

        if (token->type == CSS_TOKEN_EOF)
            break;

        copy = *token;
        if (copy.type < CSS_TOKEN_LAST_INTERN && copy.data.data != NULL) {
            lwc_error lerr = lwc_intern_string(
                (char *)copy.data.data, copy.data.len, &copy.idata);
            if (lerr != lwc_error_ok) {
                error = css_error_from_lwc_error(lerr);
                goto cleanup;
            }
        } else {
            copy.idata = NULL;
        }

        if (expect_var_name) {
            if (copy.type != CSS_TOKEN_S) {
                expect_var_name = false;

                if (value != NULL &&
                        (copy.type == CSS_TOKEN_IDENT ||
                        copy.type == CSS_TOKEN_CUSTOM_PROPERTY) &&
                        copy.idata != NULL) {
                    error = css__var_value_add_reference(value, copy.idata);
                    if (error != CSS_OK) {
                        if (copy.idata != NULL)
                            lwc_string_unref(copy.idata);
                        goto cleanup;
                    }
                }
            }
        }

        if (css__is_var_function_token(&copy))
            expect_var_name = true;

        perr = parserutils_vector_append(tokens, &copy);
        if (perr != PARSERUTILS_OK) {
            if (copy.idata != NULL)
                lwc_string_unref(copy.idata);
            error = css_error_from_parserutils_error(perr);
            goto cleanup;
        }
    }

    *out = tokens;
    tokens = NULL;
    error = CSS_OK;

cleanup:
    if (lexer != NULL)
        css__lexer_destroy(lexer);
    if (input != NULL)
        parserutils_inputstream_destroy(input);
    css__tokens_destroy(tokens);

    return error;
}

void css__stylesheet_var_token_cache_destroy(css_stylesheet *sheet)
{
    if (sheet == NULL)
        return;

    for (uint32_t i = 0; i < sheet->var_token_vector_l; i++)
        css__tokens_destroy(sheet->var_token_vector[i]);

    free(sheet->var_token_vector);
    sheet->var_token_vector = NULL;
    sheet->var_token_vector_l = 0;
}

static css_error css__stylesheet_var_tokens_get(
    css_stylesheet *sheet,
    uint32_t string_number,
    lwc_string *raw,
    const parserutils_vector **tokens)
{
    parserutils_vector **new_vector;
    uint32_t new_len;
    uint32_t index;
    css_error error;

    if (sheet == NULL || raw == NULL || tokens == NULL || string_number == 0)
        return CSS_BADPARM;

    index = string_number - 1;
    if (index >= sheet->var_token_vector_l) {
        new_len = sheet->var_token_vector_l;
        if (new_len == 0)
            new_len = 16;

        while (new_len <= index)
            new_len *= 2;

        new_vector = realloc(sheet->var_token_vector,
            new_len * sizeof(parserutils_vector *));
        if (new_vector == NULL)
            return CSS_NOMEM;

        memset(new_vector + sheet->var_token_vector_l, 0,
            (new_len - sheet->var_token_vector_l) *
            sizeof(parserutils_vector *));

        sheet->var_token_vector = new_vector;
        sheet->var_token_vector_l = new_len;
    }

    if (sheet->var_token_vector[index] == NULL) {
        error = css__tokenise_value(raw, &sheet->var_token_vector[index],
            NULL);
        if (error != CSS_OK)
            return error;
    }

    *tokens = sheet->var_token_vector[index];
    return CSS_OK;
}

static css_error css__var_value_create(
    lwc_string *raw,
    css_var_value **out)
{
    css_var_value *value;
    css_error error;

    value = calloc(1, sizeof(*value));
    if (value == NULL)
        return CSS_NOMEM;

    value->refcnt = 1;
    value->raw = lwc_string_ref(raw);

    error = css__tokenise_value(raw, &value->tokens, value);
    if (error != CSS_OK) {
        for (uint32_t i = 0; i < value->ref_count; i++)
            lwc_string_unref(value->refs[i]);
        free(value->refs);
        lwc_string_unref(value->raw);
        free(value);
        return error;
    }

    *out = value;
    return CSS_OK;
}

static css_var_value *css__var_value_ref(css_var_value *value)
{
    if (value != NULL)
        value->refcnt++;

    return value;
}

static void css__var_value_unref(css_var_value *value)
{
    if (value == NULL)
        return;

    if (--value->refcnt > 0)
        return;

    css__tokens_destroy(value->tokens);
    for (uint32_t i = 0; i < value->ref_count; i++)
        lwc_string_unref(value->refs[i]);
    free(value->refs);
    lwc_string_unref(value->raw);
    free(value);
}

static css_var_context *css__variables_ctx_ref(css_var_context *ctx)
{
    if (ctx != NULL)
        ctx->refcnt++;

    return ctx;
}

static void css__variables_ctx_rebuild_lookup(css_var_context *ctx)
{
    css_var_lookup *new_lookup;
    uint32_t capacity;

    if (ctx == NULL)
        return;

    if (ctx->count == 0) {
        free(ctx->lookup);
        ctx->lookup = NULL;
        ctx->lookup_capacity = 0;
        ctx->lookup_valid = true;
        return;
    }

    capacity = css__var_lookup_capacity_for_count(ctx->count);
    if (capacity != ctx->lookup_capacity) {
        new_lookup = calloc(capacity, sizeof(css_var_lookup));
        if (new_lookup == NULL) {
            ctx->lookup_valid = false;
            return;
        }

        free(ctx->lookup);
        ctx->lookup = new_lookup;
        ctx->lookup_capacity = capacity;
    } else {
        memset(ctx->lookup, 0, capacity * sizeof(css_var_lookup));
    }

    for (uint32_t i = 0; i < ctx->count; i++) {
        css__var_lookup_insert(ctx->lookup, ctx->lookup_capacity,
            ctx->entries[i].name, i);
    }

    ctx->lookup_valid = true;
}

static void css__variables_ctx_update_lookup_after_append(
    css_var_context *ctx,
    uint32_t index)
{
    uint32_t capacity;

    if (ctx == NULL)
        return;

    capacity = css__var_lookup_capacity_for_count(ctx->count);
    if (ctx->lookup_valid &&
            ctx->lookup != NULL &&
            capacity == ctx->lookup_capacity) {
        css__var_lookup_insert(ctx->lookup, ctx->lookup_capacity,
            ctx->entries[index].name, index);
        return;
    }

    css__variables_ctx_rebuild_lookup(ctx);
}

static void css__variables_ctx_clear_cycle_cache(css_var_context *ctx)
{
    if (ctx == NULL)
        return;

    for (uint32_t i = 0; i < ctx->cyclic_count; i++)
        lwc_string_unref(ctx->cyclic_names[i]);

    free(ctx->cyclic_names);
    ctx->cyclic_names = NULL;
    ctx->cyclic_count = 0;
    ctx->cyclic_capacity = 0;
}

static void css__variables_ctx_clear_resolved_cache(css_var_context *ctx)
{
    if (ctx == NULL)
        return;

    for (uint32_t i = 0; i < ctx->resolved_count; i++) {
        lwc_string_unref(ctx->resolved[i].name);
        css__tokens_destroy(ctx->resolved[i].tokens);
    }

    free(ctx->resolved);
    free(ctx->resolved_lookup);
    ctx->resolved = NULL;
    ctx->resolved_count = 0;
    ctx->resolved_capacity = 0;
    ctx->resolved_lookup = NULL;
    ctx->resolved_lookup_capacity = 0;
    ctx->resolved_lookup_valid = true;
}

static void css__variables_ctx_invalidate_cycles(css_var_context *ctx)
{
    css__variables_ctx_clear_cycle_cache(ctx);
    css__variables_ctx_clear_resolved_cache(ctx);
    if (ctx != NULL)
        ctx->cycles_valid = false;
}

static bool css__variables_ctx_find_name(
    const css_var_context *ctx,
    lwc_string *name,
    uint32_t *index)
{
    if (ctx == NULL)
        return false;

    if (ctx->lookup_valid) {
        if (ctx->lookup != NULL) {
            return css__var_lookup_find(
                ctx->lookup, ctx->lookup_capacity, name, index);
        }
        if (ctx->count == 0)
            return false;
    }

    for (uint32_t i = 0; i < ctx->count; i++) {
        if (ctx->entries[i].name == name) {
            if (index != NULL)
                *index = i;
            return true;
        }
    }

    return false;
}

static css_var_value *css__variables_ctx_get_parsed(
    const css_var_context *ctx,
    lwc_string *name)
{
    uint32_t index;

    if (ctx == NULL)
        return NULL;

    if (css__variables_ctx_find_name(ctx, name, &index) == false)
        return ctx->parent != NULL
            ? css__variables_ctx_get_parsed(ctx->parent, name)
            : NULL;

    return ctx->entries[index].value;
}

typedef struct css_var_binding {
    const css_var_entry *entry;
} css_var_binding;

typedef struct css_var_binding_list {
    css_var_binding *items;
    uint32_t count;
    uint32_t capacity;
    css_var_lookup *lookup;
    uint32_t lookup_capacity;
    bool lookup_valid;
} css_var_binding_list;

static void css__var_binding_list_rebuild_lookup(css_var_binding_list *list)
{
    css_var_lookup *new_lookup;
    uint32_t capacity;

    if (list == NULL)
        return;

    if (list->count == 0) {
        free(list->lookup);
        list->lookup = NULL;
        list->lookup_capacity = 0;
        list->lookup_valid = true;
        return;
    }

    capacity = css__var_lookup_capacity_for_count(list->count);
    if (capacity != list->lookup_capacity) {
        new_lookup = calloc(capacity, sizeof(css_var_lookup));
        if (new_lookup == NULL) {
            list->lookup_valid = false;
            return;
        }

        free(list->lookup);
        list->lookup = new_lookup;
        list->lookup_capacity = capacity;
    } else {
        memset(list->lookup, 0, capacity * sizeof(css_var_lookup));
    }

    for (uint32_t i = 0; i < list->count; i++) {
        css__var_lookup_insert(list->lookup, list->lookup_capacity,
            list->items[i].entry->name, i);
    }

    list->lookup_valid = true;
}

static void css__var_binding_list_update_lookup_after_append(
    css_var_binding_list *list,
    uint32_t index)
{
    uint32_t capacity;

    if (list == NULL)
        return;

    capacity = css__var_lookup_capacity_for_count(list->count);
    if (list->lookup_valid &&
            list->lookup != NULL &&
            capacity == list->lookup_capacity) {
        css__var_lookup_insert(list->lookup, list->lookup_capacity,
            list->items[index].entry->name, index);
        return;
    }

    css__var_binding_list_rebuild_lookup(list);
}

static void css__var_binding_list_destroy(css_var_binding_list *list)
{
    if (list == NULL)
        return;

    free(list->lookup);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static bool css__var_binding_list_find_name(
    const css_var_binding_list *list,
    lwc_string *name,
    uint32_t *index)
{
    if (list == NULL)
        return false;

    if (list->lookup_valid) {
        if (list->lookup != NULL) {
            return css__var_lookup_find(
                list->lookup, list->lookup_capacity, name, index);
        }
        if (list->count == 0)
            return false;
    }

    for (uint32_t i = 0; i < list->count; i++) {
        if (list->items[i].entry->name == name) {
            if (index != NULL)
                *index = i;
            return true;
        }
    }

    return false;
}

static css_error css__var_binding_list_append(
    css_var_binding_list *list,
    const css_var_entry *entry)
{
    css_var_binding *new_items;
    uint32_t new_cap;
    uint32_t index;

    if (css__var_binding_list_find_name(list, entry->name, NULL))
        return CSS_OK;

    if (list->count >= list->capacity) {
        new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        new_items = realloc(list->items, new_cap * sizeof(css_var_binding));
        if (new_items == NULL)
            return CSS_NOMEM;

        list->items = new_items;
        list->capacity = new_cap;
    }

    index = list->count++;
    list->items[index].entry = entry;
    css__var_binding_list_update_lookup_after_append(list, index);
    return CSS_OK;
}

static void css__variables_ctx_rebuild_resolved_lookup(css_var_context *ctx)
{
    css_var_lookup *new_lookup;
    uint32_t capacity;

    if (ctx == NULL)
        return;

    if (ctx->resolved_count == 0) {
        free(ctx->resolved_lookup);
        ctx->resolved_lookup = NULL;
        ctx->resolved_lookup_capacity = 0;
        ctx->resolved_lookup_valid = true;
        return;
    }

    capacity = css__var_lookup_capacity_for_count(ctx->resolved_count);
    if (capacity != ctx->resolved_lookup_capacity) {
        new_lookup = calloc(capacity, sizeof(css_var_lookup));
        if (new_lookup == NULL) {
            ctx->resolved_lookup_valid = false;
            return;
        }

        free(ctx->resolved_lookup);
        ctx->resolved_lookup = new_lookup;
        ctx->resolved_lookup_capacity = capacity;
    } else {
        memset(ctx->resolved_lookup, 0,
            capacity * sizeof(css_var_lookup));
    }

    for (uint32_t i = 0; i < ctx->resolved_count; i++) {
        css__var_lookup_insert(ctx->resolved_lookup,
            ctx->resolved_lookup_capacity, ctx->resolved[i].name, i);
    }

    ctx->resolved_lookup_valid = true;
}

static void css__variables_ctx_update_resolved_lookup_after_append(
    css_var_context *ctx,
    uint32_t index)
{
    uint32_t capacity;

    if (ctx == NULL)
        return;

    capacity = css__var_lookup_capacity_for_count(ctx->resolved_count);
    if (ctx->resolved_lookup_valid &&
            ctx->resolved_lookup != NULL &&
            capacity == ctx->resolved_lookup_capacity) {
        css__var_lookup_insert(ctx->resolved_lookup,
            ctx->resolved_lookup_capacity, ctx->resolved[index].name,
            index);
        return;
    }

    css__variables_ctx_rebuild_resolved_lookup(ctx);
}

static bool css__variables_ctx_find_resolved_index(
    const css_var_context *ctx,
    lwc_string *name,
    uint32_t *index)
{
    if (ctx == NULL)
        return false;

    if (ctx->resolved_lookup_valid) {
        if (ctx->resolved_lookup != NULL) {
            return css__var_lookup_find(ctx->resolved_lookup,
                ctx->resolved_lookup_capacity, name, index);
        }
        if (ctx->resolved_count == 0)
            return false;
    }

    for (uint32_t i = 0; i < ctx->resolved_count; i++) {
        if (ctx->resolved[i].name == name) {
            if (index != NULL)
                *index = i;
            return true;
        }
    }

    return false;
}

static bool css__variables_ctx_find_resolved(
    const css_var_context *ctx,
    lwc_string *name,
    const parserutils_vector **tokens)
{
    uint32_t index;

    if (css__variables_ctx_find_resolved_index(ctx, name, &index) == false)
        return false;

    if (tokens != NULL)
        *tokens = ctx->resolved[index].tokens;

    return true;
}

static css_error css__variables_ctx_store_resolved(
    css_var_context *ctx,
    lwc_string *name,
    parserutils_vector *tokens)
{
    css_var_resolved *new_resolved;
    uint32_t index;

    if (css__variables_ctx_find_resolved_index(ctx, name, &index)) {
        css__tokens_destroy(ctx->resolved[index].tokens);
        ctx->resolved[index].tokens = tokens;
        return CSS_OK;
    }

    if (ctx->resolved_count >= ctx->resolved_capacity) {
        uint32_t new_cap = ctx->resolved_capacity == 0
            ? VAR_CTX_INITIAL_CAPACITY
            : ctx->resolved_capacity * 2;

        new_resolved = realloc(ctx->resolved,
            new_cap * sizeof(css_var_resolved));
        if (new_resolved == NULL)
            return CSS_NOMEM;

        ctx->resolved = new_resolved;
        ctx->resolved_capacity = new_cap;
    }

    index = ctx->resolved_count++;
    ctx->resolved[index].name = lwc_string_ref(name);
    ctx->resolved[index].tokens = tokens;
    css__variables_ctx_update_resolved_lookup_after_append(ctx, index);

    return CSS_OK;
}

static css_var_context *css__variables_ctx_resolved_cache_owner(
    css_var_context *ctx)
{
    /* Empty inherited contexts resolve exactly like their nearest ancestor. */
    while (ctx != NULL && ctx->count == 0 && ctx->parent != NULL)
        ctx = ctx->parent;

    return ctx;
}

static css_error css__variables_ctx_collect_effective(
    const css_var_context *ctx,
    css_var_binding_list *list)
{
    for (; ctx != NULL; ctx = ctx->parent) {
        for (uint32_t i = 0; i < ctx->count; i++) {
            css_error error = css__var_binding_list_append(
                list, &ctx->entries[i]);
            if (error != CSS_OK)
                return error;
        }
    }

    return CSS_OK;
}

static css_error css__variables_ctx_mark_cyclic(
    css_var_context *ctx,
    lwc_string *name)
{
    lwc_string **new_names;
    uint32_t new_cap;

    for (uint32_t i = 0; i < ctx->cyclic_count; i++) {
        if (ctx->cyclic_names[i] == name)
            return CSS_OK;
    }

    if (ctx->cyclic_count >= ctx->cyclic_capacity) {
        new_cap = ctx->cyclic_capacity == 0 ? 4 : ctx->cyclic_capacity * 2;
        new_names = realloc(ctx->cyclic_names,
            new_cap * sizeof(lwc_string *));
        if (new_names == NULL)
            return CSS_NOMEM;

        ctx->cyclic_names = new_names;
        ctx->cyclic_capacity = new_cap;
    }

    ctx->cyclic_names[ctx->cyclic_count++] = lwc_string_ref(name);
    return CSS_OK;
}

enum {
    CSS_VAR_CYCLE_UNVISITED = 0,
    CSS_VAR_CYCLE_VISITING = 1,
    CSS_VAR_CYCLE_DONE = 2
};

typedef struct css_var_cycle_search {
    css_var_context *ctx;
    const css_var_binding_list *bindings;
    uint8_t *state;
    uint32_t *stack;
    uint32_t stack_len;
} css_var_cycle_search;

static css_error css__variables_ctx_visit_cycles(
    css_var_cycle_search *search,
    uint32_t index)
{
    const css_var_entry *entry = search->bindings->items[index].entry;

    search->state[index] = CSS_VAR_CYCLE_VISITING;
    search->stack[search->stack_len++] = index;

    for (uint32_t i = 0; i < entry->value->ref_count; i++) {
        uint32_t dep;

        if (css__var_binding_list_find_name(search->bindings,
                entry->value->refs[i], &dep) == false) {
            continue;
        }

        if (search->state[dep] == CSS_VAR_CYCLE_VISITING) {
            bool mark = false;

            for (uint32_t j = 0; j < search->stack_len; j++) {
                if (search->stack[j] == dep)
                    mark = true;
                if (mark) {
                    css_error error = css__variables_ctx_mark_cyclic(
                        search->ctx,
                        search->bindings->items[search->stack[j]].entry->name);
                    if (error != CSS_OK)
                        return error;
                }
            }
        } else if (search->state[dep] == CSS_VAR_CYCLE_UNVISITED) {
            css_error error = css__variables_ctx_visit_cycles(search, dep);
            if (error != CSS_OK)
                return error;
        }
    }

    search->stack_len--;
    search->state[index] = CSS_VAR_CYCLE_DONE;

    return CSS_OK;
}

static css_error css__variables_ctx_ensure_cycles(css_var_context *ctx)
{
    css_var_binding_list bindings;
    css_var_cycle_search search;
    css_error error = CSS_OK;

    if (ctx == NULL || ctx->cycles_valid)
        return CSS_OK;

    memset(&bindings, 0, sizeof(bindings));
    memset(&search, 0, sizeof(search));
    search.ctx = ctx;
    search.bindings = &bindings;

    css__variables_ctx_clear_cycle_cache(ctx);

    error = css__variables_ctx_collect_effective(ctx, &bindings);
    if (error != CSS_OK)
        goto cleanup;

    if (bindings.count == 0) {
        ctx->cycles_valid = true;
        goto cleanup;
    }

    search.state = calloc(bindings.count, sizeof(uint8_t));
    search.stack = calloc(bindings.count, sizeof(uint32_t));
    if (search.state == NULL || search.stack == NULL) {
        error = CSS_NOMEM;
        goto cleanup;
    }

    for (uint32_t i = 0; i < bindings.count; i++) {
        if (search.state[i] == CSS_VAR_CYCLE_UNVISITED) {
            error = css__variables_ctx_visit_cycles(&search, i);
            if (error != CSS_OK)
                goto cleanup;
        }
    }

    ctx->cycles_valid = true;

cleanup:
    free(search.state);
    free(search.stack);
    css__var_binding_list_destroy(&bindings);

    return error;
}

static css_error css__variables_ctx_name_is_cyclic(
    css_var_context *ctx,
    lwc_string *name,
    bool *cyclic)
{
    css_error error;

    *cyclic = false;

    if (ctx == NULL)
        return CSS_OK;

    if (ctx->count == 0 && ctx->parent != NULL)
        return css__variables_ctx_name_is_cyclic(ctx->parent, name, cyclic);

    error = css__variables_ctx_ensure_cycles(ctx);
    if (error != CSS_OK)
        return error;

    for (uint32_t i = 0; i < ctx->cyclic_count; i++) {
        if (ctx->cyclic_names[i] == name) {
            *cyclic = true;
            break;
        }
    }

    return CSS_OK;
}

static css_error css__resolve_cached_var_value(
    css_var_context *var_ctx,
    lwc_string *name,
    css_var_value *value,
    parserutils_vector *out_tokens,
    int depth)
{
    const parserutils_vector *cached = NULL;
    parserutils_vector *resolved = NULL;
    css_var_context *cache_ctx;
    parserutils_error perr;
    css_error error;

    if (depth > CSS_VAR_MAX_DEPTH)
        return CSS_INVALID;

    cache_ctx = css__variables_ctx_resolved_cache_owner(var_ctx);

    if (css__variables_ctx_find_resolved(cache_ctx, name, &cached)) {
        return css__tokens_append_vector_ref(out_tokens, cached);
    }

    perr = parserutils_vector_create(sizeof(css_token), 16, &resolved);
    if (perr != PARSERUTILS_OK)
        return css_error_from_parserutils_error(perr);

    error = css__resolve_token_vector(value->tokens, var_ctx, resolved,
        depth);
    if (error != CSS_OK)
        goto cleanup;

    error = css__variables_ctx_store_resolved(cache_ctx, name, resolved);
    if (error == CSS_OK) {
        cached = resolved;
        resolved = NULL;
        error = css__tokens_append_vector_ref(out_tokens, cached);
    } else if (error == CSS_NOMEM) {
        error = css__tokens_append_vector_ref(out_tokens, resolved);
    }

cleanup:
    css__tokens_destroy(resolved);
    return error;
}

static bool css__token_is_char(const css_token *token, char c)
{
    size_t len = 0;
    const char *data;

    if (token->type != CSS_TOKEN_CHAR)
        return false;

    data = css__var_token_get_str(token, &len);
    return len == 1 && data[0] == c;
}

/**
 * Handle a `var(` ... `)` sequence from the token stream.
 */
static css_error css__handle_var_function(
    const parserutils_vector *value_tokens,
    int32_t *token_ctx,
    css_var_context *var_ctx,
    parserutils_vector *out_tokens,
    int depth)
{
    css_error error;
    parserutils_error perr;
    const css_token *t = NULL;
    lwc_string *var_name = NULL;
    css_var_value *resolved_val = NULL;
    parserutils_vector *fallback_tokens = NULL;
    bool has_fallback = false;

    /* Skip whitespace */
    while (1) {
        t = parserutils_vector_iterate(value_tokens, token_ctx);
        if (t == NULL) { error = CSS_INVALID; goto cleanup; }
        if (t->type != CSS_TOKEN_S) break;
    }

    if (t->type != CSS_TOKEN_IDENT && t->type != CSS_TOKEN_CUSTOM_PROPERTY) {
        CSS_LOG(DEBUG, "var() missing ident or custom property name: got token type %d (data: %.*s)",
                t->type, (int)t->data.len,
                t->data.data != NULL ? (char *)t->data.data : "");
        error = CSS_INVALID; goto cleanup;
    }
    if (t->idata != NULL) {
        var_name = lwc_string_ref(t->idata);
    } else {
        lwc_error lerr = lwc_intern_string(
            (char *)t->data.data, t->data.len, &var_name);
        if (lerr != lwc_error_ok) {
            error = css_error_from_lwc_error(lerr); goto cleanup;
        }
    }

    /* Skip whitespace */
    while (1) {
        t = parserutils_vector_iterate(value_tokens, token_ctx);
        if (t == NULL) { error = CSS_INVALID; goto cleanup; }
        if (t->type != CSS_TOKEN_S) break;
    }

    if (css__token_is_char(t, ',')) {
        has_fallback = true;
        perr = parserutils_vector_create(sizeof(css_token), 16, &fallback_tokens);
        if (perr != PARSERUTILS_OK) {
            error = css_error_from_parserutils_error(perr); goto cleanup;
        }

        while (1) {
            t = parserutils_vector_iterate(value_tokens, token_ctx);
            if (t == NULL) { error = CSS_INVALID; goto cleanup; }
            if (t->type != CSS_TOKEN_S) break;
        }

        int parens = 1;
        while (1) {
            if (t == NULL) { error = CSS_INVALID; goto cleanup; }

            if (css__token_is_char(t, '(')) {
                parens++;
            } else if (css__token_is_char(t, ')')) {
                parens--;
            } else if (t->type == CSS_TOKEN_FUNCTION) {
                parens++;
            }

            if (parens == 0) break;

            error = css__tokens_append_ref(fallback_tokens, t);
            if (error != CSS_OK) goto cleanup;

            t = parserutils_vector_iterate(value_tokens, token_ctx);
        }
    } else if (css__token_is_char(t, ')')) {
        has_fallback = false;
    } else {
        CSS_LOG(DEBUG, "var() missing closing paren");
        error = CSS_INVALID; goto cleanup;
    }

    resolved_val = css__variables_ctx_get_parsed(var_ctx, var_name);

    if (resolved_val != NULL) {
        size_t initial_len = 0;
        bool cyclic = false;
        parserutils_vector_get_length(out_tokens, &initial_len);

        error = css__variables_ctx_name_is_cyclic(var_ctx, var_name, &cyclic);
        if (error != CSS_OK)
            goto cleanup;

        if (cyclic) {
            CSS_LOG(DEBUG, "var() reference to cyclic custom property '%.*s'",
                (int)lwc_string_length(var_name), lwc_string_data(var_name));
            error = CSS_INVALID;
        } else if (initial_len == 0) {
            /* Start-of-output expansion matches the cached token shape. */
            error = css__resolve_cached_var_value(var_ctx, var_name,
                resolved_val, out_tokens, depth + 1);
        } else {
            error = css__resolve_token_vector(resolved_val->tokens, var_ctx,
                out_tokens, depth + 1);
        }

        if (error != CSS_OK) {
            /* If resolving failed (e.g., due to a cycle), rollback the tokens we appended */
            css__tokens_truncate(out_tokens, initial_len);
        }
    } else {
        error = CSS_INVALID;
    }

    if (error != CSS_OK) {
        if (has_fallback) {
            size_t initial_len = 0;

            if (error != CSS_INVALID)
                goto cleanup;

            parserutils_vector_get_length(out_tokens, &initial_len);

            error = css__resolve_token_vector(
                fallback_tokens, var_ctx, out_tokens, depth + 1);

            if (error != CSS_OK) {
                css__tokens_truncate(out_tokens, initial_len);
                goto cleanup;
            }
        } else {
            goto cleanup;
        }
    }

    error = CSS_OK;
cleanup:
    if (var_name != NULL) lwc_string_unref(var_name);
    css__tokens_destroy(fallback_tokens);
    return error;
}

static css_error css__resolve_token_vector(
    const parserutils_vector *value_tokens,
    css_var_context *var_ctx,
    parserutils_vector *out_tokens,
    int depth)
{
    css_error error = CSS_OK;
    int32_t token_ctx = 0;
    const css_token *token;

    if (depth > CSS_VAR_MAX_DEPTH) return CSS_INVALID;

    while ((token = parserutils_vector_iterate(value_tokens, &token_ctx)) != NULL) {
        if (css__is_var_function_token(token)) {
            error = css__handle_var_function(value_tokens, &token_ctx,
                var_ctx, out_tokens, depth);
            if (error != CSS_OK) return error;
            continue;
        }

        size_t vec_len = 0;
        parserutils_vector_get_length(out_tokens, &vec_len);
        if (token->type == CSS_TOKEN_S && vec_len == 0) {
            continue;
        }

        error = css__tokens_append_ref(out_tokens, token);
        if (error != CSS_OK) return error;
    }

    return CSS_OK;
}

css_error css__variables_ctx_create(css_var_context **out)
{
    css_var_context *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return CSS_NOMEM;

    ctx->refcnt = 1;
    ctx->cycles_valid = true;
    ctx->lookup_valid = true;
    ctx->resolved_lookup_valid = true;
    *out = ctx;
    return CSS_OK;
}

css_error css__variables_ctx_clone(
    const css_var_context *src,
    css_var_context **out)
{
    css_var_binding_list bindings;
    css_var_context *ctx;
    css_error error;

    memset(&bindings, 0, sizeof(bindings));

    error = css__variables_ctx_create(&ctx);
    if (error != CSS_OK)
        return error;

    if (src != NULL) {
        error = css__variables_ctx_collect_effective(src, &bindings);
        if (error != CSS_OK) {
            css__var_binding_list_destroy(&bindings);
            css__variables_ctx_destroy(ctx);
            return error;
        }
    }

    if (bindings.count > 0) {
        ctx->entries = malloc(bindings.count * sizeof(css_var_entry));
        if (ctx->entries == NULL) {
            css__var_binding_list_destroy(&bindings);
            css__variables_ctx_destroy(ctx);
            return CSS_NOMEM;
        }

        ctx->capacity = bindings.count;
        ctx->count = bindings.count;

        for (uint32_t i = 0; i < bindings.count; i++) {
            const css_var_entry *entry = bindings.items[i].entry;

            ctx->entries[i].name = lwc_string_ref(entry->name);
            ctx->entries[i].value = css__var_value_ref(entry->value);
            ctx->entries[i].specificity = entry->specificity;
            ctx->entries[i].origin = entry->origin;
            ctx->entries[i].important = entry->important;
            ctx->entries[i].cascaded = entry->cascaded;
        }

        ctx->cycles_valid = false;
        css__variables_ctx_rebuild_lookup(ctx);
    }

    css__var_binding_list_destroy(&bindings);
    *out = ctx;
    return CSS_OK;
}

css_error css__variables_ctx_clone_inherited(
    css_var_context *src,
    css_var_context **out)
{
    css_var_context *ctx;
    css_error error;

    error = css__variables_ctx_create(&ctx);
    if (error != CSS_OK)
        return error;

    ctx->parent = css__variables_ctx_ref(src);

    *out = ctx;
    return CSS_OK;
}

void css__variables_ctx_destroy(css_var_context *ctx)
{
    if (ctx == NULL)
        return;

    if (--ctx->refcnt > 0)
        return;

    for (uint32_t i = 0; i < ctx->count; i++) {
        lwc_string_unref(ctx->entries[i].name);
        css__var_value_unref(ctx->entries[i].value);
    }

    css__variables_ctx_clear_cycle_cache(ctx);
    css__variables_ctx_clear_resolved_cache(ctx);
    css__variables_ctx_destroy(ctx->parent);
    free(ctx->lookup);
    free(ctx->entries);
    free(ctx);
}

css_error css__variables_ctx_set(css_var_context *ctx,
    lwc_string *name, lwc_string *value)
{
    css_var_value *parsed_value;
    css_error error;
    uint32_t index;

    error = css__var_value_create(value, &parsed_value);
    if (error != CSS_OK)
        return error;

    /* Check for existing entry (pointer comparison — interned strings) */
    if (css__variables_ctx_find_name(ctx, name, &index)) {
        /* Replace value */
        css__var_value_unref(ctx->entries[index].value);
        ctx->entries[index].value = parsed_value;
        ctx->entries[index].origin = CSS_ORIGIN_AUTHOR;
        ctx->entries[index].specificity = 0;
        ctx->entries[index].important = false;
        ctx->entries[index].cascaded = true;
        css__variables_ctx_invalidate_cycles(ctx);
        return CSS_OK;
    }

    /* New entry — grow if needed */
    if (ctx->count >= ctx->capacity) {
        uint32_t new_cap = ctx->capacity == 0
            ? VAR_CTX_INITIAL_CAPACITY
            : ctx->capacity * 2;
        css_var_entry *new_entries = realloc(ctx->entries,
            new_cap * sizeof(css_var_entry));
        if (new_entries == NULL) {
            css__var_value_unref(parsed_value);
            return CSS_NOMEM;
        }
        ctx->entries = new_entries;
        ctx->capacity = new_cap;
    }

    index = ctx->count++;
    ctx->entries[index].name = lwc_string_ref(name);
    ctx->entries[index].value = parsed_value;
    ctx->entries[index].origin = CSS_ORIGIN_AUTHOR;
    ctx->entries[index].specificity = 0;
    ctx->entries[index].important = false;
    ctx->entries[index].cascaded = true;
    css__variables_ctx_update_lookup_after_append(ctx, index);
    css__variables_ctx_invalidate_cycles(ctx);

    return CSS_OK;
}

static bool css__variable_decl_outranks(
    const css_var_entry *existing,
    css_origin origin,
    uint32_t specificity,
    bool important)
{
    if (existing->cascaded == false)
        return true;

    if (existing->origin < origin) {
        if (existing->important == false ||
                existing->origin != CSS_ORIGIN_USER) {
            return true;
        }
    } else if (existing->origin == origin) {
        if (existing->origin == CSS_ORIGIN_UA) {
            return specificity >= existing->specificity;
        } else if (existing->important == false && important) {
            return true;
        } else if (existing->important && important == false) {
            return false;
        } else {
            return specificity >= existing->specificity;
        }
    } else {
        if (origin == CSS_ORIGIN_USER && important)
            return true;
    }

    return false;
}

static void css__variables_ctx_update_metadata(
    css_var_entry *entry,
    css_origin origin,
    uint32_t specificity,
    bool important)
{
    entry->origin = origin;
    entry->specificity = specificity;
    entry->important = important;
    entry->cascaded = true;
}

css_error css__variables_ctx_cascade(
    css_var_context *ctx,
    lwc_string *name,
    lwc_string *value,
    css_origin origin,
    uint32_t specificity,
    bool important)
{
    css_var_value *parsed_value = NULL;
    css_error error;
    uint32_t index;

    if (css__variables_ctx_find_name(ctx, name, &index)) {
        if (css__variable_decl_outranks(&ctx->entries[index],
                origin, specificity, important)) {
            error = css__var_value_create(value, &parsed_value);
            if (error != CSS_OK)
                return error;

            css__var_value_unref(ctx->entries[index].value);
            ctx->entries[index].value = parsed_value;
            css__variables_ctx_update_metadata(&ctx->entries[index],
                origin, specificity, important);
            css__variables_ctx_invalidate_cycles(ctx);
        }
        return CSS_OK;
    }

    if (ctx->count >= ctx->capacity) {
        uint32_t new_cap = ctx->capacity == 0
            ? VAR_CTX_INITIAL_CAPACITY
            : ctx->capacity * 2;
        css_var_entry *new_entries = realloc(ctx->entries,
            new_cap * sizeof(css_var_entry));
        if (new_entries == NULL) {
            return CSS_NOMEM;
        }
        ctx->entries = new_entries;
        ctx->capacity = new_cap;
    }

    error = css__var_value_create(value, &parsed_value);
    if (error != CSS_OK)
        return error;

    index = ctx->count++;
    ctx->entries[index].name = lwc_string_ref(name);
    ctx->entries[index].value = parsed_value;
    css__variables_ctx_update_metadata(&ctx->entries[index],
        origin, specificity, important);
    css__variables_ctx_update_lookup_after_append(ctx, index);
    css__variables_ctx_invalidate_cycles(ctx);

    return CSS_OK;
}

lwc_string *css__variables_ctx_get(const css_var_context *ctx,
    lwc_string *name)
{
    uint32_t index;

    if (ctx == NULL)
        return NULL;

    if (css__variables_ctx_find_name(ctx, name, &index))
        return ctx->entries[index].value->raw;

    return css__variables_ctx_get(ctx->parent, name);
}

static css_error css__cascade_resolved_style(
    css_style *result_style,
    bool important,
    css_select_state *state)
{
    css_style rs = *result_style;

    while (rs.used > 0) {
        css_code_t result_opv = *rs.bytecode;
        opcode_t result_op;

        advance_bytecode(&rs, sizeof(result_opv));
        result_op = getOpcode(result_opv);

        if (important)
            result_opv |= FLAG_IMPORTANT;

        if (result_op < CSS_N_PROPERTIES) {
            css_error error = prop_dispatch[result_op].cascade(
                result_opv, &rs, state);
            if (error != CSS_OK)
                return error;
        }
    }

    return CSS_OK;
}

static css_error css__cascade_unset_property(
    const struct css_prop_entry *entry,
    css_stylesheet *sheet,
    bool important,
    css_select_state *state)
{
    css_error error;
    parserutils_error perr;
    parserutils_vector *tokens = NULL;
    css_style *result_style = NULL;
    css_language resolve_lang;
    css_token unset_token;
    css_token eof_token;
    lwc_string *unset = NULL;
    int32_t token_ctx = 0;

    if (entry == NULL)
        return CSS_INVALID;

    error = css_error_from_lwc_error(
        lwc_intern_string("unset", SLEN("unset"), &unset));
    if (error != CSS_OK)
        return error;

    perr = parserutils_vector_create(sizeof(css_token), 2, &tokens);
    if (perr != PARSERUTILS_OK) {
        lwc_string_unref(unset);
        return css_error_from_parserutils_error(perr);
    }

    unset_token.type = CSS_TOKEN_IDENT;
    unset_token.data.data = (uint8_t *)"unset";
    unset_token.data.len = SLEN("unset");
    unset_token.idata = unset;
    unset_token.col = 0;
    unset_token.line = 0;

    eof_token.type = CSS_TOKEN_EOF;
    eof_token.data.data = NULL;
    eof_token.data.len = 0;
    eof_token.idata = NULL;
    eof_token.col = 0;
    eof_token.line = 0;

    perr = parserutils_vector_append(tokens, &unset_token);
    if (perr != PARSERUTILS_OK) {
        error = css_error_from_parserutils_error(perr);
        goto cleanup;
    }

    perr = parserutils_vector_append(tokens, &eof_token);
    if (perr != PARSERUTILS_OK) {
        error = css_error_from_parserutils_error(perr);
        goto cleanup;
    }

    memset(&resolve_lang, 0, sizeof(resolve_lang));
    resolve_lang.sheet = sheet;
    resolve_lang.strings = sheet->propstrings;

    error = css__stylesheet_style_create(sheet, &result_style);
    if (error != CSS_OK)
        goto cleanup;

    error = entry->handler(&resolve_lang, tokens, &token_ctx, result_style);
    if (error == CSS_OK)
        error = css__cascade_resolved_style(result_style, important, state);

cleanup:
    if (result_style != NULL)
        css__stylesheet_style_destroy(result_style);
    if (tokens != NULL)
        parserutils_vector_destroy(tokens);
    if (unset != NULL)
        lwc_string_unref(unset);

    return error;
}

css_error css__resolve_var_property(
    lwc_string *prop_name,
    uint32_t raw_value_idx,
    lwc_string *raw_value,
    css_var_context *var_ctx,
    css_stylesheet *sheet,
    bool important,
    css_select_state *state)
{
    css_error error = CSS_OK;
    parserutils_error perr;
    parserutils_vector *tokens = NULL;
    const parserutils_vector *raw_tokens = NULL;
    css_style *result_style = NULL;
    const struct css_prop_entry *entry;

    /* Step 1: Look up the property handler by name via gperf */
    entry = css_prop_lookup(
        lwc_string_data(prop_name), lwc_string_length(prop_name));

    CSS_LOG(DEBUG, "VAR_RESOLVE: prop='%s' raw='%.*s'",
        lwc_string_data(prop_name),
        (int)lwc_string_length(raw_value), lwc_string_data(raw_value));

    if (entry == NULL) {
        CSS_LOG(DEBUG, "  Property '%s' not found in gperf table",
            lwc_string_data(prop_name));
        return CSS_INVALID;
    }

    /* Step 2: Resolve all var() references in cached value tokens */
    perr = parserutils_vector_create(sizeof(css_token), 16, &tokens);
    if (perr != PARSERUTILS_OK) {
        return css_error_from_parserutils_error(perr);
    }

    error = css__stylesheet_var_tokens_get(sheet, raw_value_idx,
        raw_value, &raw_tokens);
    if (error != CSS_OK)
        goto cleanup;

    error = css__resolve_token_vector(raw_tokens, var_ctx, tokens, 1);
    if (error != CSS_OK) {
        if (error == CSS_INVALID)
            error = css__cascade_unset_property(entry, sheet, important, state);
        goto cleanup;
    }

    {
        size_t vec_len = 0;
        parserutils_vector_get_length(tokens, &vec_len);
        if (vec_len == 0) {
            error = css__cascade_unset_property(entry, sheet, important, state);
            goto cleanup;
        }
    }

    /* Append EOF token for the parser */
    {
        css_token eof_token;
        eof_token.type = CSS_TOKEN_EOF;
        eof_token.data.data = NULL;
        eof_token.data.len = 0;
        eof_token.idata = NULL;
        eof_token.col = 0;
        eof_token.line = 0;
        perr = parserutils_vector_append(tokens, &eof_token);
        if (perr != PARSERUTILS_OK) {
            error = css_error_from_parserutils_error(perr);
            goto cleanup;
        }
    }

#if CSS_LOG_ENABLED
    /* Dump resolved tokens */
    {
        size_t dbg_len = 0;
        parserutils_vector_get_length(tokens, &dbg_len);
        CSS_LOG(DEBUG, "VAR_RESOLVE: resolved %zu tokens for '%s'",
            dbg_len, lwc_string_data(prop_name));
        int32_t dbg_ctx = 0;
        const css_token *dbg_t;
        while ((dbg_t = parserutils_vector_iterate(tokens, &dbg_ctx)) != NULL) {
            if (dbg_t->type == CSS_TOKEN_EOF) break;
            CSS_LOG(DEBUG, "  token[%d] type=%d data='%.*s'",
                dbg_ctx - 1, dbg_t->type,
                (int)(dbg_t->idata ? lwc_string_length(dbg_t->idata) : dbg_t->data.len),
                dbg_t->idata ? lwc_string_data(dbg_t->idata) : (const char*)dbg_t->data.data);
        }
    }
#endif
    
    /* Step 3: Call the handler to parse resolved tokens into bytecode */
    css_language resolve_lang;
    memset(&resolve_lang, 0, sizeof(resolve_lang));
    resolve_lang.sheet = sheet;
    resolve_lang.strings = sheet->propstrings;

    error = css__stylesheet_style_create(sheet, &result_style);
    if (error != CSS_OK) goto cleanup;

    int32_t token_ctx = 0;
    error = entry->handler(&resolve_lang, tokens, &token_ctx, result_style);
    CSS_LOG(DEBUG, "VAR_RESOLVE: handler for '%s' returned %d",
        lwc_string_data(prop_name), error);
    if (error != CSS_OK) {
        css__stylesheet_style_destroy(result_style);
        result_style = NULL;
        if (error == CSS_INVALID)
            error = css__cascade_unset_property(entry, sheet, important, state);
        goto cleanup;
    }

    /* Step 4: Cascade all OPVs in the resulting style.
     * Longhands produce 1 OPV; shorthands produce multiple. */
    error = css__cascade_resolved_style(result_style, important, state);
    if (error != CSS_OK)
        goto cleanup;

    css__stylesheet_style_destroy(result_style);
    result_style = NULL;

cleanup:
    if (result_style != NULL) css__stylesheet_style_destroy(result_style);
    if (tokens != NULL) {
        int32_t ctx = 0;
        const css_token *t;
        while ((t = parserutils_vector_iterate(tokens, &ctx)) != NULL) {
            if (t->idata != NULL) lwc_string_unref(t->idata);
        }
        parserutils_vector_destroy(tokens);
    }
    return error;
}
