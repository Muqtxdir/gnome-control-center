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
#include <glib/gprintf.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include "list-box-helper.h"
#include "cc-ubuntu-panel.h"
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
#define UBUNTU_DOCK_ON_MONITOR_KEY "preferred-monitor"

struct _CcUbuntuPanel {
  CcPanel                 parent_instance;

  GtkSwitch              *dock_autohide_switch;
  GtkSwitch              *dock_extendheight_switch;
  GtkCheckButton         *dock_showmounted_button;
  GtkCheckButton         *dock_showtrash_button;
  GtkCheckButton         *dock_movetop_button;
  GtkListBoxRow          *dock_monitor_row;
  GtkListBox             *dock_general_listbox;
  GtkListBox             *dock_behavior_listbox;
  GtkListBox             *dock_launcher_listbox;
  GtkListBox             *dock_position_listbox;
  GtkComboBox            *dock_placement_combo;
  GtkComboBoxText        *dock_clickaction_combo;
  GtkComboBoxText        *dock_scrollaction_combo;
  GtkListStore           *dock_placement_liststore;
  GtkComboBoxText        *dock_position_combo;
  GtkAdjustment          *icon_size_adjustment;
  GtkScale               *icon_size_scale;

  GSettings              *dock_settings;
  CcDisplayConfigManager *display_config_manager;
  GDBusProxy             *shell_proxy;
};

CC_PANEL_REGISTER (CcUbuntuPanel, cc_ubuntu_panel);

static void monitor_labeler_hide (CcUbuntuPanel *self);
static void update_dock_placement_combo_selection (CcUbuntuPanel *self);

static void
cc_ubuntu_panel_dispose (GObject *object)
{
  CcUbuntuPanel *self = CC_UBUNTU_PANEL (object);

  monitor_labeler_hide (self);

  g_clear_object (&self->dock_settings);
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
  g_autoptr(CcDisplayConfig) current_config = NULL;
  GList *outputs, *l;
  GVariantBuilder builder;
  gint number = 0;
  guint n_monitors = 0;

  if (!self->shell_proxy || !self->display_config_manager)
    return;

  current_config = cc_display_config_manager_get_current (self->display_config_manager);

  if (!current_config)
    return;

  outputs = cc_display_config_get_ui_sorted_monitors (current_config);
  if (!outputs)
    return;

  if (cc_display_config_is_cloning (current_config))
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
                    CcDisplayConfig *config,
                    gint            *n_monitors,
                    gint            *primary_index)
{
  g_autoptr(CcDisplayConfig) current = NULL;
  CcDisplayMonitor *primary_monitor;
  GList *config_monitors = NULL;
  GList *valid_monitors, *l;
  gint n_valid_monitors;

  if (config)
    {
      config_monitors = cc_display_config_get_monitors (config);
    }
  else if (self->display_config_manager)
    {
      current = cc_display_config_manager_get_current (self->display_config_manager);
      config_monitors = cc_display_config_get_monitors (current);
    }

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
cc_monitor_id_to_dock (gint index,
                       gint primary_monitor,
                       gint n_monitors)
{
  if (index < 0)
    return -1;

  /* The The dock uses the Gdk index for monitors, where the primary monitor
   * always has index 0, so let's follow what dash-to-dock does in docking.js
   * (as part of _createDocks), but using inverted math */
  index -= primary_monitor;

  if (index < 0)
    index += n_monitors;

  return index;
}

static void
on_screen_changed (CcUbuntuPanel *self)
{
  g_autoptr(CcDisplayConfig) current = NULL;
  g_autoptr(GList) valid_outputs = NULL;
  GtkTreeIter ubuntu_dock_placement_iter;
  GList *outputs, *l;
  gint n_monitors;
  gint primary_monitor;
  gboolean is_rtl;

  if (self->display_config_manager == NULL)
    return;

  current = cc_display_config_manager_get_current (self->display_config_manager);
  if (current == NULL)
    return;

  gtk_list_store_clear (self->dock_placement_liststore);

  outputs = cc_display_config_get_ui_sorted_monitors (current);
  valid_outputs = get_valid_monitors (self, current, &n_monitors, &primary_monitor);
  is_rtl = gtk_widget_get_state_flags (GTK_WIDGET (self->dock_placement_combo)) & GTK_STATE_FLAG_DIR_RTL;

  for (l = outputs; l != NULL && valid_outputs != NULL; l = l->next)
    {
      g_autofree char *monitor_label = NULL;
      CcDisplayMonitor *output = l->data;
      int monitor_id;

      if (!cc_display_monitor_is_active (output))
        continue;

      const gchar *monitor_name = cc_display_monitor_get_display_name (output);
      if (cc_display_monitor_is_primary (output))
        {
          if (!is_rtl)
            monitor_label = g_strdup_printf ("%s (%s)", monitor_name, _("Primary Display"));
          else
            monitor_label = g_strdup_printf ("(%s) %s", _("Primary Display"), monitor_name);
        }
      else
        {
          monitor_label = g_strdup (monitor_name);
        }

      if (n_monitors > 1)
        {
          int monitor_number = cc_display_monitor_get_ui_number (output);
          g_autofree char *old_label = g_steal_pointer (&monitor_label);

          if (!is_rtl)
            monitor_label = g_strdup_printf ("%d. %s", monitor_number,  old_label);
          else
            monitor_label = g_strdup_printf ("%s .%d", old_label, monitor_number);
        }

      monitor_id = cc_monitor_id_to_dock (g_list_index (valid_outputs, output),
                                          primary_monitor, n_monitors);

      gtk_list_store_append (self->dock_placement_liststore, &ubuntu_dock_placement_iter);
      gtk_list_store_set (self->dock_placement_liststore, &ubuntu_dock_placement_iter,
                          0, monitor_label,
                          1, monitor_id,
                          -1);
    }
    
  gtk_widget_set_visible (GTK_WIDGET (self->dock_monitor_row), valid_outputs != NULL);

  gtk_list_store_prepend (self->dock_placement_liststore, &ubuntu_dock_placement_iter);
  gtk_list_store_set (self->dock_placement_liststore, &ubuntu_dock_placement_iter,
                      0, _("All displays"),
                      1, -1,
                      -1);

  update_dock_placement_combo_selection (self);
  ensure_monitor_labels (self);
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
on_dock_placement_combo_changed (CcUbuntuPanel *self)
{
  gint active;
  gint monitor_id;
  gboolean ubuntu_dock_on_all_monitors;
  gint ubuntu_dock_current_index;

  active = gtk_combo_box_get_active (self->dock_placement_combo);
  if (active < 0)
    return;

  ubuntu_dock_on_all_monitors = g_settings_get_boolean (self->dock_settings, UBUNTU_DOCK_ALL_MONITORS_KEY);
  ubuntu_dock_current_index = g_settings_get_int (self->dock_settings, UBUNTU_DOCK_ON_MONITOR_KEY);
  if (active == 0)
    {
      if (!ubuntu_dock_on_all_monitors)
        {
          g_settings_set_boolean (self->dock_settings, UBUNTU_DOCK_ALL_MONITORS_KEY, TRUE);
          g_settings_apply (self->dock_settings);
        }
    }
  else
    {
      GtkTreeIter placement_iter;
      g_autoptr(GSettings) delayed_settings = g_settings_new (UBUNTU_DOCK_SCHEMA);
      g_settings_delay (delayed_settings);

      if (ubuntu_dock_on_all_monitors)
        g_settings_set_boolean (delayed_settings, UBUNTU_DOCK_ALL_MONITORS_KEY, FALSE);

      gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (self->dock_placement_liststore),
                                     &placement_iter, NULL, active);
      gtk_tree_model_get (GTK_TREE_MODEL (self->dock_placement_liststore),
                          &placement_iter,
                          1, &monitor_id, -1);

      if (ubuntu_dock_current_index != monitor_id)
        g_settings_set_int (delayed_settings, UBUNTU_DOCK_ON_MONITOR_KEY, monitor_id);

      g_settings_apply (delayed_settings);
    }
}

static int
get_dock_monitor (CcUbuntuPanel *self)
{
  g_autoptr(GList) monitors = NULL;
  int index;
  int n_monitors;
  int primary_monitor;

  monitors = get_valid_monitors (self, NULL, &n_monitors, &primary_monitor);
  index = g_settings_get_int (self->dock_settings, UBUNTU_DOCK_ON_MONITOR_KEY);

  if (index < 0 || index >= n_monitors)
    return primary_monitor;

  return index;
}

static void
update_dock_placement_combo_selection (CcUbuntuPanel *self)
{
  int selection = 0;

  if (g_settings_get_boolean (self->dock_settings, UBUNTU_DOCK_ALL_MONITORS_KEY) == FALSE)
    {
      GtkTreeIter placement_iter;
      int dock_monitor;
      int monitor_id;

      dock_monitor = get_dock_monitor (self);
      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->dock_placement_liststore), &placement_iter))
        {
          do
            {
              gtk_tree_model_get (GTK_TREE_MODEL (self->dock_placement_liststore), &placement_iter,
                                  1, &monitor_id, -1);
              if (monitor_id == dock_monitor)
                break;

              selection++;
            } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (self->dock_placement_liststore),
                                               &placement_iter));
        }
    }
  gtk_combo_box_set_active (GTK_COMBO_BOX (self->dock_placement_combo), selection);
}

static void
cc_ubuntu_panel_class_init (CcUbuntuPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_ubuntu_panel_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/ubuntu/cc-ubuntu-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_autohide_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_extendheight_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_showmounted_button);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_showtrash_button);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_movetop_button);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_general_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_behavior_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_launcher_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_position_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_monitor_row);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_placement_combo);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_clickaction_combo);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_scrollaction_combo);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_placement_liststore);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, dock_position_combo);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, icon_size_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuPanel, icon_size_scale);

  gtk_widget_class_bind_template_callback (widget_class, on_dock_placement_combo_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_icon_size_adjustment_value_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_icon_size_format_value);
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

static const char *
get_dock_position_for_direction (CcUbuntuPanel *self,
                                 const char    *position)
{
  if (gtk_widget_get_state_flags (GTK_WIDGET (self)) & GTK_STATE_FLAG_DIR_RTL)
    {
      if (g_str_equal (position, "LEFT"))
        position = "RIGHT";
      else if (g_str_equal (position, "RIGHT"))
        position = "LEFT";
    }

  return position;
}

static gboolean
dock_position_get_mapping (GValue   *value,
                           GVariant *variant,
                           gpointer  user_data)
{
  CcUbuntuPanel *self = user_data;
  const char *position;

  position = g_variant_get_string (variant, NULL);
  g_value_set_string (value, get_dock_position_for_direction (self, position));

  return TRUE;
}

static GVariant *
dock_position_set_mapping (const GValue       *value,
                           const GVariantType *type,
                           gpointer            user_data)
{
  CcUbuntuPanel *self = user_data;
  const char *position;

  position = g_value_get_string (value);

  return g_variant_new_string (get_dock_position_for_direction (self, position));
}

static void
cc_ubuntu_panel_init (CcUbuntuPanel *self)
{
  g_autoptr(GSettingsSchema) schema = NULL;

  g_resources_register (cc_ubuntu_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Only load if we have ubuntu dock or dash to dock installed */
  schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (), UBUNTU_DOCK_SCHEMA, TRUE);
  if (!schema)
    {
      g_warning ("No Ubuntu Dock is installed here. Panel disabled. Please fix your installation.");
      return;
    }
  self->dock_settings = g_settings_new_full (schema, NULL, NULL);
  g_signal_connect_object (self->dock_settings, "changed::" ICONSIZE_KEY,
                           G_CALLBACK (icon_size_widget_refresh), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->dock_settings, "changed::" UBUNTU_DOCK_ALL_MONITORS_KEY,
                           G_CALLBACK (update_dock_placement_combo_selection), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->dock_settings, "changed::" UBUNTU_DOCK_ON_MONITOR_KEY,
                           G_CALLBACK (update_dock_placement_combo_selection), self, G_CONNECT_SWAPPED);
  g_settings_bind_with_mapping (self->dock_settings, "dock-position",
                                self->dock_position_combo, "active-id",
                                G_SETTINGS_BIND_DEFAULT,
                                dock_position_get_mapping,
                                dock_position_set_mapping,
                                self, NULL);
  g_settings_bind (self->dock_settings, "click-action",
                                self->dock_clickaction_combo, "active-id",
                                G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->dock_settings, "scroll-action",
                                self->dock_scrollaction_combo, "active-id",
                                G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->dock_settings, "dock-fixed",
                   self->dock_autohide_switch, "active",
                   G_SETTINGS_BIND_INVERT_BOOLEAN);
  g_settings_bind (self->dock_settings, "extend-height",
                   self->dock_extendheight_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->dock_settings, "show-mounts",
                   self->dock_showmounted_button, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->dock_settings, "show-trash",
                   self->dock_showtrash_button, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->dock_settings, "show-apps-at-top",
                   self->dock_movetop_button, "active",
                   G_SETTINGS_BIND_DEFAULT);

  /* Icon size change - we halve the sizes so we can only get even values */
  gtk_adjustment_set_value (self->icon_size_adjustment, DEFAULT_ICONSIZE / 2);
  gtk_adjustment_set_lower (self->icon_size_adjustment, MIN_ICONSIZE / 2);
  gtk_adjustment_set_upper (self->icon_size_adjustment, MAX_ICONSIZE / 2);
  gtk_scale_add_mark (self->icon_size_scale, DEFAULT_ICONSIZE / 2, GTK_POS_BOTTOM, NULL);

  icon_size_widget_refresh (self);

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
