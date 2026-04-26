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
    void *libcss_node_data;
} test_node;

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
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);

    *ancestor = NULL;

    return CSS_OK;
}

static css_error named_parent_node(void *pw, void *node, const css_qname *qname, void **parent)
{
    UNUSED(pw);
    UNUSED(node);
    UNUSED(qname);

    *parent = NULL;

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
    UNUSED(pw);
    UNUSED(node);

    *parent = NULL;

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
    UNUSED(pw);
    UNUSED(node);

    *match = true;

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
    css_select_ctx *select_ctx;
    css_select_results *results;
    lwc_string *node_name;
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

static const css_computed_style *select_style_from_css(const char *css, style_case *ctx)
{
    css_stylesheet_params params = {
        .params_version = CSS_STYLESHEET_PARAMS_VERSION_1,
        .level = CSS_LEVEL_3,
        .charset = "UTF-8",
        .url = "var_resolve",
        .title = "var_resolve",
        .allow_quirks = false,
        .inline_style = false,
        .resolve = resolve_url,
        .resolve_pw = NULL,
        .import = NULL,
        .import_pw = NULL,
        .color = NULL,
        .color_pw = NULL,
        .font = NULL,
        .font_pw = NULL,
    };
    css_media media = {.type = CSS_MEDIA_ALL};
    test_node node;

    memset(ctx, 0, sizeof(*ctx));

    assert(css_stylesheet_create(&params, &ctx->sheet) == CSS_OK);
    {
        css_error err = css_stylesheet_append_data(ctx->sheet, (const uint8_t *)css, strlen(css));
        assert(err == CSS_OK || err == CSS_NEEDDATA);
    }
    assert(css_stylesheet_data_done(ctx->sheet) == CSS_OK);

    assert(css_select_ctx_create(&ctx->select_ctx) == CSS_OK);
    assert(css_select_ctx_append_sheet(ctx->select_ctx, ctx->sheet, CSS_ORIGIN_AUTHOR, NULL) == CSS_OK);

    assert(lwc_intern_string("div", 3, &ctx->node_name) == lwc_error_ok);
    node.name = ctx->node_name;
    node.libcss_node_data = NULL;

    assert(css_select_style(ctx->select_ctx, &node, &unit_ctx, &media,
            NULL, &select_handler, NULL, &ctx->results) == CSS_OK);

    if (node.libcss_node_data != NULL) {
        assert(css_libcss_node_data_handler(&select_handler,
            CSS_NODE_DELETED, NULL, &node, NULL,
            node.libcss_node_data) == CSS_OK);
    }

    assert(ctx->results != NULL);
    assert(ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE] != NULL);

    return ctx->results->styles[CSS_PSEUDO_ELEMENT_NONE];
}

static void destroy_style_case(style_case *ctx)
{
    if (ctx->results != NULL)
        css_select_results_destroy(ctx->results);
    if (ctx->select_ctx != NULL)
        css_select_ctx_destroy(ctx->select_ctx);
    if (ctx->sheet != NULL)
        css_stylesheet_destroy(ctx->sheet);
    if (ctx->node_name != NULL)
        lwc_string_unref(ctx->node_name);
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

int main(void)
{
    test_same_rule_custom_property_after_use();
    test_later_rule_custom_property_after_use();
    test_nested_fallback_uses_final_context();
    test_var_shorthand_keeps_declaration_order();
    test_important_custom_property_wins();
    test_invalid_var_overrides_earlier_width();

    printf("PASS\n");
    return 0;
}
