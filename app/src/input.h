#ifndef INK_INPUT_H
#define INK_INPUT_H
#include <glib.h>
typedef struct { gchar *id, *name; gboolean enabled; gunichar rows[3][16]; guint lengths[3]; } Layout;
typedef struct { gboolean shift, caps, ctrl, alt; gint64 shift_tap; } InputState;
gboolean layout_load(Layout *layout, const gchar *path, GError **error);
void layout_free(Layout *layout);
gunichar input_character(InputState *state, gunichar lower, gunichar upper);
gsize input_bytes(InputState *state, gunichar lower, gunichar upper, gchar out[8]);
void input_shift(InputState *state, gint64 now);
guint layout_next(GPtrArray *layouts, guint current);
void layouts_select(GPtrArray *layouts, const gchar *const *ids, gsize count);
gboolean profile_valid(const gchar *host, const gchar *user, const gchar *port, const gchar *session);
int input_check(const gchar *directory);
#endif
