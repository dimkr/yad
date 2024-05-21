/*
 * This file is part of YAD.
 *
 * YAD is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * YAD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with YAD. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2008-2019, Victor Ananjevsky <ananasik@gmail.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#ifndef G_OS_WIN32
# include <sys/shm.h>
# include <gdk/gdkx.h>
#endif

#include "yad.h"

#ifdef HAVE_GTK_LAYER_SHELL
#  include <gtk-layer-shell.h>
#endif

YadOptions options;
static GtkWidget *dialog = NULL;
static GtkWidget *text = NULL;

static gint ret = YAD_RESPONSE_ESC;

static gboolean is_x11 = FALSE;

YadNTabs *tabs;

#ifndef G_OS_WIN32
static void
sa_usr1 (gint sig)
{
  if (options.plug != -1)
    yad_print_result ();
  else
    yad_exit (options.data.def_resp);
}

static void
sa_usr2 (gint sig)
{
  if (options.plug != -1)
    gtk_main_quit ();
  else
    yad_exit (YAD_RESPONSE_CANCEL);
}
#endif

static gboolean
keys_cb (GtkWidget *w, GdkEventKey *ev, gpointer d)
{
  if (options.plug != -1)
    return FALSE;

  switch (ev->keyval)
    {
    case GDK_KEY_Escape:
      if (options.data.escape_ok)
          yad_exit (options.data.def_resp);
      else if (!options.data.no_escape)
         yad_exit (YAD_RESPONSE_ESC);
      return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      if (ev->state & GDK_CONTROL_MASK)
        {
          yad_exit (options.data.def_resp);
          return TRUE;
        }
     }
   return FALSE;
}

static void
btn_cb (GtkWidget *b, gchar *cmd)
{
  if (cmd)
    run_command_async (cmd);
  else
    {
      gint resp = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (b), "resp"));
      yad_exit (resp);
    }
}

static gboolean
timeout_cb (gpointer data)
{
  static guint count = 1;
  GtkWidget *w = (GtkWidget *) data;

  if (options.data.timeout < count)
    {
      yad_exit (YAD_RESPONSE_TIMEOUT);
      return FALSE;
    }

  if (w)
    {
      gdouble percent = ((gdouble) options.data.timeout - count) / (gdouble) options.data.timeout;
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (w), percent);
      if (settings.show_remain)
        {
          gchar *lbl = g_strdup_printf (_("%d sec"), options.data.timeout - count);
          gtk_progress_bar_set_text (GTK_PROGRESS_BAR (w), lbl);
          g_free (lbl);
        }
    }

  count++;

  return TRUE;
}

static gboolean
unfocus_cb (GtkWidget *w, GdkEventFocus *ev, gpointer d)
{
  if (options.data.close_on_unfocus)
    gtk_main_quit ();
  return FALSE;
}

#if !GTK_CHECK_VERSION(3,0,0)
static void
text_size_allocate_cb (GtkWidget * w, GtkAllocation * al, gpointer data)
{
  PangoLayout *pl = gtk_label_get_layout (GTK_LABEL (w));

  if (pango_layout_is_wrapped (pl))
    gtk_widget_set_size_request (w, al->width, -1);
}
#endif

void
yad_exit (gint id)
{
  if ((options.mode == YAD_MODE_FILE) && !(id & 1))
    {
      /* show custom confirmation dialog */
      if (!file_confirm_overwrite (dialog))
        return;
    }

  ret = id;
  gtk_main_quit ();
}

static GtkWidget *
create_layout (GtkWidget *dlg)
{
  GtkWidget *image, *mw, *imw, *layout, *exp, *box;

  layout = image = mw = exp = NULL;

  /* create image */
  if (options.data.dialog_image)
    {
      GdkPixbuf *pb = NULL;

      pb = get_pixbuf (options.data.dialog_image, YAD_BIG_ICON, FALSE);
      image = gtk_image_new_from_pixbuf (pb);
      if (pb)
        g_object_unref (pb);

      gtk_widget_set_name (image, "yad-dialog-image");
#if !GTK_CHECK_VERSION(3,0,0)
      gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
#else
      gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (image, GTK_ALIGN_START);
#endif
    }

  /* create text label */
  if (options.data.dialog_text)
    {
      /* for dnd's tooltip we don't need text label */
      if (options.mode != YAD_MODE_DND || !options.dnd_data.tooltip)
        {
          gchar *buf = g_strcompress (options.data.dialog_text);

          text = gtk_label_new (NULL);
          if (!options.data.no_markup)
            gtk_label_set_markup (GTK_LABEL (text), buf);
          else
            gtk_label_set_text (GTK_LABEL (text), buf);
          g_free (buf);

          gtk_widget_set_name (text, "yad-dialog-label");
          gtk_label_set_line_wrap (GTK_LABEL (text), TRUE);
          gtk_label_set_selectable (GTK_LABEL (text), options.data.selectable_labels);
          gtk_label_set_justify (GTK_LABEL (text), options.data.text_align);
#if !GTK_CHECK_VERSION(3,0,0)
          gtk_widget_set_state (text, GTK_STATE_NORMAL);
          gtk_misc_set_alignment (GTK_MISC (text), options.data.text_align, 0.5);
          if (!options.data.fixed)
            g_signal_connect (G_OBJECT (text), "size-allocate", G_CALLBACK (text_size_allocate_cb), NULL);
#else
          gtk_widget_set_state_flags (text, GTK_STATE_NORMAL, FALSE);
          gtk_label_set_xalign (GTK_LABEL (text), options.data.text_align);
#endif
          gtk_widget_set_can_focus (text, FALSE);
        }
    }

  /* create main widget */
  switch (options.mode)
    {
    case YAD_MODE_CALENDAR:
      mw = calendar_create_widget (dlg);
      break;
    case YAD_MODE_COLOR:
      mw = color_create_widget (dlg);
      break;
    case YAD_MODE_ENTRY:
      mw = entry_create_widget (dlg);
      break;
    case YAD_MODE_FILE:
      mw = file_create_widget (dlg);
      break;
    case YAD_MODE_FONT:
      mw = font_create_widget (dlg);
      break;
    case YAD_MODE_FORM:
      mw = form_create_widget (dlg);
      break;
#ifdef HAVE_HTML
    case YAD_MODE_HTML:
      mw = html_create_widget (dlg);
      break;
#endif
    case YAD_MODE_ICONS:
      mw = icons_create_widget (dlg);
      break;
    case YAD_MODE_LIST:
      mw = list_create_widget (dlg);
      break;
    case YAD_MODE_NOTEBOOK:
      if (options.plug == -1)
        mw = notebook_create_widget (dlg);
      break;
    case YAD_MODE_PANED:
      if (options.plug == -1)
        mw = paned_create_widget (dlg);
      break;
    case YAD_MODE_PICTURE:
      mw = picture_create_widget (dlg);
      break;
    case YAD_MODE_PROGRESS:
      mw = progress_create_widget (dlg);
      break;
    case YAD_MODE_SCALE:
      mw = scale_create_widget (dlg);
      break;
    case YAD_MODE_TEXTINFO:
      mw = text_create_widget (dlg);
      break;
    default: ;
    }

  /* add expander */
  imw = NULL;
  if (mw && options.data.expander)
    {
      exp = gtk_expander_new_with_mnemonic (options.data.expander);
      gtk_expander_set_expanded (GTK_EXPANDER (exp), FALSE);
      if (mw)
        {
          gtk_container_add (GTK_CONTAINER (exp), mw);
          imw = exp;
        }
    }
  else
    imw = mw;

  /* create layout */
  if (options.data.image_on_top)
    {
#if !GTK_CHECK_VERSION(3,0,0)
      layout = gtk_vbox_new (FALSE, 5);
      box = gtk_hbox_new (FALSE, 5);
#else
      layout = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
      box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
#endif

      if (image)
        gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 2);
      if (text)
        gtk_box_pack_start (GTK_BOX (box), text, TRUE, TRUE, 2);

      gtk_box_pack_start (GTK_BOX (layout), box, FALSE, FALSE, 0);
      if (imw)
        gtk_box_pack_start (GTK_BOX (layout), imw, TRUE, TRUE, 0);
    }
  else
    {
#if !GTK_CHECK_VERSION(3,0,0)
      layout = gtk_hbox_new (FALSE, 5);
      box = gtk_vbox_new (FALSE, 5);
#else
      layout = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
      box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
#endif

      if (text)
        gtk_box_pack_start (GTK_BOX (box), text, FALSE, FALSE, 0);
      if (imw)
        gtk_box_pack_start (GTK_BOX (box), imw, TRUE, TRUE, 0);

      if (image)
        gtk_box_pack_start (GTK_BOX (layout), image, FALSE, FALSE, 0);
      gtk_box_pack_start (GTK_BOX (layout), box, TRUE, TRUE, 0);
    }

  if (options.mode == YAD_MODE_DND)
    dnd_init (layout);

  return layout;
}

static GtkWidget *
create_dialog (void)
{
  GtkWidget *dlg, *vbox, *layout;
#ifdef HAVE_GTK_LAYER_SHELL
  GtkLayerShellLayer layer = GTK_LAYER_SHELL_LAYER_ENTRY_NUMBER;
  GtkLayerShellEdge edge = GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER;
  GtkLayerShellEdge corner = GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER;
#endif

  /* create dialog window */
  dlg = gtk_window_new (GTK_WINDOW_TOPLEVEL);

#ifdef HAVE_GTK_LAYER_SHELL
  if (options.data.layer)
    {
      if (strcasecmp (options.data.layer, "background") == 0)
        layer = GTK_LAYER_SHELL_LAYER_BACKGROUND;
      else if (strcasecmp (options.data.layer, "bottom") == 0)
        layer = GTK_LAYER_SHELL_LAYER_BOTTOM;
      else if (strcasecmp (options.data.layer, "top") == 0)
        layer = GTK_LAYER_SHELL_LAYER_TOP;
      else if (strcasecmp (options.data.layer, "overlay") == 0)
        layer = GTK_LAYER_SHELL_LAYER_OVERLAY;
   }

  if (options.data.edge)
    {
      if (strcasecmp (options.data.edge, "top") == 0)
        edge = GTK_LAYER_SHELL_EDGE_TOP;
      else if (strcasecmp (options.data.edge, "bottom") == 0)
        edge = GTK_LAYER_SHELL_EDGE_BOTTOM;
      else if (strcasecmp (options.data.edge, "left") == 0)
        edge = GTK_LAYER_SHELL_EDGE_LEFT;
      else if (strcasecmp (options.data.edge, "right") == 0)
        edge = GTK_LAYER_SHELL_EDGE_RIGHT;
      else if (strcasecmp (options.data.edge, "topleft") == 0)
        {
          edge = GTK_LAYER_SHELL_EDGE_LEFT;
          corner = GTK_LAYER_SHELL_EDGE_TOP;
        }
      else if (strcasecmp (options.data.edge, "topright") == 0)
        {
          edge = GTK_LAYER_SHELL_EDGE_RIGHT;
          corner = GTK_LAYER_SHELL_EDGE_TOP;
        }
      else if (strcasecmp (options.data.edge, "bottomleft") == 0)
        {
          edge = GTK_LAYER_SHELL_EDGE_LEFT;
          corner = GTK_LAYER_SHELL_EDGE_BOTTOM;
        }
      else if (strcasecmp (options.data.edge, "bottomright") == 0)
        {
          edge = GTK_LAYER_SHELL_EDGE_RIGHT;
          corner = GTK_LAYER_SHELL_EDGE_BOTTOM;
        }
    }

     if (layer != GTK_LAYER_SHELL_LAYER_ENTRY_NUMBER || edge != GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER || corner != GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER)
       gtk_layer_init_for_window (GTK_WINDOW (dlg));

     if (layer != GTK_LAYER_SHELL_LAYER_ENTRY_NUMBER)
       gtk_layer_set_layer (GTK_WINDOW (dlg), layer);

     if (edge != GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER)
       {
         gtk_layer_set_exclusive_zone (GTK_WINDOW (dlg), 0);
         gtk_layer_set_margin (GTK_WINDOW (dlg), GTK_LAYER_SHELL_EDGE_LEFT, 20);
         gtk_layer_set_margin (GTK_WINDOW (dlg), GTK_LAYER_SHELL_EDGE_RIGHT, 20);
         gtk_layer_set_margin (GTK_WINDOW (dlg), GTK_LAYER_SHELL_EDGE_TOP, 10);
         gtk_layer_set_margin (GTK_WINDOW (dlg), GTK_LAYER_SHELL_EDGE_BOTTOM, 20);
         gtk_layer_set_anchor (GTK_WINDOW (dlg), edge, TRUE);
         if (corner != GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER)
           gtk_layer_set_anchor (GTK_WINDOW (dlg), corner, TRUE);
       }
#endif

  if (options.data.splash)
    gtk_window_set_type_hint (GTK_WINDOW (dlg), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
  gtk_window_set_title (GTK_WINDOW (dlg), options.data.dialog_title);
  gtk_widget_set_name (dlg, "yad-dialog-window");

  g_signal_connect (G_OBJECT (dlg), "delete-event", G_CALLBACK (gtk_main_quit), NULL);
  g_signal_connect (G_OBJECT (dlg), "key-press-event", G_CALLBACK (keys_cb), NULL);
  g_signal_connect (G_OBJECT (dlg), "focus-out-event", G_CALLBACK (unfocus_cb), NULL);

  /* set window icon */
  if (options.data.window_icon)
    {
      if (g_file_test (options.data.window_icon, G_FILE_TEST_EXISTS))
        gtk_window_set_icon_from_file (GTK_WINDOW (dlg), options.data.window_icon, NULL);
      else
        gtk_window_set_icon_name (GTK_WINDOW (dlg), options.data.window_icon);
    }

  /* set window borders */
  if (options.data.borders < 0)
    options.data.borders = 2;
  gtk_container_set_border_width (GTK_CONTAINER (dlg), (guint) options.data.borders);

  /* set window size and position */
  if (!options.data.maximized && !options.data.fullscreen)
    {
      if (options.data.center_keep)
        gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_CENTER_ALWAYS);
      else if (options.data.mouse)
        gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_MOUSE);
    }

  /* set window behavior */
  if (options.data.sticky)
    gtk_window_stick (GTK_WINDOW (dlg));
  gtk_window_set_keep_above (GTK_WINDOW (dlg), options.data.ontop);
  gtk_window_set_decorated (GTK_WINDOW (dlg), !options.data.undecorated);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dlg), options.data.skip_taskbar);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (dlg), options.data.skip_taskbar);
  gtk_window_set_accept_focus (GTK_WINDOW (dlg), options.data.focus);

  /* create box */
#if !GTK_CHECK_VERSION(3,0,0)
  vbox = gtk_vbox_new (FALSE, 2);
#else
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
#endif
  gtk_container_add (GTK_CONTAINER (dlg), vbox);

  layout = create_layout (dlg);

  /* create timeout indicator widget */
  if (options.data.timeout)
    {
      GtkWidget *cbox = NULL, *topb = NULL;

      if (G_LIKELY (options.data.to_indicator) && strcasecmp (options.data.to_indicator, "none") != 0)
        {
          topb = gtk_progress_bar_new ();
          gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (topb), 1.0);
          gtk_widget_set_name (topb, "yad-timeout-indicator");
        }

      /* add timeout indicator */
      if (topb)
        {
          if (strcasecmp (options.data.to_indicator, "top") == 0)
            {
#if !GTK_CHECK_VERSION(3,0,0)
              gtk_progress_bar_set_orientation (GTK_PROGRESS_BAR (topb), GTK_PROGRESS_LEFT_TO_RIGHT);
              cbox = gtk_vbox_new (FALSE, 0);
#else
              gtk_orientable_set_orientation (GTK_ORIENTABLE (topb), GTK_ORIENTATION_HORIZONTAL);
              cbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#endif
              gtk_box_pack_start (GTK_BOX (cbox), topb, FALSE, FALSE, 2);
              gtk_box_pack_end (GTK_BOX (cbox), layout, TRUE, TRUE, 0);
            }
          else if (strcasecmp (options.data.to_indicator, "bottom") == 0)
            {
#if !GTK_CHECK_VERSION(3,0,0)
              gtk_progress_bar_set_orientation (GTK_PROGRESS_BAR (topb), GTK_PROGRESS_LEFT_TO_RIGHT);
              cbox = gtk_vbox_new (FALSE, 0);
#else
              gtk_orientable_set_orientation (GTK_ORIENTABLE (topb), GTK_ORIENTATION_HORIZONTAL);
              cbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#endif
              gtk_box_pack_start (GTK_BOX (cbox), layout, TRUE, TRUE, 0);
              gtk_box_pack_end (GTK_BOX (cbox), topb, FALSE, FALSE, 2);
            }
          else if (strcasecmp (options.data.to_indicator, "left") == 0)
            {
#if !GTK_CHECK_VERSION(3,0,0)
              gtk_progress_bar_set_orientation (GTK_PROGRESS_BAR (topb), GTK_PROGRESS_BOTTOM_TO_TOP);
              cbox = gtk_hbox_new (FALSE, 0);
#else
              gtk_orientable_set_orientation (GTK_ORIENTABLE (topb), GTK_ORIENTATION_VERTICAL);
              gtk_progress_bar_set_inverted (GTK_PROGRESS_BAR (topb), TRUE);
              cbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
#endif
              gtk_box_pack_start (GTK_BOX (cbox), topb, FALSE, FALSE, 2);
              gtk_box_pack_end (GTK_BOX (cbox), layout, TRUE, TRUE, 0);
            }
          else if (strcasecmp (options.data.to_indicator, "right") == 0)
            {
#if !GTK_CHECK_VERSION(3,0,0)
              gtk_progress_bar_set_orientation (GTK_PROGRESS_BAR (topb), GTK_PROGRESS_BOTTOM_TO_TOP);
              cbox = gtk_hbox_new (FALSE, 0);
#else
              gtk_orientable_set_orientation (GTK_ORIENTABLE (topb), GTK_ORIENTATION_VERTICAL);
              gtk_progress_bar_set_inverted (GTK_PROGRESS_BAR (topb), TRUE);
              cbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
#endif
              gtk_box_pack_start (GTK_BOX (cbox), layout, TRUE, TRUE, 0);
              gtk_box_pack_end (GTK_BOX (cbox), topb, FALSE, FALSE, 2);
            }

          if (settings.show_remain)
            {
              gchar *lbl = g_strdup_printf (_("%d sec"), options.data.timeout);
#if GTK_CHECK_VERSION(3,0,0)
              gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (topb), TRUE);
#endif
              gtk_progress_bar_set_text (GTK_PROGRESS_BAR (topb), lbl);
              g_free (lbl);
            }
        }
      else
        {
#if !GTK_CHECK_VERSION(3,0,0)
          cbox = gtk_vbox_new (FALSE, 0);
#else
          cbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#endif
          gtk_box_pack_start (GTK_BOX (cbox), layout, TRUE, TRUE, 0);
        }

      if (cbox)
        gtk_box_pack_start (GTK_BOX (vbox), cbox, TRUE, TRUE, 0);

      /* set timeout handler */
      g_timeout_add_seconds (1, timeout_cb, topb);
    }
  else
    gtk_box_pack_start (GTK_BOX (vbox), layout, TRUE, TRUE, 0);

#ifdef HAVE_HTML
  /* enable no-buttons mode if --browser is specified and sets no custom buttons */
  if (options.mode == YAD_MODE_HTML && options.html_data.browser && !options.data.buttons)
    options.data.no_buttons = TRUE;
#endif

  if (!options.data.no_buttons)
    {
      GtkWidget *btn;
      /* create buttons container */
#if !GTK_CHECK_VERSION(3,0,0)
      GtkWidget *bbox = gtk_hbutton_box_new ();
#else
      GtkWidget *bbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
#endif
      gtk_container_set_border_width (GTK_CONTAINER (bbox), 2);
      gtk_box_set_spacing (GTK_BOX (bbox), 5);
      gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), options.data.buttons_layout);

      /* add buttons */
      if (options.data.buttons)
        {
          GSList *tmp = options.data.buttons;
          do
            {
              YadButton *b = (YadButton *) tmp->data;

              btn = gtk_button_new ();
              gtk_container_add (GTK_CONTAINER (btn), get_label (b->name, 2, btn));
              UNDEPR (gtk_button_set_alignment, GTK_BUTTON (btn), 0.5, 0.5);
              g_object_set_data (G_OBJECT (btn), "resp", GINT_TO_POINTER (b->response));
              g_signal_connect (G_OBJECT (btn), "clicked", G_CALLBACK (btn_cb), b->cmd);
              gtk_box_pack_start (GTK_BOX (bbox), btn, FALSE, FALSE, 0);

              tmp = tmp->next;
            }
          while (tmp != NULL);
        }
      else
        {
          if (options.mode == YAD_MODE_PROGRESS || options.mode == YAD_MODE_DND || options.mode == YAD_MODE_PICTURE)
            {
              /* add close button */
              SETUNDEPR (btn, gtk_button_new_from_stock, GTK_STOCK_CLOSE);
              g_object_set_data (G_OBJECT (btn), "resp", GINT_TO_POINTER (YAD_RESPONSE_OK));
              g_signal_connect (G_OBJECT (btn), "clicked", G_CALLBACK (btn_cb), NULL);
              gtk_box_pack_start (GTK_BOX (bbox), btn, FALSE, FALSE, 0);

            }
          else
            {
              gboolean b;
              SETUNDEPR (b, gtk_alternative_dialog_button_order, NULL);
              if (b)
                {
                  /* add ok button */
                  SETUNDEPR (btn, gtk_button_new_from_stock, GTK_STOCK_OK);
                  g_object_set_data (G_OBJECT (btn), "resp", GINT_TO_POINTER (YAD_RESPONSE_OK));
                  g_signal_connect (G_OBJECT (btn), "clicked", G_CALLBACK (btn_cb), NULL);
                  gtk_box_pack_start (GTK_BOX (bbox), btn, FALSE, FALSE, 0);

                  /* add cancel button */
                  SETUNDEPR (btn, gtk_button_new_from_stock, GTK_STOCK_CANCEL);
                  g_object_set_data (G_OBJECT (btn), "resp", GINT_TO_POINTER (YAD_RESPONSE_CANCEL));
                  g_signal_connect (G_OBJECT (btn), "clicked", G_CALLBACK (btn_cb), NULL);
                  gtk_box_pack_start (GTK_BOX (bbox), btn, FALSE, FALSE, 0);
                }
              else
                {
                  /* add cancel button */
                  SETUNDEPR (btn, gtk_button_new_from_stock, GTK_STOCK_CANCEL);
                  g_object_set_data (G_OBJECT (btn), "resp", GINT_TO_POINTER (YAD_RESPONSE_CANCEL));
                  g_signal_connect (G_OBJECT (btn), "clicked", G_CALLBACK (btn_cb), NULL);
                  gtk_box_pack_start (GTK_BOX (bbox), btn, FALSE, FALSE, 0);

                  /*add ok button */
                  SETUNDEPR (btn, gtk_button_new_from_stock, GTK_STOCK_OK);
                  g_object_set_data (G_OBJECT (btn), "resp", GINT_TO_POINTER (YAD_RESPONSE_OK));
                  g_signal_connect (G_OBJECT (btn), "clicked", G_CALLBACK (btn_cb), NULL);
                  gtk_box_pack_start (GTK_BOX (bbox), btn, FALSE, FALSE, 0);
                }
            }
        }
      /* add buttons box to main window */
      gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 0);
    }

  /* show widgets */
  gtk_widget_show_all (vbox);
#if GTK_CHECK_VERSION(3,0,0)
  if (options.data.width > 0)
    gtk_widget_set_size_request (vbox, options.data.width, options.data.height);
  else
    {
      gint mw, nw;
      gtk_widget_get_preferred_width (vbox, &mw, &nw);
      gtk_widget_set_size_request (vbox, nw, -1);
    }
#endif

  /* parse geometry or move window, if given. must be after showing widget */
  if (!options.data.maximized && !options.data.fullscreen)
    {
      gint cw, ch;

      if (options.common_data.key != -1 && options.data.width > 0 && options.data.height > 0) {
        gtk_window_resize (GTK_WINDOW (dlg), options.data.width, options.data.height);
      }

      gtk_widget_show_all (dlg);

      parse_geometry ();

      /* get current window size for gtk_window_resize */
      gtk_window_get_size (GTK_WINDOW (dlg), &cw, &ch);
      if (options.data.width == -1)
        options.data.width = cw;
      if (options.data.height == -1)
        options.data.height = ch;

      gtk_window_resize (GTK_WINDOW (dlg), options.data.width, options.data.height);

      gtk_window_set_resizable (GTK_WINDOW (dlg), !options.data.fixed);

      if (options.data.use_posx || options.data.use_posy || options.data.center)
        {
          gint ww, wh, sw, sh;
          gtk_window_get_size (GTK_WINDOW (dlg), &ww, &wh);
#if !GTK_CHECK_VERSION(3,0,0)
          gdk_window_get_geometry (gdk_get_default_root_window (), NULL, NULL, &sw, &sh, NULL);
#else
          gdk_window_get_geometry (gdk_get_default_root_window (), NULL, NULL, &sw, &sh);
#endif
          /* place window to specified coordinates */
          if (options.data.center)
            {
              gtk_window_move (GTK_WINDOW (dlg), (sw - options.data.width) / 2, (sh - options.data.height) / 2);
            }
          else
            {
              if (!options.data.use_posx)
                gtk_window_get_position (GTK_WINDOW (dlg), &options.data.posx, NULL);
              if (!options.data.use_posy)
                gtk_window_get_position (GTK_WINDOW (dlg), NULL, &options.data.posy);
              if (options.data.posx < 0)
                options.data.posx = sw - ww + options.data.posx;
              if (options.data.posy < 0)
                options.data.posy = sh - wh + options.data.posy;
              gtk_window_move (GTK_WINDOW (dlg), options.data.posx, options.data.posy);
            }
        }
    }
  else
    {
      gtk_widget_show (dlg);
      /* set maximized or fixed size after showing widget */
      if (options.data.maximized)
        gtk_window_maximize (GTK_WINDOW (dlg));
      else if (options.data.fullscreen)
        gtk_window_fullscreen (GTK_WINDOW (dlg));
    }

  /* print xid */
  if (is_x11 && options.print_xid)
    {
      FILE *xf;

      if (options.xid_file)
        xf = fopen (options.xid_file, "w");
      else
        xf = stderr;

      if (xf)
        {
          fprintf (xf, "0x%lX\n", GDK_WINDOW_XID (gtk_widget_get_window (dlg)));

          if (options.xid_file)
            fclose (xf);
          else
            fflush (xf);
        }
    }

  return dlg;
}

static void
create_plug (void)
{
  GtkWidget *win, *box;

  tabs = get_tabs (options.plug, FALSE);
  while (!tabs)
    {
      usleep (1000);
      tabs = get_tabs (options.plug, FALSE);
    }

  while (!tabs[0].xid)
    usleep (1000);

  win = gtk_plug_new (0);
  /* set window borders */
  if (options.data.borders == -1)
    options.data.borders = (gint) gtk_container_get_border_width (GTK_CONTAINER (win));
  gtk_container_set_border_width (GTK_CONTAINER (win), (guint) options.data.borders);

  box = create_layout (win);
  if (box)
    gtk_container_add (GTK_CONTAINER (win), box);

  gtk_widget_show_all (win);

  /* add plug data */
  /* notebook/paned will count non-zero xids */
  tabs[options.tabnum].pid = getpid ();
  tabs[options.tabnum].xid = gtk_plug_get_id (GTK_PLUG (win));
  shmdt (tabs);
}

void
yad_print_result (void)
{
  switch (options.mode)
    {
    case YAD_MODE_CALENDAR:
      calendar_print_result ();
      break;
    case YAD_MODE_COLOR:
      color_print_result ();
      break;
    case YAD_MODE_ENTRY:
      entry_print_result ();
      break;
    case YAD_MODE_FILE:
      file_print_result ();
      break;
    case YAD_MODE_FONT:
      font_print_result ();
      break;
    case YAD_MODE_FORM:
      form_print_result ();
      break;
    case YAD_MODE_LIST:
      list_print_result ();
      break;
    case YAD_MODE_NOTEBOOK:
      notebook_print_result ();
      break;
    case YAD_MODE_PANED:
      paned_print_result ();
      break;
    case YAD_MODE_SCALE:
      scale_print_result ();
      break;
    case YAD_MODE_TEXTINFO:
      text_print_result ();
      break;
    default:;
    }
}

gint
main (gint argc, gchar ** argv)
{
  GOptionContext *ctx;
  GError *err = NULL;
  gint w, h;
  gchar *str;

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

#if !GLIB_CHECK_VERSION(2,36,0)
  g_type_init ();
#endif

  gtk_init (&argc, &argv);
  g_set_application_name ("YAD");
  read_settings ();
  yad_options_init ();

  ctx = yad_create_context ();
  /* parse YAD_OPTIONS */
  if (g_getenv ("YAD_OPTIONS"))
    {
      gchar *cmd, **args = NULL;
      gint cnt;

      cmd = g_strdup_printf ("yad %s", g_getenv ("YAD_OPTIONS"));

      if (g_shell_parse_argv (cmd, &cnt, &args, &err))
        {
          g_option_context_parse (ctx, &cnt, &args, &err);
          if (err)
            {
              g_printerr (_("Unable to parse YAD_OPTIONS: %s\n"), err->message);
              g_error_free (err);
              err = NULL;
            }
        }
      else
        {
          g_printerr (_("Unable to parse YAD_OPTIONS: %s\n"), err->message);
          g_error_free (err);
          err = NULL;
        }

      g_free (cmd);
    }
  /* parse command line */
  g_option_context_parse (ctx, &argc, &argv, &err);
  if (err)
    {
      g_printerr (_("Unable to parse command line: %s\n"), err->message);
      return -1;
    }
  yad_set_mode ();

  /* check for current GDK backend */
#ifdef GDK_WINDOWING_X11
#if !GTK_CHECK_VERSION(3,0,0)
  is_x11 = TRUE;
#else
  if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
    is_x11 = TRUE;
#endif
#endif

  /* parse custom gtkrc */
  if (options.gtkrc_file)
    {
#if !GTK_CHECK_VERSION(3,0,0)
      gtk_rc_parse (options.gtkrc_file);
#else
      GtkCssProvider *css = gtk_css_provider_new ();
      gtk_css_provider_load_from_path (css, options.gtkrc_file, NULL);
      gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (css),
                                                 GTK_STYLE_PROVIDER_PRIORITY_USER);
      g_object_unref (css);
#endif
    }

  /* set default icons and icon theme */
  if (options.data.icon_theme)
    gtk_icon_theme_set_custom_theme (settings.icon_theme, options.data.icon_theme);
  gtk_icon_size_lookup (GTK_ICON_SIZE_DIALOG, &w, &h);
  settings.big_fallback_image =
    gtk_icon_theme_load_icon (settings.icon_theme, "yad", MIN (w, h), GTK_ICON_LOOKUP_GENERIC_FALLBACK, NULL);
  gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &w, &h);
  settings.small_fallback_image =
    gtk_icon_theme_load_icon (settings.icon_theme, "yad", MIN (w, h), GTK_ICON_LOOKUP_GENERIC_FALLBACK, NULL);

  /* correct separators */
  str = g_strcompress (options.common_data.separator);
  options.common_data.separator = str;
  str = g_strcompress (options.common_data.item_separator);
  options.common_data.item_separator = str;

  /* loads an extra arguments, if specified */
  if (options.rest_file)
    {
      GIOChannel *ioc;
      gchar *buf;
      guint len, line = 0;

      g_strfreev (options.extra_data);
      options.extra_data = NULL;

      ioc = g_io_channel_new_file (options.rest_file, "r", NULL);
      while (TRUE)
        {
          gint status = g_io_channel_read_line (ioc, &buf, NULL, NULL, NULL);

          if (status != G_IO_STATUS_NORMAL)
            break;

          /* remove \n at the end of string */
          len = strlen (buf);
          if (buf[len - 1] == '\n')
            buf[len - 1] = '\0';

          /* add line to arguments array */
          options.extra_data = g_realloc (options.extra_data, (line + 2) * sizeof (gchar *));
          options.extra_data[line] = g_strcompress (buf);
          options.extra_data[line + 1] = NULL;

          g_free (buf);
          line++;
        }
      g_io_channel_shutdown (ioc, FALSE, NULL);
    }

#ifndef G_OS_WIN32
  /* add YAD_PID variable */
  str = g_strdup_printf ("%d", getpid ());
  g_setenv ("YAD_PID", str, TRUE);
  /* set signal handlers */
  signal (SIGUSR1, sa_usr1);
  signal (SIGUSR2, sa_usr2);
#endif

  if (!is_x11 && options.plug != -1)
    {
      options.plug = -1;
      if (options.debug)
        g_printerr (_("WARNING: --plug mode not supported outside X11\n"));
    }

  /* plug mode */
  if (options.plug != -1)
    {
      create_plug ();
      gtk_main ();
      shmdt (tabs);
      return ret;
    }

  if (!is_x11)
    {
      if (options.mode == YAD_MODE_NOTEBOOK || options.mode == YAD_MODE_PANED
#ifdef HAVE_TRAY
          || options.mode == YAD_MODE_NOTIFICATION
#endif
         )
        {
          g_printerr (_("WARNING: This mode not supported outside X11\n"));
          return 1;
        }
    }

  switch (options.mode)
    {
    case YAD_MODE_ABOUT:
      ret = yad_about ();
      break;

    case YAD_MODE_VERSION:
      g_print ("%s (GTK+ %d.%d.%d)\n", PACKAGE_VERSION, gtk_major_version, gtk_minor_version, gtk_micro_version);
      ret = 0;
      break;

#ifdef HAVE_TRAY
    case YAD_MODE_NOTIFICATION:
      ret = yad_notification_run ();
      break;
#endif

    case YAD_MODE_PRINT:
      ret = yad_print_run ();
      break;

    default:
      dialog = create_dialog ();

      if (is_x11)
        {
          /* add YAD_XID variable */
          str = g_strdup_printf ("0x%X", (guint) GDK_WINDOW_XID (gtk_widget_get_window (dialog)));
          g_setenv ("YAD_XID", str, TRUE);
        }

      /* make some specific init actions */
      if (options.mode == YAD_MODE_NOTEBOOK)
        notebook_swallow_childs ();
      else if (options.mode == YAD_MODE_PANED)
        paned_swallow_childs ();
      else if (options.mode == YAD_MODE_PICTURE)
        {
          if (options.picture_data.size == YAD_PICTURE_FIT)
            picture_fit_to_window ();
        }

      if (text && options.data.selectable_labels)
        gtk_label_select_region (GTK_LABEL (text), 0, 0);

      /* run main loop */
      gtk_main ();

      /* print results */
      if (ret != YAD_RESPONSE_TIMEOUT && ret != YAD_RESPONSE_ESC)
        {
          if (options.data.always_print)
            yad_print_result ();
          else
            {
              /* standard OK button pressed */
              if (ret == YAD_RESPONSE_OK && options.data.buttons == NULL)
                yad_print_result ();
              /* custom even button pressed */
              else if (!(ret & 1))
                yad_print_result ();
            }
        }
#ifndef G_OS_WIN32
      if (options.mode == YAD_MODE_NOTEBOOK)
        notebook_close_childs ();
      else if (options.mode == YAD_MODE_PANED)
        paned_close_childs ();
      /* autokill option for progress dialog */
      if (!options.kill_parent)
        {
          if (options.mode == YAD_MODE_PROGRESS && options.progress_data.autokill && ret != YAD_RESPONSE_OK)
            kill (getppid (), SIGHUP);
        }
#endif
    }

#ifndef G_OS_WIN32
  /* NSIG defined in signal.h */
  if (options.kill_parent > 0 && options.kill_parent < NSIG)
    kill (getppid (), options.kill_parent);
#endif

  return ret;
}
