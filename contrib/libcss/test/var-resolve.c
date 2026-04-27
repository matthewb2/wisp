/*
 * Regression tests for var() resolution timing during selection.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>

#include <libcss/computed.h>
#include <libcss/libcss.h>
#include <libcss/select.h>

#include "testutils.h"

typedef struct test_node {
    lwc_string *name;
    struct test_node *parent;
    void *libcss_node_data;
} test_node;

static bool test_node_name_matches(test_node *n, const css_qname *qname)
{
    bool match = false;

    if (n == NULL)
        return false;

    if (lwc_string_length(qname->name) == 1 &&
            lwc_string_data(qname->name)[0] == '*') {
        return true;
    }

    assert(lwc_string_caseless_isequal(qname->name, n->name,
        &match) == lwc_error_ok);

    return match;
}

static css_error resolve_url(void *pw, const char *base, lwc_string *rel, lwc_string **abs)
{
    UNUSED(pw);
    UNUSED(base);

    *abs = lwc_string_ref(rel);

    return CSS_OK;
}

static css_error node_name(void *pw, void *node, css_qname *qname)
{
    test_node *n = node;
    UNUSED(pw);

    qname->ns = NULL;
    qname->name = lwc_string_ref(n->name);

    return CSS_OK;
}

static css_error node_classes(void *pw, void *node, lwc_string ***classes, uint32_t *n_classes)
{
    UNUSED(pw);
    UNUSED(node);

    *classes = NULL;
    *n_classes = 0;

    return CSS_OK;
}

static css_error node_id(void *pw, void *node, lwc_string **id)
{
    UNUSED(pw);
    UNUSED(node);

    *id = NULL;

    return CSS_OK;
}

static css_error named_ancestor_node(void *pw, void *node, const css_qname *qname, void **ancestor)
{
    test_node *n = node;
    UNUSED(pw);

    *ancestor = NULL;
    for (n = n->parent; n != NULL; n = n->parent) {
        if (test_node_name_matches(n, qname)) {
            *ancestor = n;
            break;
        }
    }

    return CSS_OK;
}

static css_error named_parent_node(void *pw, void *node, const css_qname *qname, void **parent)
{
    test_node *n = node;
    UNUSED(pw);

    *parent = test_node_name_matches(n->parent, qname) ? n->parent : NULL;

    return CSS_OK;
}

static css_error named_sibling_node(void *pw, void *node, const css_qname *qname, void **sibling)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);

    *sibling = NULL;

    return CSS_OK;
}

static css_error named_generic_sibling_node(void *pw, void *node, const css_qname *qname, void **sibling)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);

    *sibling = NULL;

    return CSS_OK;
}

static css_error parent_node(void *pw, void *node, void **parent)
{
    test_node *n = node;
    UNUSED(pw);

    *parent = n->parent;

    return CSS_OK;
}

static css_error sibling_node(void *pw, void *node, void **sibling)
{
    UNUSED(pw);
    UNUSED(node);

    *sibling = NULL;

    return CSS_OK;
}

static css_error node_has_name(void *pw, void *node, const css_qname *qname, bool *match)
{
    test_node *n = node;
    UNUSED(pw);

    if (lwc_string_length(qname->name) == 1 &&
            lwc_string_data(qname->name)[0] == '*') {
        *match = true;
    } else {
        assert(lwc_string_caseless_isequal(qname->name, n->name, match) == lwc_error_ok);
    }

    return CSS_OK;
}

static css_error node_has_class(void *pw, void *node, lwc_string *name, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(name);

    *match = false;

    return CSS_OK;
}

static css_error node_has_id(void *pw, void *node, lwc_string *name, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(name);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute(void *pw, void *node, const css_qname *qname, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_equal(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_dashmatch(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_includes(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_prefix(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_suffix(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_has_attribute_substring(
    void *pw, void *node, const css_qname *qname, lwc_string *value, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);
    UNUSED(value);

    *match = false;

    return CSS_OK;
}

static css_error node_is_root(void *pw, void *node, bool *match)
{
    test_node *n = node;
    UNUSED(pw);

    *match = (n->parent == NULL);

    return CSS_OK;
}

static css_error node_count_siblings(void *pw, void *node, bool same_name, bool after, int32_t *count)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(same_name);
    UNUSED(after);

    *count = 0;

    return CSS_OK;
}

static css_error node_is_empty(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = true;

    return CSS_OK;
}

static css_error node_is_link(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_visited(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_hover(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_active(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_focus(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_enabled(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_disabled(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_checked(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_target(void *pw, void *node, bool *match)
{
    UNUSED(pw);
    UNUSED(node);

    *match = false;

    return CSS_OK;
}

static css_error node_is_lang(void *pw, void *node, lwc_string *lang, bool *match)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(lang);

    *match = false;

    return CSS_OK;
}

static css_error node_presentational_hint(void *pw, void *node, uint32_t *nhints, css_hint **hints)
{
    UNUSED(pw);
    UNUSED(node);

    *nhints = 0;
    *hints = NULL;

    return CSS_OK;
}

static css_error ua_default_for_property(void *pw, uint32_t property, css_hint *hint)
{
    UNUSED(pw);

    if (property == CSS_PROP_COLOR) {
        hint->data.color = 0xff000000;
        hint->status = CSS_COLOR_COLOR;
    } else if (property == CSS_PROP_FONT_FAMILY) {
        hint->data.strings = NULL;
        hint->status = CSS_FONT_FAMILY_SANS_SERIF;
    } else if (property == CSS_PROP_QUOTES) {
        hint->data.strings = NULL;
        hint->status = CSS_QUOTES_NONE;
    } else if (property == CSS_PROP_VOICE_FAMILY) {
        hint->data.strings = NULL;
        hint->status = 0;
    } else {
        return CSS_INVALID;
    }

    return CSS_OK;
}

static css_error set_libcss_node_data(void *pw, void *node, void *libcss_node_data)
{
    test_node *n = node;
    UNUSED(pw);

    n->libcss_node_data = libcss_node_data;

    return CSS_OK;
}

static css_error get_libcss_node_data(void *pw, void *node, void **libcss_node_data)
{
    test_node *n = node;
    UNUSED(pw);

    *libcss_node_data = n->libcss_node_data;

    return CSS_OK;
}

static css_select_handler select_handler = {
    CSS_SELECT_HANDLER_VERSION_1,
    node_name,
    node_classes,
    node_id,
    named_ancestor_node,
    named_parent_node,
    named_sibling_node,
    named_generic_sibling_node,
    parent_node,
    sibling_node,
    node_has_name,
    node_has_class,
    node_has_id,
    node_has_attribute,
    node_has_attribute_equal,
    node_has_attribute_dashmatch,
    node_has_attribute_includes,
    node_has_attribute_prefix,
    node_has_attribute_suffix,
    node_has_attribute_substring,
    node_is_root,
    node_count_siblings,
    node_is_empty,
    node_is_link,
    node_is_visited,
    node_is_hover,
    node_is_active,
    node_is_focus,
    node_is_enabled,
    node_is_disabled,
    node_is_checked,
    node_is_target,
    node_is_lang,
    node_presentational_hint,
    ua_default_for_property,
    set_libcss_node_data,
    get_libcss_node_data,
};

typedef struct style_case {
    css_stylesheet *sheet;
    css_stylesheet *import_sheet;
    css_stylesheet *inline_style;
    css_select_ctx *select_ctx;
    css_select_results *results;
    css_select_results *parent_results;
    lwc_string *node_name;
    lwc_string *parent_name;
    test_node node;
    test_node parent_node;
} style_case;

static css_unit_ctx unit_ctx = {
    .viewport_width = 200 * (1 << CSS_RADIX_POINT),
    .viewport_height = 100 * (1 << CSS_RADIX_POINT),
    .container_width = 200 * (1 << CSS_RADIX_POINT),
    .container_height = 100 * (1 << CSS_RADIX_POINT),
    .font_size_default = 16 * (1 << CSS_RADIX_POINT),
    .font_size_minimum = 0,
    .device_dpi = 96 * (1 << CSS_RADIX_POINT),
    .root_style = NULL,
    .pw = NULL,
    .measure = NULL,
};

static css_stylesheet *create_stylesheet_from_css(
    const char *css,
    bool inline_style,
    bool allow_pending_imports)
{
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_3,
        .charset = "UTF-8",
        .url = "var_resolve",
        .title = "var_resolve",
        .allow_quirks = false,
        .inline_style = inline_style,
        .resolve = resolve_url,
        .resolve_pw = NULL,
        .import = NULL,
        .import_pw = NULL,
        .font_face = NULL,
        .font_face_pw = NULL,
        .color = NULL,
        .color_pw = NULL,
        .font = NULL,
        .font_pw = NULL,
        .error = NULL,
        .error_pw = NULL,
    };
    css_stylesheet *sheet;
    css_error err;

    assert(css_stylesheet_create(&params, &sheet) == CSS_OK);
    err = css_stylesheet_append_data(sheet, (const uint8_t *)css, strlen(css));
    assert(err == CSS_OK || err == CSS_NEEDDATA);
    err = css_stylesheet_data_done(sheet);
    assert(err == CSS_OK || (allow_pending_imports && err == CSS_IMPORTS_PENDING));

    return sheet;
}

static void destroy_node_data(test_node *node)
{
    if (node->libcss_node_data != NULL) {
        assert(css_libcss_node_data_handler(&select_handler,
            CSS_NODE_DELETED, NULL, node, NULL,
            node->libcss_node_data) == CSS_OK);
        node->libcss_node_data = NULL;
    }
}

static const css_computed_style *select_style_for_node(
    style_case *ctx,
    test_node *node,
    const css_stylesheet *inline_style,
    css_select_results **results)
{
    css_media media = {.type = CSS_MEDIA_ALL};

    assert(css_select_style(ctx->select_ctx, node, &unit_ctx, &media,
            inline_style, &select_handler, NULL, results) == CSS_OK);

    assert(*results != NULL);
    assert((*results)->styles[CSS_PSEUDO_ELEMENT_NONE] != NULL);

    return (*results)->styles[CSS_PSEUDO_ELEMENT_NONE];
}

static void setup_style_case(style_case *ctx, const char *css)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->sheet = create_stylesheet_from_css(css, false, false);

    assert(css_select_ctx_create(&ctx->select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx->select_ctx, ctx->sheet, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);

    assert(lwc_intern_string("div", 3, &ctx->node_name) == lwc_error_ok);
    ctx->node.name = ctx->node_name;
    ctx->node.parent = NULL;
    ctx->node.libcss_node_data = NULL;
}

static const css_computed_style *select_style_from_css(const char *css, style_case *ctx)
{
    setup_style_case(ctx, css);

    return select_style_for_node(ctx, &ctx->node, NULL, &ctx->results);
}

static void destroy_style_case(style_case *ctx)
{
    destroy_node_data(&ctx->node);
    destroy_node_data(&ctx->parent_node);
    if (ctx->results != NULL)
        css_select_results_destroy(ctx->results);
    if (ctx->parent_results != NULL)
        css_select_results_destroy(ctx->parent_results);
    if (ctx->select_ctx != NULL)
        css_select_ctx_destroy(ctx->select_ctx);
    if (ctx->inline_style != NULL)
        css_stylesheet_destroy(ctx->inline_style);
    if (ctx->sheet != NULL)
        css_stylesheet_destroy(ctx->sheet);
    if (ctx->import_sheet != NULL)
        css_stylesheet_destroy(ctx->import_sheet);
    if (ctx->node_name != NULL)
        lwc_string_unref(ctx->node_name);
    if (ctx->parent_name != NULL)
        lwc_string_unref(ctx->parent_name);
}

typedef uint8_t (*length_getter)(
    const css_computed_style *style,
    css_fixed_or_calc *length,
    css_unit *unit);

static void expect_length_px(const char *label,
    const css_computed_style *style,
    length_getter getter,
    uint8_t expected_type,
    int expected_px)
{
    css_fixed_or_calc length = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = getter(style, &length, &unit);
    int px = -1;

    if (type != expected_type) {
        printf("FAIL - %s type mismatch: got=%u expected=%u\n",
            label, type, expected_type);
        exit(EXIT_FAILURE);
    }

    if (css_computed_length_to_px(style, &unit_ctx, 200, length, unit, &px) != CSS_OK) {
        printf("FAIL - %s conversion failed: unit=%d\n", label, unit);
        exit(EXIT_FAILURE);
    }

    if (px != expected_px) {
        printf("FAIL - %s px mismatch: got=%d expected=%d\n",
            label, px, expected_px);
        exit(EXIT_FAILURE);
    }
}

static void expect_length_type(const char *label,
    const css_computed_style *style,
    length_getter getter,
    uint8_t expected_type)
{
    css_fixed_or_calc length = {.value = 0};
    css_unit unit = CSS_UNIT_PX;
    uint8_t type = getter(style, &length, &unit);

    if (type != expected_type) {
        printf("FAIL - %s type mismatch: got=%u expected=%u\n",
            label, type, expected_type);
        exit(EXIT_FAILURE);
    }
}

static void test_same_rule_custom_property_after_use(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--w); --w: 12px; }", &ctx);

    expect_length_px("same-rule width", style,
        css_computed_width, CSS_WIDTH_SET, 12);

    destroy_style_case(&ctx);
}

static void test_later_rule_custom_property_after_use(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--w); } div { --w: 13px; }", &ctx);

    expect_length_px("later-rule width", style,
        css_computed_width, CSS_WIDTH_SET, 13);

    destroy_style_case(&ctx);
}

static void test_nested_fallback_uses_final_context(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--missing, var(--w)); --w: 14px; }", &ctx);

    expect_length_px("nested fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 14);

    destroy_style_case(&ctx);
}

static void test_var_shorthand_keeps_declaration_order(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { margin: var(--m); margin-left: 5px; --m: 10px; }", &ctx);

    expect_length_px("margin top", style,
        css_computed_margin_top, CSS_MARGIN_SET, 10);
    expect_length_px("margin right", style,
        css_computed_margin_right, CSS_MARGIN_SET, 10);
    expect_length_px("margin bottom", style,
        css_computed_margin_bottom, CSS_MARGIN_SET, 10);
    expect_length_px("margin left", style,
        css_computed_margin_left, CSS_MARGIN_SET, 5);

    destroy_style_case(&ctx);
}

static void test_reused_custom_property_expansion(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --size: 36px; width: var(--size); height: var(--size); }", &ctx);

    expect_length_px("reused variable width", style,
        css_computed_width, CSS_WIDTH_SET, 36);
    expect_length_px("reused variable height", style,
        css_computed_height, CSS_HEIGHT_SET, 36);

    destroy_style_case(&ctx);
}

static void test_important_custom_property_wins(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --w: 12px !important; }"
        "div { width: var(--w); --w: 13px; }", &ctx);

    expect_length_px("important custom property width", style,
        css_computed_width, CSS_WIDTH_SET, 12);

    destroy_style_case(&ctx);
}

static void test_invalid_var_overrides_earlier_width(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: 7px; width: var(--missing); }", &ctx);

    expect_length_type("invalid var width", style,
        css_computed_width, CSS_WIDTH_AUTO);

    destroy_style_case(&ctx);
}

static void test_custom_property_inherits_from_parent_node(void)
{
    style_case ctx;
    const css_computed_style *style;

    setup_style_case(&ctx,
        ":root { --w: 21px; }"
        "div { width: var(--w); }");

    assert(lwc_intern_string("html", 4, &ctx.parent_name) == lwc_error_ok);
    ctx.parent_node.name = ctx.parent_name;
    ctx.parent_node.parent = NULL;
    ctx.parent_node.libcss_node_data = NULL;

    select_style_for_node(&ctx, &ctx.parent_node, NULL, &ctx.parent_results);

    ctx.node.parent = &ctx.parent_node;
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);

    expect_length_px("inherited custom property width", style,
        css_computed_width, CSS_WIDTH_SET, 21);

    destroy_style_case(&ctx);
}

static void test_inherited_custom_property_resolves_with_child_shadow(void)
{
    style_case ctx;
    test_node plain_node;
    test_node shadow_node;
    css_select_results *plain_results = NULL;
    css_select_results *shadow_results = NULL;
    lwc_string *plain_name = NULL;
    lwc_string *shadow_name = NULL;
    const css_computed_style *style;

    setup_style_case(&ctx,
        ":root { --base: 40px; --w: var(--base); }"
        "div { width: var(--w); }"
        "section { --base: 41px; width: var(--w); }");

    assert(lwc_intern_string("html", 4, &ctx.parent_name) == lwc_error_ok);
    ctx.parent_node.name = ctx.parent_name;
    ctx.parent_node.parent = NULL;
    ctx.parent_node.libcss_node_data = NULL;

    select_style_for_node(&ctx, &ctx.parent_node, NULL, &ctx.parent_results);

    ctx.node.parent = &ctx.parent_node;
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);
    expect_length_px("inherited variable without shadow", style,
        css_computed_width, CSS_WIDTH_SET, 40);

    assert(lwc_intern_string("section", 7, &shadow_name) == lwc_error_ok);
    shadow_node.name = shadow_name;
    shadow_node.parent = &ctx.parent_node;
    shadow_node.libcss_node_data = NULL;

    style = select_style_for_node(&ctx, &shadow_node, NULL, &shadow_results);
    expect_length_px("inherited variable with child shadow", style,
        css_computed_width, CSS_WIDTH_SET, 41);

    assert(lwc_intern_string("div", 3, &plain_name) == lwc_error_ok);
    plain_node.name = plain_name;
    plain_node.parent = &ctx.parent_node;
    plain_node.libcss_node_data = NULL;

    style = select_style_for_node(&ctx, &plain_node, NULL, &plain_results);
    expect_length_px("inherited variable after child shadow", style,
        css_computed_width, CSS_WIDTH_SET, 40);

    destroy_node_data(&plain_node);
    destroy_node_data(&shadow_node);
    if (plain_results != NULL)
        css_select_results_destroy(plain_results);
    if (shadow_results != NULL)
        css_select_results_destroy(shadow_results);
    lwc_string_unref(plain_name);
    lwc_string_unref(shadow_name);
    destroy_style_case(&ctx);
}

static void test_inline_style_var_resolution(void)
{
    style_case ctx;
    const css_computed_style *style;

    setup_style_case(&ctx, "div { width: 5px; }");
    ctx.inline_style = create_stylesheet_from_css(
        "width: var(--w); --w: 22px;", true, false);

    style = select_style_for_node(&ctx, &ctx.node,
        ctx.inline_style, &ctx.results);

    expect_length_px("inline style width", style,
        css_computed_width, CSS_WIDTH_SET, 22);

    destroy_style_case(&ctx);
}

static void test_imported_sheet_custom_property(void)
{
    style_case ctx;
    lwc_string *url = NULL;
    const css_computed_style *style;

    memset(&ctx, 0, sizeof(ctx));

    ctx.sheet = create_stylesheet_from_css(
        "@import url(\"vars.css\");"
        "div { width: var(--w); }", false, true);
    ctx.import_sheet = create_stylesheet_from_css(
        "div { --w: 23px; }", false, false);

    assert(css_stylesheet_next_pending_import(ctx.sheet, &url) == CSS_OK);
    lwc_string_unref(url);
    assert(css_stylesheet_register_import(ctx.sheet, ctx.import_sheet) == CSS_OK);

    assert(css_select_ctx_create(&ctx.select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx.select_ctx, ctx.sheet,
        CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);

    assert(lwc_intern_string("div", 3, &ctx.node_name) == lwc_error_ok);
    ctx.node.name = ctx.node_name;
    ctx.node.parent = NULL;
    ctx.node.libcss_node_data = NULL;

    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);

    expect_length_px("imported custom property width", style,
        css_computed_width, CSS_WIDTH_SET, 23);

    destroy_style_case(&ctx);
}

static void test_pseudo_element_uses_element_vars(void)
{
    style_case ctx;

    select_style_from_css(
        "div { --w: 24px; }"
        "div::before { width: var(--w); }", &ctx);

    assert(ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE] != NULL);
    expect_length_px("before pseudo width",
        ctx.results->styles[CSS_PSEUDO_ELEMENT_BEFORE],
        css_computed_width, CSS_WIDTH_SET, 24);

    destroy_style_case(&ctx);
}

static void test_literal_fallback_value(void)
{
    style_case ctx;
    const css_computed_style *style =
        select_style_from_css("div { width: var(--missing, 25px); }", &ctx);

    expect_length_px("literal fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 25);

    destroy_style_case(&ctx);
}

static void test_shorthand_fallback_value(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { padding: var(--missing, 6px 7px); }", &ctx);

    expect_length_px("padding fallback top", style,
        css_computed_padding_top, CSS_PADDING_SET, 6);
    expect_length_px("padding fallback right", style,
        css_computed_padding_right, CSS_PADDING_SET, 7);
    expect_length_px("padding fallback bottom", style,
        css_computed_padding_bottom, CSS_PADDING_SET, 6);
    expect_length_px("padding fallback left", style,
        css_computed_padding_left, CSS_PADDING_SET, 7);

    destroy_style_case(&ctx);
}

static void test_variable_cycle_is_invalid(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a); width: var(--a); }", &ctx);

    expect_length_type("cycle width", style,
        css_computed_width, CSS_WIDTH_AUTO);

    destroy_style_case(&ctx);
}

static void test_cycle_uses_property_fallback(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a); width: var(--a, 31px); }", &ctx);

    expect_length_px("cycle property fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 31);

    destroy_style_case(&ctx);
}

static void test_cyclic_var_fallback_does_not_poison_later_use(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a);"
        " width: var(--a, 42px); height: var(--a); }", &ctx);

    expect_length_px("cycle fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 42);
    expect_length_type("cycle later height", style,
        css_computed_height, CSS_HEIGHT_AUTO);

    destroy_style_case(&ctx);
}

static void test_custom_property_fallback_cycle_is_invalid(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b, 32px); --b: var(--a); width: var(--a); }", &ctx);

    expect_length_type("custom property fallback cycle width", style,
        css_computed_width, CSS_WIDTH_AUTO);

    destroy_style_case(&ctx);
}

static void test_noncyclic_var_can_fallback_from_cycle(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--b); --b: var(--a);"
        " --c: var(--a, 33px); width: var(--c); }", &ctx);

    expect_length_px("noncyclic fallback from cycle width", style,
        css_computed_width, CSS_WIDTH_SET, 33);

    destroy_style_case(&ctx);
}

static void test_self_reference_in_fallback_is_cycle(void)
{
    style_case ctx;
    const css_computed_style *style = select_style_from_css(
        "div { --a: var(--missing, var(--a)); width: var(--a, 34px); }", &ctx);

    expect_length_px("self fallback cycle width", style,
        css_computed_width, CSS_WIDTH_SET, 34);

    destroy_style_case(&ctx);
}

static void test_inherited_overlay_cycle_is_invalid(void)
{
    style_case ctx;
    const css_computed_style *style;

    setup_style_case(&ctx,
        ":root { --b: var(--a); }"
        "div { --a: var(--b); width: var(--a, 35px); }");

    assert(lwc_intern_string("html", 4, &ctx.parent_name) == lwc_error_ok);
    ctx.parent_node.name = ctx.parent_name;
    ctx.parent_node.parent = NULL;
    ctx.parent_node.libcss_node_data = NULL;

    select_style_for_node(&ctx, &ctx.parent_node, NULL, &ctx.parent_results);

    ctx.node.parent = &ctx.parent_node;
    style = select_style_for_node(&ctx, &ctx.node, NULL, &ctx.results);

    expect_length_px("inherited overlay cycle fallback width", style,
        css_computed_width, CSS_WIDTH_SET, 35);

    destroy_style_case(&ctx);
}

int main(void)
{
    test_same_rule_custom_property_after_use();
    test_later_rule_custom_property_after_use();
    test_nested_fallback_uses_final_context();
    test_var_shorthand_keeps_declaration_order();
    test_reused_custom_property_expansion();
    test_important_custom_property_wins();
    test_invalid_var_overrides_earlier_width();
    test_custom_property_inherits_from_parent_node();
    test_inherited_custom_property_resolves_with_child_shadow();
    test_inline_style_var_resolution();
    test_imported_sheet_custom_property();
    test_pseudo_element_uses_element_vars();
    test_literal_fallback_value();
    test_shorthand_fallback_value();
    test_variable_cycle_is_invalid();
    test_cycle_uses_property_fallback();
    test_cyclic_var_fallback_does_not_poison_later_use();
    test_custom_property_fallback_cycle_is_invalid();
    test_noncyclic_var_can_fallback_from_cycle();
    test_self_reference_in_fallback_is_cycle();
    test_inherited_overlay_cycle_is_invalid();

    printf("PASS\n");
    return 0;
}
