#include <gtk/gtk.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "input.h"
#include "config.h"
#include "kindle.h"

KTconf *conf;
gboolean debug = FALSE;
static GtkWidget *window, *terminal, *keyboard, *status, *settings, *terminal_box;
static GtkWidget *fields[4], *font_label, *pages, *language_options, *keyboard_toggle, *keyboard_bar, *keyboard_chevron;
static GtkEditable *editing;
static GPtrArray *layouts;
static InputState input;
static guint language;
static guint symbols;
static gboolean reconnecting, connected, navigation;
static gboolean keyboard_visible = TRUE;
static GPid child;
static gchar *base, *host, *user, *port, *session;
static gint font_size = 8;
static const gchar *field_names[] = { "Host", "User", "Port", "Session" };
static void draw_keyboard(void);
static void connect_terminal(void);
static void settings_back(GtkWidget *b, gpointer data);

typedef struct { gunichar lower, upper; guint special; } Key;

static gint compare_layouts(gconstpointer a, gconstpointer b) {
    const Layout *left = *(Layout *const *)a, *right = *(Layout *const *)b;
    gint lp = !strcmp(left->id, "en.ini") ? 0 : !strcmp(left->id, "ru.ini") ? 1 : 2;
    gint rp = !strcmp(right->id, "en.ini") ? 0 : !strcmp(right->id, "ru.ini") ? 1 : 2;
    return lp == rp ? strcmp(left->id, right->id) : lp - rp;
}

static gchar *path_for(const gchar *name) { return g_build_filename(base, name, NULL); }
static void message(const gchar *text) { gtk_label_set_text(GTK_LABEL(status), text); }

static void sync_keyboard(void) {
    gboolean available = !gtk_widget_get_visible(settings) || gtk_notebook_get_current_page(GTK_NOTEBOOK(pages)) == 1;
    gtk_widget_set_visible(keyboard_bar, available);
    gtk_widget_set_visible(keyboard, available && keyboard_visible);
    gtk_widget_queue_draw(keyboard_chevron);
    atk_object_set_name(gtk_widget_get_accessible(keyboard_toggle), keyboard_visible ? "Hide keyboard" : "Show keyboard");
}

static gboolean draw_chevron(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    (void)event; (void)data;
    GtkAllocation a; gtk_widget_get_allocation(widget, &a);
    gint x = a.width / 2, y = a.height / 2, direction = keyboard_visible ? 1 : -1;
    GdkPoint points[] = {{x - 22, y - 10 * direction}, {x, y + 10 * direction}, {x + 22, y - 10 * direction}};
    GdkGC *gc = gdk_gc_new(gtk_widget_get_window(widget));
    GdkColor black = {0, 0, 0, 0}; gdk_gc_set_rgb_fg_color(gc, &black);
    gdk_gc_set_line_attributes(gc, 4, GDK_LINE_SOLID, GDK_CAP_ROUND, GDK_JOIN_ROUND);
    gdk_draw_lines(gtk_widget_get_window(widget), gc, points, G_N_ELEMENTS(points));
    g_object_unref(gc); return FALSE;
}

static void toggle_keyboard(GtkWidget *button, gpointer data) {
    (void)button; (void)data;
    keyboard_visible = !keyboard_visible; sync_keyboard();
    if (!gtk_widget_get_visible(settings)) gtk_widget_grab_focus(terminal);
}

static void save_config(void) {
    GKeyFile *f = g_key_file_new();
    g_key_file_set_string(f, "connection", "host", host);
    g_key_file_set_string(f, "connection", "user", user);
    g_key_file_set_string(f, "connection", "port", port);
    g_key_file_set_string(f, "connection", "session", session);
    g_key_file_set_integer(f, "display", "font", font_size);
    Layout *l = g_ptr_array_index(layouts, language);
    g_key_file_set_string(f, "keyboard", "layout", l->id);
    const gchar **enabled = g_new0(const gchar *, layouts->len);
    gsize count = 0;
    for (guint i = 0; i < layouts->len; i++) {
        Layout *item = g_ptr_array_index(layouts, i);
        if (item->enabled) enabled[count++] = item->id;
    }
    g_key_file_set_string_list(f, "keyboard", "enabled", enabled, count);
    g_free(enabled);
    gchar *data = g_key_file_to_data(f, NULL, NULL), *path = path_for("config/settings.ini");
    GError *error = NULL;
    if (!g_file_set_contents(path, data, -1, &error)) { message(error->message); g_error_free(error); }
    g_free(path); g_free(data); g_key_file_free(f);
}

static void update_font(void) {
    PangoFontDescription *font = pango_font_description_new();
    pango_font_description_set_family(font, "Monospace");
    pango_font_description_set_size(font, font_size * PANGO_SCALE);
    vte_terminal_set_font(VTE_TERMINAL(terminal), font);
    pango_font_description_free(font);
    gchar text[32]; g_snprintf(text, sizeof text, "Text size: %d", font_size);
    gtk_label_set_text(GTK_LABEL(font_label), text);
}

static void send_bytes(const gchar *bytes, gsize count) {
    if (editing) {
        gint start, end;
        if (gtk_editable_get_selection_bounds(editing, &start, &end)) gtk_editable_delete_text(editing, start, end);
        gint pos = gtk_editable_get_position(editing);
        gtk_editable_insert_text(editing, bytes, count, &pos);
        gtk_editable_set_position(editing, pos);
    } else if (child) vte_terminal_feed_child(VTE_TERMINAL(terminal), bytes, count);
}

static void special_key(guint keyval) {
    if (editing) {
        gint pos = gtk_editable_get_position(editing), start, end;
        if (keyval == GDK_BackSpace) {
            if (gtk_editable_get_selection_bounds(editing, &start, &end)) gtk_editable_delete_text(editing, start, end);
            else if (pos > 0) gtk_editable_delete_text(editing, pos - 1, pos);
        } else if (keyval == GDK_Left) gtk_editable_set_position(editing, MAX(0, pos - 1));
        else if (keyval == GDK_Right) gtk_editable_set_position(editing, pos + 1);
        else if (keyval == GDK_Home) gtk_editable_set_position(editing, 0);
        else if (keyval == GDK_End) gtk_editable_set_position(editing, -1);
        else if (keyval == GDK_Tab || keyval == GDK_Return) gtk_widget_child_focus(settings, GTK_DIR_TAB_FORWARD);
        return;
    }
    if (!child) return;
    if (keyval == GDK_End && !input.shift) {
        gchar sequence[16];
        guint modifier = 1 + (input.alt ? 2 : 0) + (input.ctrl ? 4 : 0);
        g_snprintf(sequence, sizeof sequence, modifier == 1 ? "\033[4~" : "\033[4;%u~", modifier);
        vte_terminal_feed_child(VTE_TERMINAL(terminal), sequence, strlen(sequence));
        input.ctrl = input.alt = FALSE; return;
    }
    GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = g_object_ref(gtk_widget_get_window(terminal));
    event->key.send_event = TRUE;
    event->key.time = GDK_CURRENT_TIME;
    event->key.keyval = keyval;
    event->key.state = (input.ctrl ? GDK_CONTROL_MASK : 0) | (input.alt ? GDK_MOD1_MASK : 0) | (input.shift ? GDK_SHIFT_MASK : 0);
    gtk_widget_event(terminal, event);
    gdk_event_free(event);
    input.shift = input.ctrl = input.alt = FALSE;
}

static void key_clicked(GtkWidget *button, gpointer data) {
    (void)button; Key key = *(Key *)data;
    gboolean refresh = input.shift || input.ctrl || input.alt;
    if (key.special) special_key(key.special);
    else {
        gchar bytes[8];
        gsize n = input_bytes(&input, key.lower, key.upper, bytes);
        if (n) send_bytes(bytes, n);
        else message("Ctrl shortcuts: use EN or Ctrl+C.");
    }
    if (refresh) draw_keyboard();
}

static GtkWidget *button_add(GtkWidget *row, const gchar *label, GCallback callback, gpointer data, gboolean expand) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_button_set_focus_on_click(GTK_BUTTON(b), FALSE);
    gtk_box_pack_start(GTK_BOX(row), b, expand, expand, 0);
    g_signal_connect(b, "clicked", callback, data);
    return b;
}

static gboolean outline_key(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    (void)event; (void)data;
    GtkAllocation a; gtk_widget_get_allocation(widget, &a);
    GdkGC *gc = gdk_gc_new(gtk_widget_get_window(widget));
    GdkColor color = {0, 28000, 28000, 28000}; gdk_gc_set_rgb_fg_color(gc, &color);
    gint x = gtk_widget_get_has_window(widget) ? 0 : a.x;
    gint y = gtk_widget_get_has_window(widget) ? 0 : a.y;
    gdk_draw_rectangle(gtk_widget_get_window(widget), gc, FALSE, x, y, a.width - 1, a.height - 1);
    g_object_unref(gc); return FALSE;
}

static GtkWidget *attach_button(GtkWidget *row, const gchar *label, GCallback callback, gpointer data, guint left, guint right) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_set_name(b, "key");
    g_signal_connect_after(b, "expose-event", G_CALLBACK(outline_key), NULL);
    gtk_button_set_focus_on_click(GTK_BUTTON(b), FALSE);
    gtk_table_attach(GTK_TABLE(row), b, left, right, 0, 1, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 3, 4);
    g_signal_connect(b, "clicked", callback, data);
    return b;
}

static void add_key(GtkWidget *row, gunichar lower, gunichar upper, guint special, const gchar *label, guint left, guint right) {
    gchar text[8] = {0};
    if (!label) g_unichar_to_utf8(input_character(&input, lower, upper), text);
    Key *key = g_new0(Key, 1); key->lower = lower; key->upper = upper; key->special = special;
    GtkWidget *b = attach_button(row, label ? label : text, G_CALLBACK(key_clicked), key, left, right);
    g_object_set_data_full(G_OBJECT(b), "input-key", key, g_free);
    if (special || label) gtk_widget_set_name(b, "action-key");
}

static void modifier(GtkWidget *b, gpointer data) {
    (void)b;
    switch (GPOINTER_TO_INT(data)) {
        case 0: input_shift(&input, g_get_monotonic_time() / 1000); break;
        case 2: input.ctrl = !input.ctrl; break;
        case 3: input.alt = !input.alt; break;
        case 4: symbols = navigation ? 0 : symbols ? 0 : 1; navigation = FALSE; input.shift = FALSE; break;
        case 5: symbols = symbols == 1 ? 2 : 1; input.shift = FALSE; break;
        case 6: navigation = !navigation; symbols = 0; input.shift = FALSE; break;
    }
    draw_keyboard();
}

static void choose_language(GtkWidget *b, gpointer data) {
    (void)b; (void)data; language = layout_next(layouts, language);
    memset(&input, 0, sizeof input); symbols = 0; navigation = FALSE;
    save_config(); draw_keyboard();
}

static GtkWidget *new_row(guint columns, gboolean accessory) {
    GtkWidget *row = gtk_table_new(1, columns, TRUE);
    gtk_box_pack_start(GTK_BOX(keyboard), row, !accessory, !accessory, 0);
    if (accessory) gtk_widget_set_size_request(row, -1, 80);
    return row;
}

static void interrupt_terminal(GtkWidget *b, gpointer data) {
    (void)b; (void)data;
    if (!editing && child) vte_terminal_feed_child(VTE_TERMINAL(terminal), "\003", 1);
}

static void action_key(GtkWidget *row, const gchar *label, GCallback callback, gpointer data, guint left, guint right, gboolean active) {
    GtkWidget *b = attach_button(row, label, callback, data, left, right);
    gtk_widget_set_name(b, active ? "active-key" : "action-key");
}

static void draw_keyboard(void) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(keyboard));
    for (GList *p = children; p; p = p->next) gtk_widget_destroy(p->data);
    g_list_free(children);
    GtkWidget *row = new_row(6, TRUE);
    add_key(row, 0, 0, GDK_Escape, "Esc", 0, 1);
    add_key(row, 0, 0, GDK_Tab, "Tab", 1, 2);
    action_key(row, "Ctrl", G_CALLBACK(modifier), GINT_TO_POINTER(2), 2, 3, input.ctrl);
    action_key(row, "Alt", G_CALLBACK(modifier), GINT_TO_POINTER(3), 3, 4, input.alt);
    action_key(row, "Ctrl+C", G_CALLBACK(interrupt_terminal), NULL, 4, 5, FALSE);
    action_key(row, navigation ? "ABC" : "Nav", G_CALLBACK(modifier), GINT_TO_POINTER(6), 5, 6, navigation);
    Layout *layout = g_ptr_array_index(layouts, language);
    const gchar *symbol_rows[2][3] = {
        {"1234567890", "-/:;()$&@\"", ".,?!'"},
        {"[]{}#%^*+=", "_\\|~<>€£¥•", "`:;\"'?"}
    };
    guint width = MAX(layout->lengths[0], MAX(layout->lengths[1], layout->lengths[2] + 2));
    if (symbols) width = 10;
    if (navigation) {
        row = new_row(10, FALSE);
        add_key(row, 0, 0, GDK_Home, "Home", 0, 3);
        add_key(row, 0, 0, GDK_Up, "↑", 4, 6);
        add_key(row, 0, 0, GDK_End, "End", 7, 10);
        row = new_row(10, FALSE);
        add_key(row, 0, 0, GDK_Left, "←", 2, 4);
        add_key(row, 0, 0, GDK_Down, "↓", 4, 6);
        add_key(row, 0, 0, GDK_Right, "→", 6, 8);
        row = new_row(10, FALSE);
        add_key(row, 0, 0, GDK_Page_Up, "Page up", 0, 5);
        add_key(row, 0, 0, GDK_Page_Down, "Page down", 5, 10);
    } else {
    for (guint r = 0; r < 3; r++) {
        row = new_row(width * 2, FALSE);
        const gchar *text = symbols ? symbol_rows[symbols - 1][r] : NULL;
        guint count = symbols ? (guint)g_utf8_strlen(text, -1) : layout->lengths[r];
        guint start = width - count;
        if (r == 2) {
            action_key(row, symbols ? (symbols == 1 ? "#+=" : "123") : (input.caps ? "Caps" : "Shift"),
                G_CALLBACK(modifier), GINT_TO_POINTER(symbols ? 5 : 0), 0, start, input.shift || input.caps);
            add_key(row, 0, 0, GDK_BackSpace, "⌫", width + count, width * 2);
        }
        for (guint i = 0; i < count; i++) {
            gunichar c = symbols ? g_utf8_get_char(text) : layout->rows[r][i];
            add_key(row, c, g_unichar_toupper(c), 0, NULL, start + i * 2, start + i * 2 + 2);
            if (symbols) text = g_utf8_next_char(text);
        }
    }
    }
    row = new_row(120, FALSE);
    action_key(row, symbols || navigation ? "ABC" : "123", G_CALLBACK(modifier), GINT_TO_POINTER(4), 0, 14, FALSE);
    guint enabled = 0;
    for (guint i = 0; i < layouts->len; i++) enabled += ((Layout *)g_ptr_array_index(layouts, i))->enabled;
    guint start = 14;
    if (enabled > 1) {
        gchar *label = g_strdup_printf("%s ↔", layout->name);
        action_key(row, label, G_CALLBACK(choose_language), NULL, 14, 30, FALSE);
        g_free(label); start = 30;
    }
    add_key(row, ',', ';', 0, NULL, start, start + 10);
    add_key(row, ' ', ' ', 0, "space", start + 10, 86);
    add_key(row, '.', ':', 0, NULL, 86, 96);
    add_key(row, 0, 0, GDK_Return, "Enter", 96, 120);
    gboolean visible = gtk_widget_get_visible(keyboard);
    gtk_widget_show_all(keyboard);
    if (!visible) gtk_widget_hide(keyboard);
}

static void child_exited(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data; child = 0; connected = FALSE;
    message("Disconnected. Reconnect in Settings.");
    if (reconnecting) { reconnecting = FALSE; connect_terminal(); }
}

static void title_changed(GtkWidget *widget, gpointer data) {
    (void)data;
    if (g_strcmp0(vte_terminal_get_window_title(VTE_TERMINAL(widget)), "InkTerm connected") == 0) { connected = TRUE; message("Connected"); }
}

static void connect_terminal(void) {
    if (child) { reconnecting = TRUE; kill(child, SIGHUP); return; }
    if (!profile_valid(host, user, port, session)) { message("Set a valid host, user, port and session in Settings."); return; }
    gchar *key = path_for("config/client.dropbear");
    if (!g_file_test(key, G_FILE_TEST_IS_REGULAR)) { message("SSH key missing. Provision the connection with the Mac installer."); g_free(key); return; }
    g_free(key);
    gchar *script = path_for("bin/connect.sh");
    gchar *argv[] = { "/bin/sh", script, host, user, port, session, NULL };
    gchar *env[] = { "TERM=xterm", "LANG=en_US.UTF-8", "LC_ALL=en_US.UTF-8", NULL };
    GError *error = NULL;
    vte_terminal_reset(VTE_TERMINAL(terminal), TRUE, TRUE);
    connected = FALSE; message("Connecting...");
    if (!vte_terminal_fork_command_full(VTE_TERMINAL(terminal), VTE_PTY_NO_LASTLOG | VTE_PTY_NO_UTMP | VTE_PTY_NO_WTMP,
            base, argv, env, 0, NULL, NULL, &child, &error)) {
        child = 0; message(error ? error->message : "Unable to start SSH"); g_clear_error(&error);
    }
    g_free(script);
}

static void connect_clicked(GtkWidget *b, gpointer data) {
    (void)b; (void)data; editing = NULL;
    gtk_widget_hide(settings); gtk_widget_show(terminal_box); sync_keyboard();
    gtk_widget_grab_focus(terminal); connect_terminal();
}

static gboolean focus_entry(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)event; (void)data; editing = GTK_EDITABLE(widget); return FALSE;
}

static void settings_page(GtkNotebook *book, GtkNotebookPage *page, guint number, gpointer data) {
    (void)book; (void)page; (void)data;
    if (!gtk_widget_get_visible(settings)) return;
    editing = NULL;
    gboolean available = number == 1;
    if (available) keyboard_visible = TRUE;
    gtk_widget_set_visible(keyboard_bar, available);
    gtk_widget_set_visible(keyboard, available && keyboard_visible);
    if (available) {
        gtk_widget_queue_draw(keyboard_chevron);
        atk_object_set_name(gtk_widget_get_accessible(keyboard_toggle), "Hide keyboard");
        gtk_widget_grab_focus(fields[0]);
    }
}

static void language_toggled(GtkToggleButton *button, gpointer data) {
    guint index = GPOINTER_TO_UINT(data), count = 0;
    Layout *layout = g_ptr_array_index(layouts, index);
    gboolean enabled = gtk_toggle_button_get_active(button);
    for (guint i = 0; i < layouts->len; i++) count += ((Layout *)g_ptr_array_index(layouts, i))->enabled;
    if (!enabled && count == 1 && layout->enabled) {
        gtk_toggle_button_set_active(button, TRUE); message("Keep at least one keyboard enabled."); return;
    }
    layout->enabled = enabled;
    if (!((Layout *)g_ptr_array_index(layouts, language))->enabled) language = layout_next(layouts, language);
    memset(&input, 0, sizeof input); symbols = 0; navigation = FALSE;
    save_config(); draw_keyboard();
}

static void settings_clicked(GtkWidget *b, gpointer data) {
    (void)b; (void)data; editing = NULL;
    const gchar *values[] = {host, user, port, session};
    for (guint i = 0; i < 4; i++) gtk_entry_set_text(GTK_ENTRY(fields[i]), values[i]);
    gtk_widget_hide(terminal_box); gtk_widget_show(settings);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(pages), 0);
    sync_keyboard();
}

static void settings_back(GtkWidget *b, gpointer data) {
    (void)b; (void)data; editing = NULL;
    message(connected ? "Connected" : child ? "Connecting..." : "Disconnected");
    gtk_widget_hide(settings); gtk_widget_show(terminal_box); sync_keyboard(); gtk_widget_grab_focus(terminal);
}

static void program_clicked(GtkWidget *button, gpointer data) {
    settings_clicked(button, data);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(pages), 2);
    sync_keyboard();
}

static void program_action(GtkWidget *button, gpointer data) {
    const gchar *sequence = data;
    settings_back(button, NULL);
    if (!child) { message("No connected program."); return; }
    memset(&input, 0, sizeof input);
    vte_terminal_feed_child(VTE_TERMINAL(terminal), sequence, strlen(sequence));
    draw_keyboard();
}

static void settings_save(GtkWidget *b, gpointer data) {
    const gchar *values[4];
    for (guint i = 0; i < 4; i++) values[i] = gtk_entry_get_text(GTK_ENTRY(fields[i]));
    if (!profile_valid(values[0], values[1], values[2], values[3])) { message("Check host, user, port (1-65535) and session name."); return; }
    g_free(host); g_free(user); g_free(port); g_free(session);
    host = g_strdup(values[0]); user = g_strdup(values[1]); port = g_strdup(values[2]); session = g_strdup(values[3]);
    save_config(); settings_back(b, data); connect_terminal();
}

static void font_change(GtkWidget *b, gpointer data) {
    (void)b; font_size = CLAMP(font_size + GPOINTER_TO_INT(data), 6, 14);
    update_font(); save_config();
}

static void close_app(GtkWidget *b, gpointer data) {
    (void)b; (void)data; reconnecting = FALSE;
    if (child) kill(child, SIGHUP);
    keyboard_grab(NULL, FALSE); gtk_main_quit();
}

static gboolean delete_window(GtkWidget *w, GdkEvent *event, gpointer data) {
    (void)event; close_app(w, data); return TRUE;
}

static gboolean initial_connect(gpointer data) { (void)data; connect_terminal(); return FALSE; }

int main(int argc, char **argv) {
    gchar executable[4096]; ssize_t n = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (n < 0) return 1;
    executable[n] = 0; gchar *bin = g_path_get_dirname(executable); base = g_path_get_dirname(bin); g_free(bin);
    gchar *layout_dir = path_for("layouts");
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return input_check(layout_dir);
    gtk_init(&argc, &argv);
    conf = g_new0(KTconf, 1); orientation_init();
    layouts = g_ptr_array_new();
    GDir *dir = g_dir_open(layout_dir, 0, NULL);
    if (!dir) { g_printerr("Keyboard directory is missing\n"); return 1; }
    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        if (name[0] == '.' || !g_str_has_suffix(name, ".ini")) continue;
        Layout *l = g_new0(Layout, 1); gchar *path = g_build_filename(layout_dir, name, NULL); GError *error = NULL;
        if (!layout_load(l, path, &error)) { g_printerr("%s\n", error->message); return 1; }
        g_ptr_array_add(layouts, l); g_free(path);
    }
    g_dir_close(dir);
    if (!layouts->len) { g_printerr("No keyboard layouts found\n"); return 1; }
    g_ptr_array_sort(layouts, compare_layouts);
    g_free(layout_dir);
    GKeyFile *f = g_key_file_new(); gchar *config_path = path_for("config/settings.ini");
    g_key_file_load_from_file(f, config_path, 0, NULL);
    host = g_key_file_get_string(f, "connection", "host", NULL); if (!host) host = g_strdup("");
    user = g_key_file_get_string(f, "connection", "user", NULL); if (!user) user = g_strdup("");
    port = g_key_file_get_string(f, "connection", "port", NULL); if (!port) port = g_strdup("22");
    session = g_key_file_get_string(f, "connection", "session", NULL); if (!session) session = g_strdup("kindle");
    gint saved_font = g_key_file_get_integer(f, "display", "font", NULL); if (saved_font >= 6 && saved_font <= 14) font_size = saved_font;
    gchar *saved_layout = g_key_file_get_string(f, "keyboard", "layout", NULL);
    for (guint i = 0; i < layouts->len; i++) if (!g_strcmp0(saved_layout, ((Layout *)g_ptr_array_index(layouts, i))->id)) language = i;
    gsize enabled_count = 0;
    gchar **enabled = g_key_file_get_string_list(f, "keyboard", "enabled", &enabled_count, NULL);
    const gchar *defaults[] = {"en.ini", saved_layout ? saved_layout : "en.ini"};
    layouts_select(layouts, enabled ? (const gchar *const *)enabled : defaults, enabled ? enabled_count : 2);
    if (!((Layout *)g_ptr_array_index(layouts, language))->enabled) language = layout_next(layouts, language);
    g_strfreev(enabled);
    g_free(saved_layout); g_free(config_path); g_key_file_free(f);
    gtk_rc_parse_string("style \"ink\" { font_name = \"Sans 8\" bg[NORMAL] = \"#ffffff\" bg[PRELIGHT] = \"#ffffff\" bg[ACTIVE] = \"#cccccc\" fg[NORMAL] = \"#000000\" xthickness = 2 ythickness = 2 GtkCheckButton::indicator-size = 32 GtkCheckButton::indicator-spacing = 14 } widget_class \"*\" style \"ink\" "
        "style \"small\" { font_name = \"Sans 6\" } widget \"*.status\" style \"small\" "
        "style \"action\" { font_name = \"Sans 7\" } widget \"*.action-key*\" style \"action\" "
        "style \"active\" { font_name = \"Sans Bold 7\" bg[NORMAL] = \"#000000\" fg[NORMAL] = \"#ffffff\" } widget \"*.active-key*\" style \"active\" "
        "style \"brand\" { font_name = \"Sans Bold 9\" } widget \"*.brand\" style \"brand\"");
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "L:A_N:application_ID:local.inkterm_PC:N_O:URL");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    GtkWidget *box = gtk_vbox_new(FALSE, 0); gtk_container_add(GTK_CONTAINER(window), box);
    GtkWidget *header = gtk_hbox_new(FALSE, 16); gtk_widget_set_size_request(header, -1, 100);
    gtk_container_set_border_width(GTK_CONTAINER(header), 12);
    gtk_box_pack_start(GTK_BOX(box), header, FALSE, FALSE, 0);
    GtkWidget *identity = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), identity, TRUE, TRUE, 8);
    GtkWidget *brand = gtk_label_new("InkTerm"); gtk_widget_set_name(brand, "brand");
    gtk_misc_set_alignment(GTK_MISC(brand), 0, 0.5); gtk_box_pack_start(GTK_BOX(identity), brand, TRUE, TRUE, 0);
    status = gtk_label_new("Ready"); gtk_widget_set_name(status, "status");
    gtk_misc_set_alignment(GTK_MISC(status), 0, 0.5); gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(identity), status, FALSE, FALSE, 0);
    GtkWidget *program_button = button_add(header, "Program", G_CALLBACK(program_clicked), NULL, FALSE);
    gtk_widget_set_size_request(program_button, 160, 76);
    gtk_button_set_relief(GTK_BUTTON(program_button), GTK_RELIEF_NONE);
    GtkWidget *settings_button = button_add(header, "Settings", G_CALLBACK(settings_clicked), NULL, FALSE);
    gtk_widget_set_size_request(settings_button, 164, 76);
    gtk_button_set_relief(GTK_BUTTON(settings_button), GTK_RELIEF_NONE);
    GtkWidget *close_button = button_add(header, "Close", G_CALLBACK(close_app), NULL, FALSE);
    gtk_widget_set_size_request(close_button, 124, 76);
    gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(box), gtk_hseparator_new(), FALSE, FALSE, 4);
    terminal_box = gtk_vbox_new(FALSE, 0); gtk_box_pack_start(GTK_BOX(box), terminal_box, TRUE, TRUE, 0);
    terminal = vte_terminal_new(); gtk_box_pack_start(GTK_BOX(terminal_box), terminal, TRUE, TRUE, 0);
    GdkColor black = {0,0,0,0}, white = {0,65535,65535,65535};
    vte_terminal_set_colors(VTE_TERMINAL(terminal), &black, &white, NULL, 0);
    vte_terminal_set_encoding(VTE_TERMINAL(terminal), "UTF-8");
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal), 2000);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_OFF);
    settings = gtk_vbox_new(FALSE, 12); gtk_container_set_border_width(GTK_CONTAINER(settings), 16);
    gtk_box_pack_start(GTK_BOX(box), settings, TRUE, TRUE, 0);
    pages = gtk_notebook_new(); gtk_notebook_set_show_border(GTK_NOTEBOOK(pages), FALSE); gtk_box_pack_start(GTK_BOX(settings), pages, TRUE, TRUE, 0);
    GtkWidget *keyboard_settings = gtk_vbox_new(FALSE, 12), *connection_settings = gtk_vbox_new(FALSE, 8);
    gtk_container_set_border_width(GTK_CONTAINER(keyboard_settings), 20);
    gtk_container_set_border_width(GTK_CONTAINER(connection_settings), 16);
    GtkWidget *tab = gtk_label_new("Keyboard"); gtk_widget_set_size_request(tab, 220, 64);
    gtk_notebook_append_page(GTK_NOTEBOOK(pages), keyboard_settings, tab);
    tab = gtk_label_new("Connection"); gtk_widget_set_size_request(tab, 220, 64);
    gtk_notebook_append_page(GTK_NOTEBOOK(pages), connection_settings, tab);
    GtkWidget *program_settings = gtk_vbox_new(FALSE, 20);
    gtk_container_set_border_width(GTK_CONTAINER(program_settings), 20);
    tab = gtk_label_new("Program"); gtk_widget_set_size_request(tab, 220, 64);
    gtk_notebook_append_page(GTK_NOTEBOOK(pages), program_settings, tab);
    GtkWidget *explanation = gtk_label_new("Send a control key to the running program.");
    gtk_misc_set_alignment(GTK_MISC(explanation), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(program_settings), explanation, FALSE, FALSE, 12);
    const gchar *action_labels[] = {"Interrupt program   Ctrl+C", "Send end of input   Ctrl+D", "Suspend program   Ctrl+Z"};
    const gchar *action_bytes[] = {"\003", "\004", "\032"};
    for (guint i = 0; i < G_N_ELEMENTS(action_labels); i++) {
        GtkWidget *row = gtk_hbox_new(FALSE, 0);
        gtk_box_pack_start(GTK_BOX(program_settings), row, FALSE, FALSE, 0);
        GtkWidget *action = button_add(row, action_labels[i], G_CALLBACK(program_action), (gpointer)action_bytes[i], TRUE);
        gtk_widget_set_size_request(action, -1, 96);
    }
    explanation = gtk_label_new("Ctrl+Z suspends; use fg to resume.\nPrograms may handle these keys differently.");
    gtk_misc_set_alignment(GTK_MISC(explanation), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(program_settings), explanation, FALSE, FALSE, 12);
    GtkWidget *hint = gtk_label_new("Choose the languages you type in."); gtk_misc_set_alignment(GTK_MISC(hint), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(keyboard_settings), hint, FALSE, FALSE, 8);
    language_options = gtk_vbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(keyboard_settings), language_options, FALSE, FALSE, 0);
    const gchar *ids[] = {"en.ini", "ru.ini", "de.ini", "fr.ini", "es.ini"};
    const gchar *names[] = {"English", "Русский", "Deutsch", "Français", "Español"};
    for (guint i = 0; i < layouts->len; i++) {
        Layout *l = g_ptr_array_index(layouts, i); const gchar *title = l->name;
        for (guint j = 0; j < G_N_ELEMENTS(ids); j++) if (!strcmp(l->id, ids[j])) title = names[j];
        GtkWidget *toggle = gtk_check_button_new_with_label(title);
        gtk_widget_set_size_request(toggle, -1, 80);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), l->enabled);
        g_signal_connect(toggle, "toggled", G_CALLBACK(language_toggled), GUINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(language_options), toggle, FALSE, FALSE, 0);
    }
    hint = gtk_label_new("Shift: one capital. Double-tap: Caps Lock.\nTap Shift again to unlock.");
    gtk_misc_set_alignment(GTK_MISC(hint), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(keyboard_settings), hint, FALSE, FALSE, 12);
    for (guint i = 0; i < 4; i++) {
        GtkWidget *row = gtk_hbox_new(FALSE, 8), *label = gtk_label_new(field_names[i]);
        gtk_widget_set_size_request(label, 140, -1); gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
        fields[i] = gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(fields[i]), 128);
        gtk_box_pack_start(GTK_BOX(row), fields[i], TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(connection_settings), row, FALSE, FALSE, 0);
        g_signal_connect(fields[i], "focus-in-event", G_CALLBACK(focus_entry), NULL);
    }
    GtkWidget *font_row = gtk_hbox_new(FALSE, 8); font_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(font_row), font_label, TRUE, TRUE, 0);
    gtk_misc_set_alignment(GTK_MISC(font_label), 0, 0.5);
    GtkWidget *smaller = button_add(font_row, "A-", G_CALLBACK(font_change), GINT_TO_POINTER(-1), FALSE);
    GtkWidget *larger = button_add(font_row, "A+", G_CALLBACK(font_change), GINT_TO_POINTER(1), FALSE);
    gtk_widget_set_size_request(smaller, 100, 80); gtk_widget_set_size_request(larger, 100, 80);
    gtk_box_pack_start(GTK_BOX(keyboard_settings), font_row, FALSE, FALSE, 12);
    GtkWidget *actions = gtk_hbox_new(FALSE, 12); gtk_box_pack_start(GTK_BOX(connection_settings), actions, FALSE, FALSE, 8);
    button_add(actions, "Save & connect", G_CALLBACK(settings_save), NULL, TRUE);
    button_add(actions, "Reconnect", G_CALLBACK(connect_clicked), NULL, TRUE);
    GtkWidget *done = gtk_hbox_new(FALSE, 0); gtk_box_pack_end(GTK_BOX(settings), done, FALSE, FALSE, 0);
    GtkWidget *done_button = button_add(done, "Done", G_CALLBACK(settings_back), NULL, TRUE);
    gtk_widget_set_size_request(done_button, -1, 80);
    keyboard_bar = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(keyboard_bar), 6);
    gtk_box_pack_end(GTK_BOX(box), keyboard_bar, FALSE, FALSE, 0);
    keyboard_toggle = gtk_button_new();
    gtk_button_set_focus_on_click(GTK_BUTTON(keyboard_toggle), FALSE);
    gtk_button_set_relief(GTK_BUTTON(keyboard_toggle), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(keyboard_toggle, 216, 76);
    keyboard_chevron = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(keyboard_toggle), keyboard_chevron);
    g_signal_connect(keyboard_chevron, "expose-event", G_CALLBACK(draw_chevron), NULL);
    g_signal_connect_after(keyboard_toggle, "expose-event", G_CALLBACK(outline_key), NULL);
    g_signal_connect(keyboard_toggle, "clicked", G_CALLBACK(toggle_keyboard), NULL);
    gtk_box_pack_end(GTK_BOX(keyboard_bar), keyboard_toggle, FALSE, FALSE, 3);
    keyboard = gtk_vbox_new(FALSE, 0); gtk_container_set_border_width(GTK_CONTAINER(keyboard), 6);
    gtk_box_pack_end(GTK_BOX(box), keyboard, FALSE, FALSE, 0);
    gint height = gdk_screen_get_height(gdk_screen_get_default());
    gtk_widget_set_size_request(keyboard, -1, MAX(480, height * 37 / 100));
    g_signal_connect(pages, "switch-page", G_CALLBACK(settings_page), NULL);
    draw_keyboard(); update_font();
    g_signal_connect(window, "delete-event", G_CALLBACK(delete_window), NULL);
    g_signal_connect(window, "visibility-notify-event", G_CALLBACK(grab_keyboard_cb), NULL);
    g_signal_connect(terminal, "child-exited", G_CALLBACK(child_exited), NULL);
    g_signal_connect(terminal, "window-title-changed", G_CALLBACK(title_changed), NULL);
    gtk_widget_add_events(window, GDK_VISIBILITY_NOTIFY_MASK);
    gtk_widget_show_all(window); gtk_widget_hide(settings); sync_keyboard();
    gtk_window_maximize(GTK_WINDOW(window)); gtk_widget_grab_focus(terminal);
    g_timeout_add(300, initial_connect, NULL);
    gtk_main();
    orientation_restore(); gtk_widget_destroy(window);
    return 0;
}
