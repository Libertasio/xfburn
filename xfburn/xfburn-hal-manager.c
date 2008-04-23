/* $Id: xfburn-hal-manager.c 4382 2006-11-01 17:08:37Z pollux $ */
/*
 *  Copyright (c) 2005-2006 Jean-François Wauthy (pollux@xfce.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef	HAVE_CONFIG_H
#include <config.h>
#endif /* !HAVE_CONFIG_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <libxfce4util/libxfce4util.h>

#include "xfburn-global.h"
#include "xfburn-progress-dialog.h"

#include "xfburn-hal-manager.h"

static void xfburn_hal_manager_class_init (XfburnHalManagerClass * klass);
static void xfburn_hal_manager_init (XfburnHalManager * sp);

static void cb_new_output (XfburnHalManager * dialog, const gchar * output, gpointer data);

/*********************/
/* class declaration */
/*********************/
static XfburnProgressDialogClass *parent_class = NULL;

GtkType
xfburn_hal_manager_get_type ()
{
  static GtkType type = 0;

  if (type == 0) {
    static const GTypeInfo our_info = {
      sizeof (XfburnHalManagerClass),
      NULL,
      NULL,
      (GClassInitFunc) xfburn_hal_manager_class_init,
      NULL,
      NULL,
      sizeof (XfburnHalManager),
      0,
      (GInstanceInitFunc) xfburn_hal_manager_init,
    };

    type = g_type_register_static (G_TYPE_OBJECT, "XfburnHalManager", &our_info, 0);
  }

  return type;
}

static void
xfburn_hal_manager_class_init (XfburnHalManagerClass * klass)
{
  parent_class = g_type_class_peek_parent (klass);
}

static void
xfburn_hal_manager_init (XfburnHalManager * obj)
{
  g_signal_connect_after (G_OBJECT (obj), "output", G_CALLBACK (cb_new_output), NULL);
}

/*           */
/* internals */
/*           */
static void
cb_new_output (XfburnHalManager * dialog, const gchar * output, gpointer data)
{
  static gint readcd_end = -1;

  if (strstr (output, READCD_DONE)) {
    xfburn_progress_dialog_set_status (XFBURN_PROGRESS_DIALOG (dialog), XFBURN_PROGRESS_DIALOG_STATUS_COMPLETED);
  }
  else if (strstr (output, READCD_PROGRESS)) {
    gint readcd_done = -1;
    gdouble fraction;

    sscanf (output, "%*s %d", &readcd_done);
    fraction = ((gdouble) readcd_done) / readcd_end;

    xfburn_progress_dialog_set_progress_bar_fraction (XFBURN_PROGRESS_DIALOG (dialog), fraction);
  }
  else if (strstr (output, READCD_CAPACITY)) {
    xfburn_progress_dialog_set_action_text (XFBURN_PROGRESS_DIALOG (dialog), _("Reading CD..."));
    sscanf (output, "%*s %d", &readcd_end);
  }
}

/*        */
/* public */
/*        */

GtkWidget *
xfburn_hal_manager_new ()
{
  XfburnHalManager *obj;

  obj = XFBURN_CREATE_HAL_MANAGER (g_object_new (XFBURN_TYPE_CREATE_HAL_MANAGER,
                                                         "show-buffers", FALSE, "title", _("Create ISO from CD"), NULL));

  return GTK_WIDGET (obj);
}