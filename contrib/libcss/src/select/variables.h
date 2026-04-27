/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_select_variables_h_
#define css_select_variables_h_

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/errors.h>
#include <libcss/types.h>

/**
 * A single custom property binding: name -> value.
 * Entries point to ref-counted custom property values. Each value keeps the
 * original text plus cached tokens and cached var() references.
 */
typedef struct css_var_value css_var_value;
typedef struct css_var_lookup css_var_lookup;

typedef struct css_var_entry {
    lwc_string *name;   /* e.g. "--primary" */
    css_var_value *value;
    uint32_t specificity;
    css_origin origin;
    bool important;
    bool cascaded;      /* false when this is only an inherited value */
} css_var_entry;

/**
 * Per-element variable context.
 *
 * Local declarations are kept in append order with a best-effort lookup table
 * for fast name lookups. Inherited values are reached through parent contexts,
 * so child contexts do not copy every inherited custom property.
 */
typedef struct css_var_context {
    uint32_t refcnt;
    struct css_var_context *parent;
    css_var_entry *entries;
    uint32_t count;
    uint32_t capacity;
    css_var_lookup *lookup;
    uint32_t lookup_capacity;
    bool lookup_valid;
    lwc_string **cyclic_names;
    uint32_t cyclic_count;
    uint32_t cyclic_capacity;
    bool cycles_valid;
} css_var_context;

/**
 * Create an empty variable context.
 */
css_error css__variables_ctx_create(css_var_context **out);

/**
 * Clone a variable context (full deep copy with lwc_string_ref).
 * If src is NULL, creates an empty context.
 */
css_error css__variables_ctx_clone(const css_var_context *src, css_var_context **out);

/**
 * Clone a parent context for child inheritance. Values are preserved for
 * lookup, but cascade metadata is reset so any child declaration wins.
 */
css_error css__variables_ctx_clone_inherited(
    css_var_context *src,
    css_var_context **out);

/**
 * Destroy a variable context and unref all strings.
 */
void css__variables_ctx_destroy(css_var_context *ctx);

/**
 * Set a variable in the context. If name already exists, its value
 * is replaced. Both name and value are ref'd by this function.
 */
css_error css__variables_ctx_set(css_var_context *ctx,
    lwc_string *name, lwc_string *value);

/**
 * Cascade a variable declaration into the context.
 */
css_error css__variables_ctx_cascade(
    css_var_context *ctx,
    lwc_string *name,
    lwc_string *value,
    css_origin origin,
    uint32_t specificity,
    bool important);

/**
 * Look up a variable by name.
 * Returns the value lwc_string (not ref'd - caller must ref if keeping),
 * or NULL if not found.
 */
lwc_string *css__variables_ctx_get(const css_var_context *ctx,
    lwc_string *name);

/* --- Phase 5: var() Resolution --- */

/* Max recursion depth for nested var() references */
#define CSS_VAR_MAX_DEPTH 64

/* Forward declarations */
struct css_stylesheet;
struct css_style;
struct css_select_state;

/**
 * Resolve a deferred var() property: substitute all var() references
 * in the tokenized value text, re-parse through the property handler
 * (found via gperf lookup on prop_name), and cascade the result.
 * The raw value string index is used to cache tokenization per stylesheet.
 *
 * Works for both longhands and shorthands.
 *
 * Returns CSS_OK on success, CSS_INVALID if unresolvable.
 */
css_error css__resolve_var_property(
    lwc_string *prop_name,
    uint32_t raw_value_idx,
    lwc_string *raw_value,
    css_var_context *var_ctx,
    struct css_stylesheet *sheet,
    bool important,
    struct css_select_state *state);

/**
 * Destroy any tokenized deferred var() values cached on a stylesheet.
 */
void css__stylesheet_var_token_cache_destroy(struct css_stylesheet *sheet);

#endif
