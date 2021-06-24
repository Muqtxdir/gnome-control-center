/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* cc-list-row.h
 *
 * Copyright 2019 Purism SPC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CC_TYPE_LIST_ROW (cc_list_row_get_type())
G_DECLARE_FINAL_TYPE (CcListRow, cc_list_row, CC, LIST_ROW, GtkListBoxRow)

void       cc_list_row_set_icon_name        (CcListRow   *self,
                                             const gchar *icon_name);
void       cc_list_row_set_show_switch      (CcListRow   *self,
                                             gboolean     show_switch);
gboolean   cc_list_row_get_active           (CcListRow   *self);
void       cc_list_row_activate             (CcListRow   *self);
void       cc_list_row_set_secondary_label  (CcListRow   *self,
                                             const gchar *label);
void       cc_list_row_set_secondary_markup (CcListRow   *self,
                                             const gchar *markup);

G_END_DECLS
