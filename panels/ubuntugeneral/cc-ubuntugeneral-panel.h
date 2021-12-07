/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2017 Canonical Ltd
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

#ifndef _CC_UBUNTUGENERAL_PANEL_H_
#define _CC_UBUNTUGENERAL_PANEL_H_

#include <gio/gio.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_UBUNTUGENERAL_PANEL (cc_ubuntugeneral_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcUbuntuGeneralPanel, cc_ubuntugeneral_panel, CC, UBUNTUGENERAL_PANEL, CcPanel)

void cc_ubuntugeneral_panel_static_init_func (void);

G_END_DECLS

#endif /* _CC_EDIT_DIALOG_H_ */
