#include "input.h"
#include <string.h>
#include <stdlib.h>

gboolean layout_load(Layout *layout, const gchar *path, GError **error) {
    GKeyFile *file = g_key_file_new();
    gboolean ok = g_key_file_load_from_file(file, path, 0, error);
    if (!ok) { g_key_file_free(file); return FALSE; }
    layout->id = g_path_get_basename(path);
    layout->name = g_key_file_get_string(file, "layout", "name", NULL);
    for (guint r = 0; r < 3 && ok; r++) {
        gchar key[8];
        g_snprintf(key, sizeof key, "row%u", r + 1);
        gchar *row = g_key_file_get_value(file, "layout", key, NULL);
        ok = row && g_utf8_validate(row, -1, NULL);
        if (ok) {
            glong count = g_utf8_strlen(row, -1);
            ok = count > 0 && count <= 16;
            if (ok) {
                layout->lengths[r] = count;
                const gchar *p = row;
                for (guint i = 0; i < (guint)count; i++, p = g_utf8_next_char(p)) {
                    layout->rows[r][i] = g_utf8_get_char(p);
                    if (!g_unichar_isprint(layout->rows[r][i])) ok = FALSE;
                }
            }
        }
        g_free(row);
    }
    if (!layout->name || !*layout->name) ok = FALSE;
    if (!ok) g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE, "Invalid keyboard layout: %s", path);
    g_key_file_free(file);
    return ok;
}

void layout_free(Layout *layout) {
    g_free(layout->id);
    g_free(layout->name);
    memset(layout, 0, sizeof *layout);
}

gunichar input_character(InputState *state, gunichar lower, gunichar upper) {
    gboolean shifted = state->shift;
    if (g_unichar_isalpha(lower)) shifted ^= state->caps;
    return shifted ? upper : lower;
}

gsize input_bytes(InputState *state, gunichar lower, gunichar upper, gchar out[8]) {
    gunichar c = input_character(state, lower, upper);
    gsize n = 0;
    if (state->alt) out[n++] = '\033';
    if (state->ctrl) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c == ' ' || c == '@') c = 0;
        else if (c >= 'A' && c <= '_') c &= 31;
        else if (c == '?') c = 127;
        else return 0;
    }
    n += g_unichar_to_utf8(c, out + n);
    state->shift = state->ctrl = state->alt = FALSE;
    state->shift_tap = 0;
    return n;
}

void input_shift(InputState *state, gint64 now) {
    if (state->caps) { state->caps = state->shift = FALSE; }
    else if (state->shift && state->shift_tap && now - state->shift_tap <= 600) {
        state->caps = TRUE; state->shift = FALSE;
    } else state->shift = !state->shift;
    state->shift_tap = now;
}

guint layout_next(GPtrArray *layouts, guint current) {
    for (guint step = 1; step <= layouts->len; step++) {
        guint next = (current + step) % layouts->len;
        if (((Layout *)g_ptr_array_index(layouts, next))->enabled) return next;
    }
    return current;
}

void layouts_select(GPtrArray *layouts, const gchar *const *ids, gsize count) {
    guint enabled = 0;
    for (guint i = 0; i < layouts->len; i++) {
        Layout *l = g_ptr_array_index(layouts, i); l->enabled = FALSE;
        for (gsize j = 0; j < count; j++) if (!g_strcmp0(l->id, ids[j])) l->enabled = TRUE;
        enabled += l->enabled;
    }
    if (!enabled && layouts->len) ((Layout *)g_ptr_array_index(layouts, 0))->enabled = TRUE;
}

static gboolean valid_token(const gchar *s, const gchar *extra) {
    if (!s || !*s || strlen(s) > 128 || *s == '-') return FALSE;
    for (; *s; s++) if (!g_ascii_isalnum(*s) && !strchr(extra, *s)) return FALSE;
    return TRUE;
}

gboolean profile_valid(const gchar *host, const gchar *user, const gchar *port, const gchar *session) {
    if (!valid_token(host, ".-:") || !valid_token(user, "._-") || !valid_token(session, "._-")) return FALSE;
    if (!port || !*port) return FALSE;
    for (const gchar *p = port; *p; p++) if (!g_ascii_isdigit(*p)) return FALSE;
    gchar *end;
    guint64 value = g_ascii_strtoull(port, &end, 10);
    return !*end && value > 0 && value <= 65535;
}

int input_check(const gchar *directory) {
    InputState s = {0}; gchar out[8] = {0};
    g_assert(input_bytes(&s, 0x439, 0x419, out) == 2 && !memcmp(out, "й", 2));
    s.shift = TRUE;
    g_assert(input_bytes(&s, 0x439, 0x419, out) == 2 && !memcmp(out, "Й", 2) && !s.shift);
    s.caps = TRUE;
    g_assert(input_bytes(&s, 'a', 'A', out) == 1 && out[0] == 'A' && s.caps);
    s.shift = TRUE;
    g_assert(input_bytes(&s, 'a', 'A', out) == 1 && out[0] == 'a' && s.caps && !s.shift);
    g_assert(input_bytes(&s, '1', '!', out) == 1 && out[0] == '1');
    s.ctrl = TRUE;
    g_assert(input_bytes(&s, 'c', 'C', out) == 1 && out[0] == 3 && !s.ctrl);
    s.alt = TRUE; s.caps = FALSE;
    g_assert(input_bytes(&s, 0xe9, 0xc9, out) == 3 && !memcmp(out, "\033é", 3));
    g_assert(profile_valid("192.0.2.10", "reader", "22", "kindle"));
    g_assert(!profile_valid("host", "root", "65536", "x"));
    g_assert(!profile_valid("host", "root", "22", "x;id"));
    g_assert(!profile_valid("-oProxyCommand=id", "root", "22", "x"));
    memset(&s, 0, sizeof s);
    input_shift(&s, 1000); g_assert(s.shift && !s.caps);
    input_shift(&s, 1300); g_assert(!s.shift && s.caps);
    input_shift(&s, 1400); g_assert(!s.shift && !s.caps);
    input_shift(&s, 2000); input_shift(&s, 2800); g_assert(!s.shift && !s.caps);
    GPtrArray *choices = g_ptr_array_new();
    Layout en = {.id = "en.ini"}, ru = {.id = "ru.ini"}, de = {.id = "de.ini"};
    g_ptr_array_add(choices, &en); g_ptr_array_add(choices, &de); g_ptr_array_add(choices, &ru);
    const gchar *selected[] = {"en.ini", "ru.ini", "missing.ini"};
    layouts_select(choices, selected, 3);
    g_assert(en.enabled && ru.enabled && !de.enabled && layout_next(choices, 0) == 2 && layout_next(choices, 2) == 0);
    layouts_select(choices, selected + 1, 1); g_assert(layout_next(choices, 2) == 2);
    layouts_select(choices, NULL, 0); g_assert(en.enabled && !ru.enabled);
    g_ptr_array_free(choices, TRUE);
    GError *error = NULL;
    GDir *dir = g_dir_open(directory, 0, &error);
    g_assert_no_error(error); g_assert(dir);
    const gchar *name; guint count = 0;
    while ((name = g_dir_read_name(dir))) {
        if (name[0] == '.' || !g_str_has_suffix(name, ".ini")) continue;
        Layout l = {0}; gchar *path = g_build_filename(directory, name, NULL);
        if (!layout_load(&l, path, &error)) { g_printerr("%s: %s\n", path, error ? error->message : "invalid layout"); return 1; }
        for (guint r = 0; r < 3; r++) for (guint i = 0; i < l.lengths[r]; i++) {
            memset(&s, 0, sizeof s);
            gsize n = input_bytes(&s, l.rows[r][i], g_unichar_toupper(l.rows[r][i]), out);
            g_assert(n && g_utf8_validate(out, n, NULL));
        }
        layout_free(&l); g_free(path); count++;
    }
    g_dir_close(dir); g_assert(count >= 5);
    g_print("PASS: %u layouts, UTF-8, Shift, Caps, Ctrl, Alt language selection and connection validation\n", count);
    return 0;
}
