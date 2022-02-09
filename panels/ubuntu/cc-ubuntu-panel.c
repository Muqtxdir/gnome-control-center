/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2017-2020 Canonical Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <libhandy-1/handy.h>

#include "list-box-helper.h"
#include "cc-ubuntu-panel.h"
#include "cc-ubuntu-dock-dialog.h"
#include "cc-ubuntu-resources.h"
#include "shell/cc-application.h"
#include "shell/cc-object-storage.h"

#include "panels/display/cc-display-config-manager-dbus.h"
#include "panels/display/cc-display-config.h"

#define MIN_ICONSIZE 16.0
#define MAX_ICONSIZE 64.0
#define DEFAULT_ICONSIZE 48.0
#define ICONSIZE_KEY "dash-max-icon-size"

#define UBUNTU_DOCK_SCHEMA "org.gnome.shell.extensions.dash-to-dock"
#define UBUNTU_DOCK_ALL_MONITORS_KEY "multi-monitor"
#define UBUNTU_DOCK_PREFERRED_MONITOR_KEY "preferred-monitor"
#define UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY "preferred-monitor-by-connector"

/*
 * This allows to migrate settings from 'preferred-monitor' to
 * 'preferred-monitor-by-connector', and can be removed after 22.04
 * simplifying all the logic, by relying on connector names only.
 */
#define UBUNTU_DOCK_MONITOR_INDEX_USE_CONNECTOR -2

#define INTERFACE_SCHEMA "org.gnome.desktop.interface"
#define GTK_THEME_KEY "gtk-theme"
#define CURSOR_THEME_KEY "cursor-theme"
#define ICON_THEME_KEY "icon-theme"

#define GEDIT_PREFRENCES_SCHEMA "org.gnome.gedit.preferences.editor"
#define GEDIT_THEME_KEY "scheme"

struct _CcUbuntuPanel {
  CcPanel                 parent_instance;

  HdyPreferencesGroup    *dock_preferences_group;
  GtkSwitch              *dock_autohide_switch;
  HdyComboRow            *dock_monitor_row;
  GListStore             *dock_monitors_liststore;
  HdyComboRow            *dock_position_row;
  GtkAdjustment          *icon_size_adjustment;
  GtkScale               *icon_size_scale;
  GtkFlowBox             *theme_box;
  GtkFlowBoxChild        *theme_dark;
  GtkFlowBoxChild        *theme_light;
  GtkFlowBox             *color_box;
  GtkFlowBoxChild        *aqua;
  GtkFlowBoxChild        *blue;
  GtkFlowBoxChild        *green;
  GtkFlowBoxChild        *orange;
  GtkFlowBoxChild        *pink;
  GtkFlowBoxChild        *purple;
  GtkFlowBoxChild        *red;
  GtkFlowBoxChild        *yellow;
  
  GSettings              *dock_settings;
  GSettings              *interface_settings;
  GSettings              *gedit_settings;
  CcDisplayConfigManager *display_config_manager;
  CcDisplayConfig        *display_current_config;
  GDBusProxy             *shell_proxy;

  gboolean                updating;
};

CC_PANEL_REGISTER (CcUbuntuPanel, cc_ubuntu_panel);

static void monitor_labeler_hide (CcUbuntuPanel *self);
static void update_dock_monitor_combo_row_selection (CcUbuntuPanel *self);

static void
cc_ubuntu_panel_dispose (GObject *object)
{
  CcUbuntuPanel *self = CC_UBUNTU_PANEL (object);

  monitor_labeler_hide (self);

  /* Upstream code is wrong at handling configuration finalization if happens
   * earlier than one of its child nodes, so we need to remove the entries
   * not to make this happen implicitly too late, causing a crash.
   * This can be removed when the follow MR is merged:
   * https://gitlab.gnome.org/GNOME/gnome-control-center/-/merge_requests/1175
   */
  if (self->dock_monitors_liststore)
    g_list_store_remove_all (self->dock_monitors_liststore);

  g_clear_object (&self->dock_settings);
  g_clear_object (&self->dock_monitors_liststore);
  g_clear_object (&self->interface_settings);
  g_clear_object (&self->gedit_settings);
  g_clear_object (&self->display_current_config);
  g_clear_object (&self->display_config_manager);
  g_clear_object (&self->shell_proxy);

  G_OBJECT_CLASS (cc_ubuntu_panel_parent_class)->dispose (object);
}

static void
monitor_labeler_hide (CcUbuntuPanel *self)
{
  if (!self->shell_proxy)
    return;

  g_dbus_proxy_call (self->shell_proxy,
                     "HideMonitorLabels",
                     NULL, G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, NULL, NULL);
}

static void
monitor_labeler_show (CcUbuntuPanel *self)
{
  GList *outputs, *l;
  GVariantBuilder builder;
  gint number = 0;
  guint n_monitors = 0;

  if (!self->shell_proxy || !self->display_config_manager)
    return;

  outputs = cc_display_config_get_ui_sorted_monitors (self->display_current_config);
  if (!outputs)
    return;

  if (cc_display_config_is_cloning (self->display_current_config))
    return monitor_labeler_hide (self);

  g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
  g_variant_builder_open (&builder, G_VARIANT_TYPE_ARRAY);

  for (l = outputs; l != NULL; l = l->next)
    {
      CcDisplayMonitor *output = l->data;

      if (!cc_display_monitor_is_active (output))
        continue;

      number = cc_display_monitor_get_ui_number (output);
      if (number == 0)
        continue;

      g_variant_builder_add (&builder, "{sv}",
                             cc_display_monitor_get_connector_name (output),
                             g_variant_new_int32 (number));
      n_monitors++;
    }

  g_variant_builder_close (&builder);

  if (number < 2 || n_monitors < 2)
    {
      g_variant_builder_clear (&builder);
      return monitor_labeler_hide (self);
    }

  g_dbus_proxy_call (self->shell_proxy,
                     "ShowMonitorLabels",
                     g_variant_builder_end (&builder),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, NULL, NULL);
}

static void
ensure_monitor_labels (CcUbuntuPanel *self)
{
  g_autoptr(GList) windows = NULL;
  GList *w;

  windows = gtk_window_list_toplevels ();

  for (w = windows; w; w = w->next)
    {
      if (gtk_window_has_toplevel_focus (GTK_WINDOW (w->data)))
        {
          monitor_labeler_show (self);
          break;
        }
    }

  if (!w)
    monitor_labeler_hide (self);
}

static void
shell_proxy_ready (GObject        *source,
                   GAsyncResult   *res,
                   CcUbuntuPanel *self)
{
  GDBusProxy *proxy;
  g_autoptr(GError) error = NULL;

  proxy = cc_object_storage_create_dbus_proxy_finish (res, &error);
  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to contact gnome-shell: %s", error->message);
      return;
    }

  self->shell_proxy = proxy;

  ensure_monitor_labels (self);
}

static GList *
get_valid_monitors (CcUbuntuPanel   *self,
                    gint            *n_monitors,
                    gint            *primary_index)
{
  CcDisplayMonitor *primary_monitor;
  GList *config_monitors = NULL;
  GList *valid_monitors, *l;
  gint n_valid_monitors;

  config_monitors = cc_display_config_get_monitors (self->display_current_config);
  primary_monitor = NULL;
  valid_monitors = NULL;
  n_valid_monitors = 0;

  for (l = config_monitors; l != NULL; l = l->next)
    {
      CcDisplayMonitor *monitor = l->data;

      if (!cc_display_monitor_is_active (monitor))
        continue;

      /* The default monitors list uses reversed order, so prepend to
       * set it back to mutter order */
      valid_monitors = g_list_prepend (valid_monitors, monitor);

      if (cc_display_monitor_is_primary (monitor))
        primary_monitor = monitor;

      n_valid_monitors++;
    }

  if (n_monitors)
    *n_monitors = n_valid_monitors;

  if (primary_index)
    *primary_index = g_list_index (valid_monitors, primary_monitor);

  return valid_monitors;
}

static int
ui_sort_monitor (gconstpointer a, gconstpointer b)
{
  CcDisplayMonitor *monitor_a = (CcDisplayMonitor *) a;
  CcDisplayMonitor *monitor_b = (CcDisplayMonitor *) b;

  return cc_display_monitor_get_ui_number (monitor_a) -
         cc_display_monitor_get_ui_number (monitor_b);
}

static GList *
get_valid_monitors_sorted (CcUbuntuPanel   *self,
                           gint            *n_monitors,
                           gint            *primary_index)
{
  GList *monitors = get_valid_monitors (self, n_monitors, primary_index);

  return g_list_sort (monitors, ui_sort_monitor);
}

static int
dock_monitor_to_id (gint index,
                    gint primary_monitor,
                    gint n_monitors)
{
  if (index < 0)
    return -1;

  /* The The dock uses the Gdk index for monitors, where the primary monitor
   * always has index 0, so let's follow what dash-to-dock does in docking.js
   * (as part of _createDocks) */
  return (primary_monitor + index) % n_monitors;
}

typedef enum
{
  GSD_UBUNTU_DOCK_MONITOR_ALL,
  GSD_UBUNTU_DOCK_MONITOR_PRIMARY,
} GsdUbuntuDockMonitor;

static char *
get_dock_monitor_value_object_name (HdyValueObject *object,
                                    CcUbuntuPanel  *self)
{
  const GValue *value = hdy_value_object_get_value (object);

  if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    return g_value_dup_string (value);

  if (G_VALUE_TYPE (value) == CC_TYPE_DISPLAY_MONITOR)
    {
      CcDisplayMonitor *monitor = g_value_get_object (value);
      int monitor_number = cc_display_monitor_get_ui_number (monitor);
      const char *monitor_name = cc_display_monitor_get_display_name (monitor);

      if (gtk_widget_get_state_flags (GTK_WIDGET (self)) & GTK_STATE_FLAG_DIR_LTR)
        return g_strdup_printf ("%d. %s", monitor_number, monitor_name);
      else
        return g_strdup_printf ("%s .%d", monitor_name, monitor_number);
    }

  g_return_val_if_reached (NULL);
}

static void
populate_dock_monitor_combo_row (CcUbuntuPanel *self)
{
  g_autoptr(CcDisplayMonitor) primary_monitor = NULL;
  g_autoptr(GList) valid_monitors = NULL;
  g_autoptr(HdyValueObject) primary_value_object = NULL;
  g_autoptr(HdyValueObject) all_displays_value_object = NULL;
  GList *l;
  gint index;

  if (self->display_config_manager == NULL)
    return;

  g_list_store_remove_all (self->dock_monitors_liststore);

  valid_monitors = get_valid_monitors_sorted (self, NULL, NULL);
  gtk_widget_set_visible (GTK_WIDGET (self->dock_monitor_row), valid_monitors != NULL);

  if (!valid_monitors)
    return;

  all_displays_value_object = hdy_value_object_new_string (_("All displays"));
  g_list_store_insert (self->dock_monitors_liststore,
                       GSD_UBUNTU_DOCK_MONITOR_ALL,
                       all_displays_value_object);

  for (l = valid_monitors, index = 0; l != NULL; l = l->next, index++)
    {
      g_auto(GValue) value = G_VALUE_INIT;
      g_autoptr(HdyValueObject) monitor_value_object = NULL;
      CcDisplayMonitor *monitor = l->data;

      if (cc_display_monitor_is_primary (monitor))
        g_set_object (&primary_monitor, monitor);

      g_value_init (&value, CC_TYPE_DISPLAY_MONITOR);
      g_value_set_object (&value, monitor);
      monitor_value_object = hdy_value_object_new (&value);

      g_list_store_append (self->dock_monitors_liststore, monitor_value_object);
    }

  if (primary_monitor)
    {
      int ui_number = cc_display_monitor_get_ui_number (primary_monitor);

      if (gtk_widget_get_state_flags (GTK_WIDGET (self)) & GTK_STATE_FLAG_DIR_LTR)
        {
          primary_value_object = hdy_value_object_new_take_string(
            g_strdup_printf ("%s (%d)", _("Primary Display"), ui_number));
        }
      else
        {
          primary_value_object = hdy_value_object_new_take_string(
            g_strdup_printf ("(%d) %s", ui_number, _("Primary Display")));
        }
    }
  else
    {
      primary_value_object = hdy_value_object_new_string (_("Primary Display"));
    }

  g_list_store_insert (self->dock_monitors_liststore,
                       GSD_UBUNTU_DOCK_MONITOR_PRIMARY,
                       primary_value_object);
}

static void
on_screen_changed (CcUbuntuPanel *self)
{
  g_autoptr(CcDisplayConfig) current = NULL;

  if (self->display_config_manager == NULL)
    return;

  current = cc_display_config_manager_get_current (self->display_config_manager);
  if (current == NULL)
    return;

  self->updating = TRUE;

  g_set_object (&self->display_current_config, current);

  populate_dock_monitor_combo_row (self);
  ensure_monitor_labels (self);

  self->updating = FALSE;

  update_dock_monitor_combo_row_selection (self);
}

static void
session_bus_ready (GObject        *source,
                   GAsyncResult   *res,
                   gpointer        user_data)
{
  CcUbuntuPanel *self = user_data;
  GDBusConnection *bus;
  g_autoptr(GError) error = NULL;

  bus = g_bus_get_finish (res, &error);
  if (!bus)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to get session bus: %s", error->message);
      return;
    }

  self->display_config_manager = cc_display_config_manager_dbus_new ();
  g_signal_connect_object (self->display_config_manager, "changed",
                           G_CALLBACK (on_screen_changed),
                           self,
                           G_CONNECT_SWAPPED);
}

static void
on_theme_box_selected_children_changed (CcUbuntuPanel *self)
{
  const gchar *gtk_theme = NULL;
  const gchar *icon_theme = NULL;
  const gchar *gedit_theme = NULL;
  g_autoptr(GList) selected = NULL;
  g_autoptr(GList) selected_color = NULL;

  selected = gtk_flow_box_get_selected_children (self->theme_box);
  selected_color = gtk_flow_box_get_selected_children (self->color_box);
  if (selected && selected_color != NULL)
    {
      GtkFlowBoxChild *selected_item = GTK_FLOW_BOX_CHILD (g_list_nth_data (selected, 0));
      GtkFlowBoxChild *selected_color_item = GTK_FLOW_BOX_CHILD (g_list_nth_data (selected_color, 0));
      if (selected_item == self->theme_light)
      {
        gedit_theme = "Yaru";
        if (selected_color_item == self->orange)
        {
          gtk_theme = "Yaru";
          icon_theme = "Yaru";
        }
        else if (selected_color_item == self->aqua)
        {
          gtk_theme = "Yaru-aqua";
          icon_theme = "Yaru-aqua";
        }
        else if (selected_color_item == self->blue)
        {
          gtk_theme = "Yaru-blue";
          icon_theme = "Yaru-blue";
        }
        else if (selected_color_item == self->green)
        {
          gtk_theme = "Yaru-green";
          icon_theme = "Yaru-green";
        }
        else if (selected_color_item == self->pink)
        {
          gtk_theme = "Yaru-pink";
          icon_theme = "Yaru-pink";
        }
        else if (selected_color_item == self->purple)
        {
          gtk_theme = "Yaru-purple";
          icon_theme = "Yaru-purple";
        }
        else if (selected_color_item == self->red)
        {
          gtk_theme = "Yaru-red";
          icon_theme = "Yaru-red";
        }
        else if (selected_color_item == self->yellow)
        {
          gtk_theme = "Yaru-yellow";
          icon_theme = "Yaru-yellow";
        }
      }
      else if (selected_item == self->theme_dark)
      {
        gedit_theme = "Yaru-dark";
        if (selected_color_item == self->orange)
        {
          gtk_theme = "Yaru-dark";
          icon_theme = "Yaru";
        }
        else if (selected_color_item == self->aqua)
        {
          gtk_theme = "Yaru-aqua-dark";
          icon_theme = "Yaru-aqua";
        }
        else if (selected_color_item == self->blue)
        {
          gtk_theme = "Yaru-blue-dark";
          icon_theme = "Yaru-blue";
        }
        else if (selected_color_item == self->green)
        {
          gtk_theme = "Yaru-green-dark";
          icon_theme = "Yaru-green";
        }
        else if (selected_color_item == self->pink)
        {
          gtk_theme = "Yaru-pink-dark";
          icon_theme = "Yaru-pink";
        }
        else if (selected_color_item == self->purple)
        {
          gtk_theme = "Yaru-purple-dark";
          icon_theme = "Yaru-purple";
        }
        else if (selected_color_item == self->red)
        {
          gtk_theme = "Yaru-red-dark";
          icon_theme = "Yaru-red";
        }
        else if (selected_color_item == self->yellow)
        {
          gtk_theme = "Yaru-yellow-dark";
          icon_theme = "Yaru-yellow";
        }
      }
    }

  if (gtk_theme != NULL)
    g_settings_set_string (self->interface_settings, GTK_THEME_KEY, gtk_theme);
  if (self->gedit_settings && gedit_theme != NULL)
    g_settings_set_string (self->gedit_settings, GEDIT_THEME_KEY, gedit_theme);
  if (icon_theme != NULL)
    g_settings_set_string (self->interface_settings, ICON_THEME_KEY, icon_theme);
}

static void
on_interface_settings_changed (CcUbuntuPanel *self)
{
  g_autofree gchar *gtk_theme = NULL;
  g_autofree gchar *cursor_theme = NULL;
  g_autofree gchar *icon_theme = NULL;
  GtkFlowBoxChild *theme_item = NULL;
  GtkFlowBoxChild *color_item = NULL;

  gtk_theme = g_settings_get_string (self->interface_settings, GTK_THEME_KEY);
  cursor_theme = g_settings_get_string (self->interface_settings, CURSOR_THEME_KEY);
  icon_theme = g_settings_get_string (self->interface_settings, ICON_THEME_KEY);
  
  if (g_str_equal (cursor_theme, "Yaru"))
  {
    if (g_strcmp0 (gtk_theme, "Yaru") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->orange;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-aqua") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->aqua;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-blue") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->blue;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-green") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->green;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-pink") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->pink;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-purple") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->purple;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-red") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->red;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-yellow") == 0)
      {
        theme_item = self->theme_light;
        color_item = self->yellow;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->orange;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-aqua-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->aqua;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-blue-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->blue;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-green-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->green;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-pink-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->pink;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-purple-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->purple;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-red-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->red;
      }
    else if (g_strcmp0 (gtk_theme, "Yaru-yellow-dark") == 0)
      {
        theme_item = self->theme_dark;
        color_item = self->yellow;
      }
  }

  if (theme_item && color_item != NULL)
  {
    gtk_flow_box_select_child (self->theme_box, theme_item);
    gtk_flow_box_select_child (self->color_box, color_item);
  }
  else
  {
    gtk_flow_box_unselect_all (self->theme_box);
    gtk_flow_box_unselect_all (self->color_box);
  }
}

static void
load_custom_css (CcUbuntuPanel *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  /* use custom CSS */
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/control-center/ubuntu/appearance.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
icon_size_widget_refresh (CcUbuntuPanel *self)
{
  gint value = g_settings_get_int (self->dock_settings, ICONSIZE_KEY);
  gtk_adjustment_set_value (self->icon_size_adjustment, (gdouble) value / 2);
}

static gchar *
on_icon_size_format_value (CcUbuntuPanel *self, gdouble value)
{
  return g_strdup_printf ("%d", (int)value * 2);
}

static void
on_icon_size_adjustment_value_changed (CcUbuntuPanel *self)
{
  gint value = (gint)gtk_adjustment_get_value (self->icon_size_adjustment) * 2;
  if (g_settings_get_int (self->dock_settings, ICONSIZE_KEY) != value)
    g_settings_set_int (self->dock_settings, ICONSIZE_KEY, value);
}

static void
on_dock_monitor_row_changed (CcUbuntuPanel *self)
{
  gboolean ubuntu_dock_on_all_monitors;
  g_autofree char *ubuntu_dock_current_connector = NULL;
  int selected;

  if (self->updating)
    return;

  selected = hdy_combo_row_get_selected_index (self->dock_monitor_row);
  if (selected < 0)
    return;

  ubuntu_dock_on_all_monitors =
    g_settings_get_boolean (self->dock_settings, UBUNTU_DOCK_ALL_MONITORS_KEY);
  ubuntu_dock_current_connector =
    g_settings_get_string (self->dock_settings, UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY);
  if (selected == GSD_UBUNTU_DOCK_MONITOR_ALL)
    {
      if (!ubuntu_dock_on_all_monitors)
        {
          g_settings_set_boolean (self->dock_settings,
                                  UBUNTU_DOCK_ALL_MONITORS_KEY,
                                  TRUE);
          g_settings_apply (self->dock_settings);
        }
    }
  else
    {
      g_autoptr(GSettings) delayed_settings = g_settings_new (UBUNTU_DOCK_SCHEMA);
      g_settings_delay (delayed_settings);
      g_autofree char *connector_name = NULL;

      if (ubuntu_dock_on_all_monitors)
        g_settings_set_boolean (delayed_settings, UBUNTU_DOCK_ALL_MONITORS_KEY, FALSE);

      if (selected == GSD_UBUNTU_DOCK_MONITOR_PRIMARY)
        {
          connector_name = g_strdup ("primary");
        }
      else
        {
          g_autoptr(HdyValueObject) value_object = NULL;
          CcDisplayMonitor *monitor;

          value_object = g_list_model_get_item (G_LIST_MODEL (self->dock_monitors_liststore),
                                                selected);

          monitor = g_value_get_object (hdy_value_object_get_value (value_object));
          connector_name = g_strdup (cc_display_monitor_get_connector_name (monitor));
        }

      if (g_strcmp0 (ubuntu_dock_current_connector, connector_name) != 0)
        {
          g_settings_set_int (delayed_settings, UBUNTU_DOCK_PREFERRED_MONITOR_KEY,
                                                UBUNTU_DOCK_MONITOR_INDEX_USE_CONNECTOR);
          g_settings_set_string (delayed_settings, UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY,
                                 connector_name);
        }

      g_settings_apply (delayed_settings);
    }
}

static void
on_dock_behavior_row_activated (CcUbuntuPanel *self)
{
  CcUbuntuDockDialog *dialog;

  dialog = cc_ubuntu_dock_dialog_new (self->dock_settings);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (self))));
  gtk_widget_show (GTK_WIDGET (dialog));
}

static CcDisplayMonitor *
get_dock_monitor (CcUbuntuPanel *self)
{
  g_autoptr(GList) monitors = NULL;
  int index;
  int n_monitors;
  int primary_monitor;

  monitors = get_valid_monitors_sorted (self, &n_monitors, &primary_monitor);
  index = g_settings_get_int (self->dock_settings, UBUNTU_DOCK_PREFERRED_MONITOR_KEY);

  if (index == UBUNTU_DOCK_MONITOR_INDEX_USE_CONNECTOR)
    {
      g_autofree char *connector = NULL;
      GList *l;
      int i;

      connector = g_settings_get_string (self->dock_settings,
                                         UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY);

      for (l = monitors, i = 0; l; l = l->next, i++)
        {
          CcDisplayMonitor *monitor = l->data;
          const char *monitor_connector = cc_display_monitor_get_connector_name (monitor);
          if (g_strcmp0 (monitor_connector, connector) == 0)
            return g_object_ref (monitor);
        }
    }

  if (index < 0 || index >= n_monitors)
    return NULL;

  index = dock_monitor_to_id (index, primary_monitor, n_monitors);

  return g_object_ref (g_list_nth_data (monitors, index));
}

static gboolean
dock_placement_row_compare (gconstpointer a, gconstpointer b)
{
  const GValue *row_value_a;
  const GValue *row_value_b;

  row_value_a = hdy_value_object_get_value (HDY_VALUE_OBJECT ((gpointer) a));
  row_value_b = hdy_value_object_get_value (HDY_VALUE_OBJECT ((gpointer) b));

  if (row_value_a == NULL || row_value_b == NULL)
    return row_value_a == row_value_b;

  if (G_VALUE_TYPE (row_value_a) != G_VALUE_TYPE (row_value_b))
    return FALSE;

  if (G_VALUE_TYPE (row_value_a) == CC_TYPE_DISPLAY_MONITOR)
    {
      return cc_display_monitor_get_ui_number (g_value_get_object (row_value_a)) ==
             cc_display_monitor_get_ui_number (g_value_get_object (row_value_b));
    }

  if (G_VALUE_TYPE (row_value_a) == G_TYPE_STRING)
    {
      return g_strcmp0 (g_value_get_string (row_value_a),
                        g_value_get_string (row_value_b)) == 0;
    }

  g_return_val_if_reached (FALSE);
}

static void
update_dock_monitor_combo_row_selection (CcUbuntuPanel *self)
{
  guint selection = GSD_UBUNTU_DOCK_MONITOR_PRIMARY;

  if (g_settings_get_boolean (self->dock_settings, UBUNTU_DOCK_ALL_MONITORS_KEY))
    {
      selection = GSD_UBUNTU_DOCK_MONITOR_ALL;
    }
  else
    {
      g_autoptr (CcDisplayMonitor) monitor = get_dock_monitor (self);

      if (monitor)
        {
          g_autoptr(HdyValueObject) monitor_value_object = NULL;
          g_auto(GValue) value = G_VALUE_INIT;

          g_value_init (&value, CC_TYPE_DISPLAY_MONITOR);
          g_value_set_object (&value, monitor);
          monitor_value_object = hdy_value_object_new (&value);

          if (!g_list_store_find_with_equal_func (self->dock_monitors_liststore,
                                                  monitor_value_object,
                                                  dock_placement_row_compare,
                                                  &selection))
            selection = GSD_UBUNTU_DOCK_MONITOR_PRIMARY;
        }
    }

  hdy_combo_row_set_selected_index (self->dock_monitor_row, selection);
}

static void
cc_ubuntu_panel_class_init (CcUbuntuPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_ubuntu_panel_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/ubuntu/cc-ubuntu-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_preferences_group);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_autohide_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_monitor_row);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_position_row);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, icon_size_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, icon_size_scale);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, theme_box);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, theme_dark);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, theme_light);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, color_box);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, aqua);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, blue);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, green);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, orange);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, pink);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, purple);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, red);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, yellow);

  gtk_widget_class_bind_template_callback (widget_class, on_dock_monitor_row_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_dock_behavior_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_icon_size_adjustment_value_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_icon_size_format_value);
  gtk_widget_class_bind_template_callback (widget_class, on_theme_box_selected_children_changed);
}

static void
mapped_cb (CcUbuntuPanel *self)
{
  CcShell *shell;
  GtkWidget *toplevel;

  shell = cc_panel_get_shell (CC_PANEL (self));
  toplevel = cc_shell_get_toplevel (shell);

  g_signal_handlers_disconnect_by_func (toplevel, mapped_cb, self);
  g_signal_connect_object (toplevel, "notify::has-toplevel-focus",
                           G_CALLBACK (ensure_monitor_labels), self,
                           G_CONNECT_SWAPPED);
}

typedef enum
{
  GSD_UBUNTU_DOCK_POSITION_TOP,
  GSD_UBUNTU_DOCK_POSITION_RIGHT,
  GSD_UBUNTU_DOCK_POSITION_BOTTOM,
  GSD_UBUNTU_DOCK_POSITION_LEFT,

  GSD_UBUNTU_DOCK_POSITION_FIRST = GSD_UBUNTU_DOCK_POSITION_RIGHT,
} GsdUbuntuDockPosition;

static GsdUbuntuDockPosition
get_dock_position_for_direction (CcUbuntuPanel         *self,
                                 GsdUbuntuDockPosition  position)
{
  if (gtk_widget_get_state_flags (GTK_WIDGET (self)) & GTK_STATE_FLAG_DIR_RTL)
    {
      switch (position)
        {
          case GSD_UBUNTU_DOCK_POSITION_RIGHT:
            position = GSD_UBUNTU_DOCK_POSITION_LEFT;
            break;
          case GSD_UBUNTU_DOCK_POSITION_LEFT:
            position = GSD_UBUNTU_DOCK_POSITION_LEFT;
            break;
          default:
            break;
        }
    }

  return position;
}

static const char *
get_dock_position_string (GsdUbuntuDockPosition  position)
{
  switch (position)
    {
      case GSD_UBUNTU_DOCK_POSITION_TOP:
        return "TOP";
      case GSD_UBUNTU_DOCK_POSITION_RIGHT:
        return "RIGHT";
      case GSD_UBUNTU_DOCK_POSITION_BOTTOM:
        return "BOTTOM";
      case GSD_UBUNTU_DOCK_POSITION_LEFT:
        return "LEFT";
      default:
        g_return_val_if_reached ("LEFT");
    }
}

static GsdUbuntuDockPosition
get_dock_position_from_string (const char *position)
{
  if (g_str_equal (position, "TOP"))
    return GSD_UBUNTU_DOCK_POSITION_TOP;

  if (g_str_equal (position, "RIGHT"))
    return GSD_UBUNTU_DOCK_POSITION_RIGHT;

  if (g_str_equal (position, "BOTTOM"))
    return GSD_UBUNTU_DOCK_POSITION_BOTTOM;

  if (g_str_equal (position, "LEFT"))
    return GSD_UBUNTU_DOCK_POSITION_LEFT;

  g_return_val_if_reached (GSD_UBUNTU_DOCK_POSITION_LEFT);
}

static GsdUbuntuDockPosition
get_dock_position_row_position (CcUbuntuPanel *self,
                                int            index)
{
  GListModel *model = hdy_combo_row_get_model (self->dock_position_row);
  HdyValueObject *value_object = g_list_model_get_item (model, index);

  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (value_object), "position"));
}

static int
get_dock_position_row_index (CcUbuntuPanel         *self,
                             GsdUbuntuDockPosition  position)
{
  GListModel *model = hdy_combo_row_get_model (self->dock_position_row);
  guint n_items;
  guint i;

  n_items = g_list_model_get_n_items (model);

  for (i = 0; i < n_items; i++)
    {
      HdyValueObject *value_object = g_list_model_get_item (model, i);
      GsdUbuntuDockPosition item_position;

      item_position = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (value_object), "position"));

      if (item_position == position)
        return i;
    }

  g_return_val_if_reached (0);
}

static gboolean
dock_position_get_mapping (GValue   *value,
                           GVariant *variant,
                           gpointer  user_data)
{
  CcUbuntuPanel *self = user_data;
  GsdUbuntuDockPosition position;

  position = get_dock_position_from_string (g_variant_get_string (variant, NULL));
  position = get_dock_position_for_direction (self, position);

  if (G_VALUE_TYPE (value) == G_TYPE_INT)
    {
      g_value_set_int (value, get_dock_position_row_index (self, position));
      return TRUE;
    }
  else if (G_VALUE_TYPE (value) == G_TYPE_STRING)
    {
      g_value_set_string (value, get_dock_position_string (position));
      return TRUE;
    }

  return FALSE;
}

static GVariant *
dock_position_set_mapping (const GValue       *value,
                           const GVariantType *type,
                           gpointer            user_data)
{
  CcUbuntuPanel *self = user_data;
  GsdUbuntuDockPosition position;

  position = get_dock_position_row_position (self, g_value_get_int (value));
  position = get_dock_position_for_direction (self, position);

  return g_variant_new_string (get_dock_position_string (position));
}

static void
populate_dock_position_row (HdyComboRow *combo_row)
{
  g_autoptr (GListStore) list_store = NULL;
  struct {
    char *name;
    GsdUbuntuDockPosition position;
  } positions[] = {
    {
      NC_("Position on screen for the Ubuntu dock", "Left"),
          GSD_UBUNTU_DOCK_POSITION_LEFT,
    },
    {
      NC_("Position on screen for the Ubuntu dock", "Bottom"),
          GSD_UBUNTU_DOCK_POSITION_BOTTOM,
    },
    {
      NC_("Position on screen for the Ubuntu dock", "Right"),
          GSD_UBUNTU_DOCK_POSITION_RIGHT,
    },
  };
  guint i;

  list_store = g_list_store_new (HDY_TYPE_VALUE_OBJECT);
  for (i = 0; i < G_N_ELEMENTS (positions); i++)
    {
      g_autoptr (HdyValueObject) value_object = NULL;

      value_object = hdy_value_object_new_string (_(positions[i].name));
      g_object_set_data (G_OBJECT (value_object),
                         "position",
                         GUINT_TO_POINTER (positions[i].position));
      g_list_store_append (list_store, value_object);
    }

  hdy_combo_row_bind_name_model (combo_row,
                                 G_LIST_MODEL (list_store),
                                 (HdyComboRowGetNameFunc) hdy_value_object_dup_string,
                                 NULL, NULL);
}

static void
cc_ubuntu_panel_init (CcUbuntuPanel *self)
{
  GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default ();
  g_autoptr(GSettingsSchema) schema = NULL;

  g_resources_register (cc_ubuntu_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  self->interface_settings = g_settings_new (INTERFACE_SCHEMA);
  g_signal_connect_object (self->interface_settings, "changed::" GTK_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->interface_settings, "changed::" CURSOR_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->interface_settings, "changed::" ICON_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);

  schema = g_settings_schema_source_lookup (schema_source, GEDIT_PREFRENCES_SCHEMA, TRUE);
  if (schema)
    {
      self->gedit_settings = g_settings_new (GEDIT_PREFRENCES_SCHEMA);
      g_signal_connect_object (self->gedit_settings, "changed::" GEDIT_THEME_KEY,
                               G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
    }
  else
    {
      g_warning ("No gedit is installed here. Colors won't be updated. Please fix your installation.");
    }

  /* Only load if we have ubuntu dock or dash to dock installed */
  schema = g_settings_schema_source_lookup (schema_source, UBUNTU_DOCK_SCHEMA, TRUE);
  if (!schema)
    {
      g_warning ("No Ubuntu Dock is installed here. Panel disabled. Please fix your installation.");
      gtk_widget_hide (GTK_WIDGET (self->dock_preferences_group));
      return;
    }

  self->dock_settings = g_settings_new_full (schema, NULL, NULL);
  self->dock_monitors_liststore = g_list_store_new (HDY_TYPE_VALUE_OBJECT);

  self->updating = TRUE;
  hdy_combo_row_bind_name_model (self->dock_monitor_row,
                                 G_LIST_MODEL (self->dock_monitors_liststore),
                                 (HdyComboRowGetNameFunc) get_dock_monitor_value_object_name,
                                 self, NULL);
  self->updating = FALSE;

  populate_dock_position_row (self->dock_position_row);

  g_signal_connect_object (self->dock_settings, "changed::" ICONSIZE_KEY,
                           G_CALLBACK (icon_size_widget_refresh), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->dock_settings, "changed::" UBUNTU_DOCK_ALL_MONITORS_KEY,
                           G_CALLBACK (update_dock_monitor_combo_row_selection), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->dock_settings, "changed::" UBUNTU_DOCK_PREFERRED_MONITOR_KEY,
                           G_CALLBACK (update_dock_monitor_combo_row_selection), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->dock_settings, "changed::" UBUNTU_DOCK_PREFERRED_CONNECTOR_KEY,
                           G_CALLBACK (update_dock_monitor_combo_row_selection), self, G_CONNECT_SWAPPED);
  g_settings_bind_with_mapping (self->dock_settings, "dock-position",
                                self->dock_position_row, "selected-index",
                                G_SETTINGS_BIND_DEFAULT,
                                dock_position_get_mapping,
                                dock_position_set_mapping,
                                self, NULL);
  g_settings_bind (self->dock_settings, "dock-fixed",
                   self->dock_autohide_switch, "active",
                   G_SETTINGS_BIND_INVERT_BOOLEAN);

  /* Icon size change - we halve the sizes so we can only get even values */
  gtk_adjustment_set_value (self->icon_size_adjustment, DEFAULT_ICONSIZE / 2);
  gtk_adjustment_set_lower (self->icon_size_adjustment, MIN_ICONSIZE / 2);
  gtk_adjustment_set_upper (self->icon_size_adjustment, MAX_ICONSIZE / 2);
  gtk_scale_add_mark (self->icon_size_scale, DEFAULT_ICONSIZE / 2, GTK_POS_BOTTOM, NULL);

  icon_size_widget_refresh (self);
  on_interface_settings_changed (self);
  load_custom_css (self);

  cc_object_storage_create_dbus_proxy (G_BUS_TYPE_SESSION,
                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                                       G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                       "org.gnome.Shell",
                                       "/org/gnome/Shell",
                                       "org.gnome.Shell",
                                       cc_panel_get_cancellable (CC_PANEL (self)),
                                       (GAsyncReadyCallback) shell_proxy_ready,
                                       self);

  g_signal_connect (self, "map", G_CALLBACK (mapped_cb), NULL);

  g_bus_get (G_BUS_TYPE_SESSION, NULL, session_bus_ready, self);
}

void
cc_ubuntu_panel_static_init_func (void)
{
  CcApplication *application;
  const gchar *desktop_list;
  g_auto(GStrv) desktops = NULL;

  desktop_list = g_getenv ("XDG_CURRENT_DESKTOP");
  if (desktop_list != NULL)
    desktops = g_strsplit (desktop_list, ":", -1);

  if (desktops == NULL || !g_strv_contains ((const gchar * const *) desktops, "ubuntu")) {
    application = CC_APPLICATION (g_application_get_default ());
    cc_shell_model_set_panel_visibility (cc_application_get_model (application),
                                         "ubuntu",
                                         CC_PANEL_HIDDEN);
  }
}
