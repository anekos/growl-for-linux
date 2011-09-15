/* Copyright 2011 by Yasuhiro Matsumoto
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <gtk/gtk.h>
#ifdef _WIN32
# include <gdk/gdkwin32.h>
#endif
#include <ctype.h>
#include <stdlib.h>
#include <memory.h>
#include <curl/curl.h>
#include "../../gol.h"
#include "../../plugins/from_url.h"
#include "display_nico2.xpm"


static GList* notifications = NULL;

typedef struct {
  NOTIFICATION_INFO* ni;
  gint x, y;
  gint timeout;
  GtkWidget* popup;
  gint width;
  gint height;
  gboolean sticky;
  gboolean hover;
} DISPLAY_INFO;

static void
free_display_info(DISPLAY_INFO* di) {
  g_free(di->ni->title);
  g_free(di->ni->text);
  g_free(di->ni->icon);
  g_free(di->ni->url);
  g_free(di->ni);
  g_free(di);
}

static gboolean
open_url(const gchar* url) {
#if defined(_WIN32)
  return (int) ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOW) > 32;
#elif defined(MACOSX)
  GError* error = NULL;
  const gchar *argv[] = {"open", (gchar*) url, NULL};
  return g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
#else
  GError* error = NULL;
  gchar *argv[] = {"xdg-open", (gchar*) url, NULL};
  return g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
#endif
}

static void
display_clicked(GtkWidget* widget, GdkEvent* event, gpointer user_data) {
  DISPLAY_INFO* di = (DISPLAY_INFO*) user_data;
  if (di->timeout >= 30) di->timeout = 30;
  if (di->ni->url && *di->ni->url) open_url(di->ni->url);
}

static void
display_enter(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
  ((DISPLAY_INFO*) user_data)->hover = TRUE;
}

static void
display_leave(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
  ((DISPLAY_INFO*) user_data)->hover = FALSE;
}

static gboolean
display_animation_func(gpointer data) {
  DISPLAY_INFO* di = (DISPLAY_INFO*) data;

  if (di->x + di->width < 0) {
    gtk_widget_destroy(di->popup);
    di->popup = NULL;
    notifications = g_list_remove(notifications, di);
    free_display_info(di);
    return FALSE;
  }

  if (!di->hover) di->x -= 10;
  gdk_window_move(di->popup->window, di->x, di->y);
  return TRUE;
}

G_MODULE_EXPORT gboolean
display_show(NOTIFICATION_INFO* ni) {
  GdkColor color;
  GtkWidget* fixed;
  GtkWidget* image = NULL;
  GdkScreen* screen;
  gint monitor_num;
  GdkRectangle rect;

  DISPLAY_INFO* di = g_new0(DISPLAY_INFO, 1);
  if (!di) {
    perror("g_new0");
    return FALSE;
  }
  di->ni = ni;

  screen = gdk_screen_get_default();
  monitor_num = gdk_screen_get_primary_monitor(screen);
  gdk_screen_get_monitor_geometry(screen, monitor_num, &rect);

  notifications = g_list_append(notifications, di);

  di->popup = gtk_window_new(GTK_WINDOW_POPUP);
  gtk_window_set_title(GTK_WINDOW(di->popup), "growl-for-linux");
  gtk_window_set_resizable(GTK_WINDOW(di->popup), TRUE);
  gtk_window_set_decorated(GTK_WINDOW(di->popup), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(di->popup), TRUE);

  const char* colors[] = { "red", "blue", "orange" };
  gdk_color_parse(colors[rand() % 3], &color);
  gtk_widget_modify_bg(di->popup, GTK_STATE_NORMAL, &color);

  gtk_window_stick(GTK_WINDOW(di->popup));

  GtkWidget* ebox = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
  gtk_container_add(GTK_CONTAINER(di->popup), ebox);

  fixed = gtk_fixed_new();
  gtk_container_set_border_width(GTK_CONTAINER(fixed), 0);
  gtk_container_add(GTK_CONTAINER(ebox), fixed);

  if (di->ni->icon && *di->ni->icon) {
    GdkPixbuf* pixbuf;
    if (di->ni->local) {
      gchar* newurl = g_filename_from_uri(di->ni->icon, NULL, NULL);
      GError* error = NULL;
      pixbuf = gdk_pixbuf_new_from_file(newurl ? newurl : di->ni->icon, &error);
      if (newurl) g_free(newurl);
    } else
      pixbuf = pixbuf_from_url(di->ni->icon, NULL);
    if (pixbuf) {
      GdkPixbuf* tmp = gdk_pixbuf_scale_simple(pixbuf, 32, 32, GDK_INTERP_TILES);
      if (tmp) {
        g_object_unref(pixbuf);
        pixbuf = tmp;
      }
      image = gtk_image_new_from_pixbuf(pixbuf);
      gtk_container_add(GTK_CONTAINER(fixed), image);
      g_object_unref(pixbuf);
    }
  }

  PangoFontDescription* font_desc = pango_font_description_new();
  pango_font_description_set_family(font_desc, "Sans");
  pango_font_description_set_size(font_desc, 20 * PANGO_SCALE);

  PangoContext* context = gtk_widget_get_pango_context(di->popup) ;
  PangoLayout* layout = pango_layout_new(context);

  gchar* text = g_strconcat(di->ni->title, "\n", di->ni->text, NULL);
  pango_layout_set_text(layout, text, -1);
  g_free(text);
  pango_layout_set_font_description(layout, font_desc);
  pango_layout_get_pixel_size(layout, &di->width, &di->height);

  di->x = rect.width;
  di->y = rect.y + rand() % (rect.height - di->height);
  di->width += 32 + 5;

  if (image)
    gtk_fixed_move(GTK_FIXED(fixed), image, 0, di->height / 2 - 16);
  GdkBitmap* bitmap = gdk_pixmap_new(di->popup->window, di->width, di->height, 1);
  GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(bitmap));
  GdkColormap* colormap = gdk_colormap_get_system();
  gdk_gc_set_colormap(gc, colormap);

  gdk_color_parse("black", &color);
  gdk_colormap_alloc_color(colormap, &color, TRUE, TRUE);
  gdk_gc_set_foreground (gc, &color);
  gdk_draw_rectangle(bitmap, gc, TRUE, 0, 0, di->width, di->height);

  gdk_color_parse("white", &color);
  gdk_colormap_alloc_color(colormap, &color, TRUE, TRUE);
  gdk_gc_set_foreground (gc, &color);
  if (image)
    gdk_draw_rectangle(bitmap, gc, TRUE, 0, di->height / 2 - 16, 32, 32);
  gdk_draw_layout(bitmap, gc, 32 + 5, 0, layout);

  pango_font_description_free(font_desc);

  g_signal_connect(G_OBJECT(ebox), "button-press-event", G_CALLBACK(display_clicked), di);
  g_signal_connect(G_OBJECT(ebox), "enter-notify-event", G_CALLBACK(display_enter), di);
  g_signal_connect(G_OBJECT(ebox), "leave-notify-event", G_CALLBACK(display_leave), di);

  gtk_window_move(GTK_WINDOW(di->popup), di->x, di->y);
  gtk_widget_show_all(di->popup);

  gtk_widget_set_size_request(fixed, di->width, di->height);
  gdk_window_set_back_pixmap(di->popup->window, NULL, FALSE);
  gdk_window_shape_combine_mask(di->popup->window, bitmap, 0, 0);

  g_object_unref(gc);
  g_object_unref(layout);
  g_object_unref(context);
  g_object_unref(bitmap);

  g_object_ref(di->popup);
  g_timeout_add(100, display_animation_func, di);

  return FALSE;
}

G_MODULE_EXPORT gboolean
display_init() {
  return TRUE;
}

G_MODULE_EXPORT void
display_term() {
}

G_MODULE_EXPORT const gchar*
display_name() {
  return "Nico2";
}

G_MODULE_EXPORT const gchar*
display_description() {
  return
    "<span size=\"large\"><b>Nico2</b></span>\n"
    "<span>This is nico2 notification display.</span>\n"
    "<span>Slide notification from right to left similar to nico nico douga.</span>\n";
}

G_MODULE_EXPORT char**
display_thumbnail() {
  return display_nico2;
}

// vim:set et sw=2 ts=2 ai:
