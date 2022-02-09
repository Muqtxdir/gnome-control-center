/* cc-xkb-modifier-dialog.c
 *
 * Copyright 2019 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib/gi18n.h>
#define HANDY_USE_UNSTABLE_API
#include <handy.h>

#include "cc-xkb-modifier-dialog.h"
#include "list-box-helper.h"

struct _CcXkbModifierDialog
{
  GtkDialog       parent_instance;

  GtkLabel       *description_label;
  GtkSwitch      *default_switch;
  GtkListBox     *listbox;
  GtkListBox     *switch_listbox;
  HdyActionRow   *switch_row;

  GSettings      *input_source_settings;
  const CcXkbModifier *modifier;
  GSList         *radio_group;
  gboolean        updating_active_radio;
};

G_DEFINE_TYPE (CcXkbModifierDialog, cc_xkb_modifier_dialog, GTK_TYPE_DIALOG)

static const gchar *custom_css =
".xkb-option-button {"
"    padding: 12px"
"}";

static const CcXkbOption*
get_xkb_option_from_name (const CcXkbModifier *modifier, const gchar* name)
{
  const CcXkbOption *options = modifier->options;
  int i;

  for (i = 0; options[i].label; i++)
    {
      if (g_strcmp0 (name, options[i].xkb_option) == 0)
        return &options[i];
    }

  return NULL;
}

static GtkRadioButton *
get_radio_button_from_xkb_option_name (CcXkbModifierDialog *self,
                                       const gchar         *name)
{
  gchar *xkb_option;
  GSList *l;

  for (l = self->radio_group; l != NULL; l = l->next)
    {
      xkb_option = g_object_get_data (l->data, "xkb-option");
      if (g_strcmp0 (xkb_option, name) == 0)
        return l->data;
    }

  return NULL;
}

static void
update_active_radio (CcXkbModifierDialog *self)
{
  g_auto(GStrv) options = NULL;
  GtkRadioButton *none_radio;
  guint i;
  gboolean have_nodefault_option = FALSE;

  // Block `on_active_radio_changed_cb` from running
  self->updating_active_radio = TRUE;

  options = g_settings_get_strv (self->input_source_settings, "xkb-options");

  for (i = 0; options != NULL && options[i] != NULL; i++)
    {
      GtkRadioButton *radio;

      if (!g_str_has_prefix (options[i], self->modifier->prefix))
        continue;

      if (g_strcmp0 (options[i], self->modifier->nodefault_option) == 0)
        have_nodefault_option = TRUE;

      radio = get_radio_button_from_xkb_option_name (self, options[i]);

      if (!radio)
        continue;

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
      gtk_switch_set_active (self->default_switch, FALSE);
      self->updating_active_radio = FALSE;
      return;
    }

  if (have_nodefault_option)
    {
      none_radio = get_radio_button_from_xkb_option_name (self, self->modifier->nodefault_option);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (none_radio), TRUE);
      gtk_switch_set_active (self->default_switch, FALSE);
    }
  else
    {
      gtk_switch_set_active (self->default_switch, TRUE);
    }

  self->updating_active_radio = FALSE;
}

static void
set_xkb_option (CcXkbModifierDialog *self,
                gchar               *xkb_option)
{
  g_autoptr(GPtrArray) array = NULL;
  g_auto(GStrv) options = NULL;
  gboolean found;
  guint i;
  gboolean add_nodefault;

  add_nodefault = xkb_option != self->modifier->default_option
               && xkb_option != self->modifier->nodefault_option
               && xkb_option != NULL
               && self->modifier->nodefault_option != NULL;

  /* Either replace the existing "<modifier>:" option in the string
   * array, or add the option at the end
   */
  array = g_ptr_array_new ();
  options = g_settings_get_strv (self->input_source_settings, "xkb-options");
  found = FALSE;

  for (i = 0; options != NULL && options[i] != NULL; i++)
    {
      if (g_str_has_prefix (options[i], self->modifier->prefix))
        {
          if (!found && add_nodefault)
            g_ptr_array_add (array, self->modifier->nodefault_option);
          if (!found && xkb_option != NULL)
            g_ptr_array_add (array, xkb_option);
          found = TRUE;
        }
      else
        {
          g_ptr_array_add (array, options[i]);
        }
    }

  if (!found && add_nodefault)
    g_ptr_array_add (array, self->modifier->nodefault_option);
  if (!found && xkb_option != NULL)
    g_ptr_array_add (array, xkb_option);

  g_ptr_array_add (array, NULL);

  g_settings_set_strv (self->input_source_settings,
                       "xkb-options",
                       (const gchar * const *) array->pdata);
}

static void
on_active_radio_changed_cb (CcXkbModifierDialog *self,
                            GtkRadioButton      *radio)
{
  gchar *xkb_option;

  if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio)))
    return;

  if (gtk_switch_get_state (self->default_switch))
    return;

  if (self->updating_active_radio)
    return;

  xkb_option = (gchar *)g_object_get_data (G_OBJECT (radio), "xkb-option");
  set_xkb_option (self, xkb_option);
}

static void
on_xkb_options_changed_cb (CcXkbModifierDialog *self)
{
  if (self->modifier == NULL)
    update_active_radio (self);
}

static gboolean
default_switch_changed_cb (GtkSwitch *widget,
                           gboolean   state,
                           gpointer   user_data)
{
  CcXkbModifierDialog *self = user_data;
  gchar *xkb_option;
  GSList *l;

  gtk_widget_set_sensitive (GTK_WIDGET (self->listbox), !state);

  if (!state)
    {
      for (l = self->radio_group; l != NULL; l = l->next)
        {
          if (gtk_toggle_button_get_active (l->data))
            {
              xkb_option = (gchar *)g_object_get_data (l->data, "xkb-option");
              set_xkb_option (self, xkb_option);
              break;
            }
        }
    }
  else
    {
      set_xkb_option (self, NULL);
    }

  return FALSE;
}

static void
cc_xkb_modifier_dialog_finalize (GObject *object)
{
  CcXkbModifierDialog *self = (CcXkbModifierDialog *)object;

  g_clear_object (&self->input_source_settings);

  G_OBJECT_CLASS (cc_xkb_modifier_dialog_parent_class)->finalize (object);
}

static void
cc_xkb_modifier_dialog_class_init (CcXkbModifierDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cc_xkb_modifier_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/keyboard/cc-xkb-modifier-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, CcXkbModifierDialog, description_label);
  gtk_widget_class_bind_template_child (widget_class, CcXkbModifierDialog, default_switch);
  gtk_widget_class_bind_template_child (widget_class, CcXkbModifierDialog, listbox);
  gtk_widget_class_bind_template_child (widget_class, CcXkbModifierDialog, switch_listbox);
  gtk_widget_class_bind_template_child (widget_class, CcXkbModifierDialog, switch_row);

  gtk_widget_class_bind_template_callback (widget_class, default_switch_changed_cb);
}

static void
add_radio_buttons (CcXkbModifierDialog *self)
{
  GtkWidget *row, *radio_button, *label, *last_button = NULL;
  CcXkbOption *options = self->modifier->options;
  int i;

  for (i = 0; options[i].label != NULL; i++)
    {
      row = g_object_new (GTK_TYPE_LIST_BOX_ROW,
                          "visible", TRUE,
                          "selectable", FALSE,
                          NULL);
      gtk_container_add (GTK_CONTAINER (self->listbox), row);

      radio_button = g_object_new (GTK_TYPE_RADIO_BUTTON,
                                   "visible", TRUE,
                                   "can_focus", TRUE,
                                   "receives_default", FALSE,
                                   "draw_indicator", TRUE,
                                   NULL);
      label = g_object_new (GTK_TYPE_LABEL,
                            "visible", TRUE,
                            "margin_left", 6,
                            "label", g_dpgettext2 (NULL, "keyboard key", options[i].label),
                            NULL);
      gtk_container_add (GTK_CONTAINER (radio_button), label);
      gtk_style_context_add_class (gtk_widget_get_style_context (radio_button), "xkb-option-button");
      gtk_radio_button_join_group (GTK_RADIO_BUTTON (radio_button), GTK_RADIO_BUTTON (last_button));
      g_object_set_data (G_OBJECT (radio_button), "xkb-option", options[i].xkb_option);
      g_signal_connect_object (radio_button, "toggled", (GCallback)on_active_radio_changed_cb, self, G_CONNECT_SWAPPED);
      gtk_container_add (GTK_CONTAINER (row), radio_button);

      last_button = radio_button;
    }

  self->radio_group = NULL;
  if (last_button != NULL)
    self->radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (last_button));
}

static void
cc_xkb_modifier_dialog_init (CcXkbModifierDialog *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider, custom_css, -1, NULL);

  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

  self->modifier = NULL;

  self->input_source_settings = g_settings_new ("org.gnome.desktop.input-sources");
  g_signal_connect_object (self->input_source_settings,
                           "changed::xkb-options",
                           G_CALLBACK (on_xkb_options_changed_cb),
                           self, G_CONNECT_SWAPPED);
}

CcXkbModifierDialog *
cc_xkb_modifier_dialog_new (GSettings *input_settings,
                            const CcXkbModifier *modifier)
{
  CcXkbModifierDialog *self;

  self = g_object_new (CC_TYPE_XKB_MODIFIER_DIALOG,
                       "use-header-bar", TRUE,
                       NULL);
  self->input_source_settings = g_object_ref (input_settings);

  self->modifier = modifier;
  gtk_window_set_title (GTK_WINDOW (self), gettext (modifier->title));
  gtk_label_set_markup (self->description_label, gettext (modifier->description));
  add_radio_buttons (self);
  update_active_radio (self);
  gtk_widget_set_sensitive (GTK_WIDGET (self->listbox), !gtk_switch_get_state (self->default_switch));

  return self;
}

gboolean
xcb_modifier_transform_binding_to_label (GValue   *value,
                                         GVariant *variant,
                                         gpointer  user_data)
{
  const CcXkbModifier *modifier = user_data;
  const CcXkbOption *entry = NULL;
  const char **items;
  guint i;
  gboolean have_nodefault_option = FALSE;

  items = g_variant_get_strv (variant, NULL);

  for (i = 0; items != NULL && items[i] != NULL; i++)
    {
      entry = get_xkb_option_from_name (modifier, items[i]);
      if (entry != NULL)
        break;

      if (g_strcmp0 (items[i], modifier->nodefault_option) == 0)
       have_nodefault_option = TRUE;
    }

  if (entry != NULL)
    g_value_set_string (value,
                        g_dpgettext2 (NULL, "keyboard key", entry->label));
  if (entry == NULL && have_nodefault_option)
    g_value_set_string (value, _("None"));
  else if (entry == NULL)
    g_value_set_string (value, _("Layout default"));

  return TRUE;
}
