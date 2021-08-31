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

#include "list-box-helper.h"
#include "cc-ubuntudesktop-panel.h"
#include "cc-ubuntudesktop-resources.h"
#include "shell/cc-application.h"
#include "shell/cc-object-storage.h"

#define DING_SCHEMA "org.gnome.shell.extensions.ding"

struct _CcUbuntuDesktopPanel {
  CcPanel                 parent_instance;

  GtkSwitch              *ding_showhome_switch;
  GtkSwitch              *ding_showtrash_switch;
  GtkSwitch              *ding_shownetwork_switch;
  GtkSwitch              *ding_showvolumes_switch;
  GtkSwitch              *ding_addopposite_switch;
  GtkListBox             *ding_position_listbox;
  GtkListBox             *ding_general_listbox;
  GtkComboBoxText        *ding_icons_size_combo;
  GtkComboBoxText        *ding_icons_new_alignment_combo;

  GSettings              *ding_settings;
};

CC_PANEL_REGISTER (CcUbuntuDesktopPanel, cc_ubuntudesktop_panel);

static void
cc_ubuntudesktop_panel_dispose (GObject *object)
{
  CcUbuntuDesktopPanel *self = CC_UBUNTUDESKTOP_PANEL (object);

  g_clear_object (&self->ding_settings);

  G_OBJECT_CLASS (cc_ubuntudesktop_panel_parent_class)->dispose (object);
}

static void
cc_ubuntudesktop_panel_class_init (CcUbuntuDesktopPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_ubuntudesktop_panel_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/ubuntudesktop/cc-ubuntudesktop-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_showhome_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_showtrash_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_shownetwork_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_showvolumes_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_addopposite_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_position_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_general_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_icons_size_combo);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuDesktopPanel, ding_icons_new_alignment_combo);
}

static void
cc_ubuntudesktop_panel_init (CcUbuntuDesktopPanel *self)
{
  g_autoptr(GSettingsSchema) schema = NULL;

  g_resources_register (cc_ubuntudesktop_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Only load if we have ding installed */
  schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (), DING_SCHEMA, TRUE);
  if (!schema)
    {
      g_warning ("Ding is not installed here. Panel disabled. Please fix your installation.");
      return;
    }
  self->ding_settings = g_settings_new_full (schema, NULL, NULL);
  g_settings_bind (self->ding_settings, "icon-size",
                                self->ding_icons_size_combo, "active-id",
                                G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->ding_settings, "start-corner",
                                self->ding_icons_new_alignment_combo, "active-id",
                                G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->ding_settings, "show-home",
                   self->ding_showhome_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->ding_settings, "show-trash",
                   self->ding_showtrash_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->ding_settings, "show-volumes",
                   self->ding_showvolumes_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->ding_settings, "show-network-volumes",
                   self->ding_shownetwork_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->ding_settings, "add-volumes-opposite",
                   self->ding_addopposite_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);
}

void
cc_ubuntudesktop_panel_static_init_func (void)
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
