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
#include "cc-ubuntuappearance-panel.h"
#include "cc-ubuntuappearance-resources.h"
#include "shell/cc-application.h"
#include "shell/cc-object-storage.h"

#define INTERFACE_SCHEMA "org.gnome.desktop.interface"
#define GTK_THEME_KEY "gtk-theme"
#define CURSOR_THEME_KEY "cursor-theme"
#define ICON_THEME_KEY "icon-theme"
#define FONT_KEY "font-name"
#define UBUNTU_THEME "Yaru"
#define UBUNTU_DARK_THEME "Yaru-dark"
#define DOCUMENT_FONT_KEY "document-font-name"
#define MONOSPACE_FONT_KEY "monospace-font-name"
#define OD_REG_FONT "OpenDyslexic Regular 10"
#define OD_DOC_FONT "OpenDyslexic Regular 11"
#define OD_MON_FONT "OpenDyslexicMono Regular 11"
#define KEY_TEXT_SCALING_FACTOR "text-scaling-factor"

#define GEDIT_PREFRENCES_SCHEMA "org.gnome.gedit.preferences.editor"
#define GEDIT_THEME_KEY "scheme"

struct _CcUbuntuAppearancePanel {
  CcPanel                 parent_instance;

  GtkSwitch         *dyslexia_friendly_switch;
  GtkFlowBox        *theme_box;
  GtkFlowBoxChild   *theme_dark;
  GtkFlowBoxChild   *theme_default;
  GtkScale          *text_size_scale;
  GtkLabel          *text_size_label;

  GSettings         *interface_settings;
  GSettings         *gedit_settings;
  
};

CC_PANEL_REGISTER (CcUbuntuAppearancePanel, cc_ubuntuappearance_panel);

static void
cc_ubuntuappearance_panel_dispose (GObject *object)
{
  CcUbuntuAppearancePanel *self = CC_UBUNTUAPPEARANCE_PANEL (object);

  g_clear_object (&self->interface_settings);
  g_clear_object (&self->gedit_settings);

  G_OBJECT_CLASS (cc_ubuntuappearance_panel_parent_class)->dispose (object);
}

static gboolean
get_dyslexia_friendly_mapping (GValue   *value,
                               GVariant *variant,
                               gpointer  user_data)
{
  const char *font;
  gboolean dfs;

  font = g_variant_get_string (variant, NULL);
  dfs = (g_strcmp0 (font, OD_REG_FONT) == 0);
  g_value_set_boolean (value, dfs);

  return TRUE;
}

static GVariant *
set_dyslexia_friendly_mapping (const GValue       *value,
                               const GVariantType *expected_type,
                               gpointer            user_data)
{
  gboolean dfs;
  CcUbuntuAppearancePanel *self = user_data;
  GVariant *ret = NULL;

  dfs = g_value_get_boolean (value);
  if (dfs)
    {
      ret = g_variant_new_string (OD_REG_FONT);
      g_settings_set_string (self->interface_settings, FONT_KEY, OD_REG_FONT);
      g_settings_set_string (self->interface_settings, DOCUMENT_FONT_KEY, OD_DOC_FONT);
      g_settings_set_string (self->interface_settings, MONOSPACE_FONT_KEY, OD_MON_FONT);
    }
  else
    {
      g_settings_reset (self->interface_settings, FONT_KEY);
      g_settings_reset (self->interface_settings, DOCUMENT_FONT_KEY);
      g_settings_reset (self->interface_settings, MONOSPACE_FONT_KEY);
    }

  return ret;
}

static gboolean
text_size_label_mapping_get (GValue   *value,
                             GVariant *variant,
                             gpointer  user_data)
{
  char *label;
  double text_size;

  text_size = g_variant_get_double (variant);
  
  if (text_size<1.0 && text_size>0.75)
      label = g_strdup (("text size", "Small"));
  else if (text_size == 0.75)
      label = g_strdup (("text size", "Small"));
  else if (text_size == 1.0)
      label = g_strdup (("text size", "Default"));
  else if (text_size<1.25 && text_size>1.0)
      label = g_strdup (("text size", "Large"));
  else if (text_size == 1.25)
      label = g_strdup (("text size", "Large"));
  else if (text_size<1.5 && text_size>1.25)
      label = g_strdup (("text size", "Larger"));
  else if (text_size == 1.5)
      label = g_strdup (("text size", "Larger"));
  else
      label = g_strdup (("text size", "Unkown value"));

  g_value_take_string (value, label);

  return TRUE;
}

static void
load_custom_css (CcUbuntuAppearancePanel *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  /* use custom CSS */
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/control-center/ubuntuappearance/appearance.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

}

static void
on_theme_box_selected_children_changed (CcUbuntuAppearancePanel *self)
{
  const gchar *gtk_theme = NULL;
  const gchar *gedit_theme = NULL;
  g_autoptr(GList) selected = NULL;

  selected = gtk_flow_box_get_selected_children (self->theme_box);
  if (selected != NULL)
    {
      GtkFlowBoxChild *selected_item = GTK_FLOW_BOX_CHILD (g_list_nth_data (selected, 0));
      if (selected_item == self->theme_default)
      {
        gtk_theme = UBUNTU_THEME;
        gedit_theme = UBUNTU_THEME;
      }
      else if (selected_item == self->theme_dark)
      {
        gtk_theme = UBUNTU_DARK_THEME;
        gedit_theme = UBUNTU_DARK_THEME;
      }
    }

  if (gtk_theme != NULL)
    g_settings_set_string (self->interface_settings, GTK_THEME_KEY, gtk_theme);
  if (gedit_theme != NULL)
    g_settings_set_string (self->gedit_settings, GEDIT_THEME_KEY, gedit_theme);
}

static void
on_interface_settings_changed (CcUbuntuAppearancePanel *self)
{
  g_autofree gchar *gtk_theme = NULL;
  g_autofree gchar *cursor_theme = NULL;
  g_autofree gchar *icon_theme = NULL;
  GtkFlowBoxChild *theme_item = NULL;

  gtk_theme = g_settings_get_string (self->interface_settings, GTK_THEME_KEY);
  cursor_theme = g_settings_get_string (self->interface_settings, CURSOR_THEME_KEY);
  icon_theme = g_settings_get_string (self->interface_settings, ICON_THEME_KEY);

  if (g_str_equal (cursor_theme, UBUNTU_THEME) && g_str_equal (icon_theme, UBUNTU_THEME))
    {
      if (g_strcmp0 (gtk_theme, UBUNTU_THEME) == 0)
        theme_item = self->theme_default;
      else if (g_strcmp0 (gtk_theme, UBUNTU_DARK_THEME) == 0)
        theme_item = self->theme_dark;
    }

  if (theme_item != NULL)
    gtk_flow_box_select_child (self->theme_box, theme_item);
  else
    gtk_flow_box_unselect_all (self->theme_box);
}

static void
cc_ubuntuappearance_panel_class_init (CcUbuntuAppearancePanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_ubuntuappearance_panel_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/ubuntuappearance/cc-ubuntuappearance-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcUbuntuAppearancePanel, dyslexia_friendly_switch);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuAppearancePanel, theme_box);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuAppearancePanel, theme_dark);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuAppearancePanel, theme_default);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuAppearancePanel, text_size_scale);
  gtk_widget_class_bind_template_child (widget_class, CcUbuntuAppearancePanel, text_size_label);

  gtk_widget_class_bind_template_callback (widget_class, on_theme_box_selected_children_changed);
}

static void
cc_ubuntuappearance_panel_init (CcUbuntuAppearancePanel *self)
{
  g_autoptr(GSettingsSchema) schema = NULL;

  g_resources_register (cc_ubuntuappearance_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  self->interface_settings = g_settings_new (INTERFACE_SCHEMA);
  g_signal_connect_object (self->interface_settings, "changed::" GTK_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->interface_settings, "changed::" CURSOR_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->interface_settings, "changed::" ICON_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
  self->gedit_settings = g_settings_new (GEDIT_PREFRENCES_SCHEMA);
  g_signal_connect_object (self->gedit_settings, "changed::" GEDIT_THEME_KEY,
                           G_CALLBACK (on_interface_settings_changed), self, G_CONNECT_SWAPPED);
  g_settings_bind_with_mapping (self->interface_settings, FONT_KEY,
                                self->dyslexia_friendly_switch,
                                "active", G_SETTINGS_BIND_DEFAULT,
                                get_dyslexia_friendly_mapping,
                                set_dyslexia_friendly_mapping,
                                self,
                                NULL);
  g_settings_bind (self->interface_settings, "text-scaling-factor",
                   gtk_range_get_adjustment (GTK_RANGE (self->text_size_scale)), "value",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind_with_mapping (self->interface_settings, KEY_TEXT_SCALING_FACTOR,
                                self->text_size_label,
                                "label", G_SETTINGS_BIND_GET,
                                text_size_label_mapping_get,
                                NULL, NULL, NULL);

  on_interface_settings_changed (self);
  load_custom_css (self);
}

void
cc_ubuntuappearance_panel_static_init_func (void)
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
